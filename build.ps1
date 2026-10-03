[CmdletBinding()]
param(
    [string]$OutDir = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'

$patchesDir = $PSScriptRoot
$builder = Join-Path $patchesDir 'create-patch.bat'
$combinedManifest = Join-Path $patchesDir 'manifest.toml'
$stage = Join-Path $patchesDir '_Scaled Kotor Complete'
$packageName = 'ScaledKotorComplete'

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Text
    )

    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Get-SymbolSuffix {
    param([Parameter(Mandatory)][string]$Name)

    return (($Name.ToLowerInvariant() -replace '[^a-z0-9]+', '_').Trim('_'))
}

if (-not (Test-Path -LiteralPath $builder -PathType Leaf)) {
    throw "Patch builder not found: $builder"
}
if (-not (Test-Path -LiteralPath $combinedManifest -PathType Leaf)) {
    throw "Combined patch manifest not found: $combinedManifest"
}
$manifestText = Get-Content -LiteralPath $combinedManifest -Raw
$manifestId = [regex]::Match(
    $manifestText,
    '(?m)^id\s*=\s*"([^"]+)"\s*$')
if (-not $manifestId.Success -or $manifestId.Groups[1].Value -ne 'scaled-kotor') {
    throw 'The combined manifest ID must be "scaled-kotor" because ResolutionScale resolves scaled-kotor.dll at runtime.'
}

$components = @(Get-ChildItem -LiteralPath $patchesDir -Directory | Where-Object {
    $_.Name -like 'Scaled*' -and
    (Test-Path -LiteralPath (Join-Path $_.FullName 'manifest.toml') -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $_.FullName 'hooks.toml') -PathType Leaf)
} | Sort-Object Name)

if (-not $components) {
    throw "No Scaled patch components found under $patchesDir"
}

$provider = $components | Where-Object Name -eq 'Scaled Kotor' | Select-Object -First 1
if (-not $provider) {
    throw 'The Scaled Kotor provider component was not found.'
}

$oldSkipPause = $env:SKIP_PAUSE
try {
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stage | Out-Null

    $entryPoints = @()
    $directRefreshCallbacks = @()
    $exports = New-Object System.Collections.Generic.List[string]
    $seenExports = @{}
    $mergedHooks = New-Object System.Text.StringBuilder
    $hookAddresses = @{}

    foreach ($component in $components) {
        $suffix = Get-SymbolSuffix $component.Name
        $destination = Join-Path $stage $component.Name
        New-Item -ItemType Directory -Path $destination | Out-Null

        $cppFiles = @(Get-ChildItem -LiteralPath $component.FullName -File -Filter '*.cpp')
        $sourceText = ($cppFiles | ForEach-Object {
            Get-Content -LiteralPath $_.FullName -Raw
        }) -join "`n"
        $hasDllMain = $sourceText -match '\bBOOL\s+WINAPI\s+DllMain\s*\('
        $hasRefresh = $sourceText -match 'extern\s+"C"\s+void\s+__cdecl\s+refreshResolutionDependentUi\s*\('
        $dllMainName = "DllMain_$suffix"
        $refreshName = "refreshResolutionDependentUi_$suffix"

        if ($hasDllMain) {
            $entryPoints += [pscustomobject]@{
                Name = $dllMainName
                Provider = ($component.Name -eq 'Scaled Kotor')
            }
        }
        if ($hasRefresh -and -not $hasDllMain) {
            $directRefreshCallbacks += $refreshName
        }

        foreach ($source in Get-ChildItem -LiteralPath $component.FullName -File | Where-Object {
            $_.Extension -in '.cpp', '.h'
        }) {
            $text = Get-Content -LiteralPath $source.FullName -Raw

            # The staged component is one directory deeper than the normal /I
            # path. Let the existing ../Common include directory resolve these.
            $text = $text.Replace('../Common/', '')
            if ($hasDllMain) {
                $text = [regex]::Replace($text, '\bDllMain\b', $dllMainName)
            }
            if ($hasRefresh) {
                $text = [regex]::Replace(
                    $text,
                    '\brefreshResolutionDependentUi\b',
                    $refreshName)
            }

            # cl /MP writes objects by source basename. Prefix every staged CPP
            # so the many component export.cpp files cannot overwrite each other.
            $destinationName = if ($source.Extension -eq '.cpp') {
                "${suffix}__$($source.Name)"
            } else {
                $source.Name
            }
            Write-Utf8NoBom (Join-Path $destination $destinationName) $text
        }

        $defPath = Join-Path $component.FullName 'exports.def'
        if (Test-Path -LiteralPath $defPath -PathType Leaf) {
            foreach ($line in Get-Content -LiteralPath $defPath) {
                $symbol = $line.Trim()
                if (-not $symbol -or $symbol -match '^(LIBRARY|EXPORTS)(\s|$)') {
                    continue
                }
                if ($hasRefresh -and $symbol -eq 'refreshResolutionDependentUi') {
                    $symbol = $refreshName
                }
                if (-not $seenExports.ContainsKey($symbol)) {
                    $seenExports[$symbol] = $true
                    $exports.Add($symbol)
                }
            }
        }

        $hooksPath = Join-Path $component.FullName 'hooks.toml'
        $hooksText = Get-Content -LiteralPath $hooksPath -Raw
        foreach ($addressMatch in [regex]::Matches(
            $hooksText,
            '(?m)^address\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)\s*$')) {
            $address = $addressMatch.Groups[1].Value.ToLowerInvariant()
            if ($hookAddresses.ContainsKey($address)) {
                throw "Duplicate hook address $address in $($hookAddresses[$address]) and $($component.Name)."
            }
            $hookAddresses[$address] = $component.Name
        }
        [void]$mergedHooks.AppendLine("# === $($component.Name) ===")
        [void]$mergedHooks.AppendLine($hooksText.Trim())
        [void]$mergedHooks.AppendLine()
    }

    $providerEntry = $entryPoints | Where-Object Provider | Select-Object -First 1
    if (-not $providerEntry) {
        throw 'Scaled Kotor does not define DllMain.'
    }
    $consumerEntries = @($entryPoints | Where-Object { -not $_.Provider })

    $entryDeclarations = $entryPoints | ForEach-Object {
        "BOOL WINAPI $($_.Name)(HINSTANCE, DWORD, LPVOID);"
    }
    $refreshDeclarations = $directRefreshCallbacks | ForEach-Object {
        "extern `"C`" void __cdecl $_();"
    }
    $attachCalls = $consumerEntries | ForEach-Object {
        "        if (!$($_.Name)(instance, reason, reserved)) return FALSE;"
    }
    $subscribeCalls = $directRefreshCallbacks | ForEach-Object {
        "        registerResolutionRefreshCallback($_);"
    }
    $unsubscribeCalls = @($directRefreshCallbacks | ForEach-Object {
        "        unregisterResolutionRefreshCallback($_);"
    })
    [array]::Reverse($unsubscribeCalls)
    $detachCalls = @($consumerEntries | ForEach-Object {
        "        $($_.Name)(instance, reason, reserved);"
    })
    [array]::Reverse($detachCalls)

    $aggregateSource = @"
#include <windows.h>

extern "C" int __cdecl registerResolutionRefreshCallback(void (__cdecl*)());
extern "C" void __cdecl unregisterResolutionRefreshCallback(void (__cdecl*)());
$($entryDeclarations -join "`n")
$($refreshDeclarations -join "`n")

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (!$($providerEntry.Name)(instance, reason, reserved)) return FALSE;
$($attachCalls -join "`n")
$($subscribeCalls -join "`n")
    }
    else if (reason == DLL_PROCESS_DETACH) {
$($unsubscribeCalls -join "`n")
$($detachCalls -join "`n")
        $($providerEntry.Name)(instance, reason, reserved);
    }
    return TRUE;
}
"@
    Write-Utf8NoBom (Join-Path $stage 'combined_entry.cpp') $aggregateSource

    Copy-Item -LiteralPath $combinedManifest -Destination (Join-Path $stage 'manifest.toml')
    Write-Utf8NoBom (Join-Path $stage 'hooks.toml') $mergedHooks.ToString()

    $defText = "LIBRARY ScaledKotor`r`nEXPORTS`r`n" +
        (($exports | ForEach-Object { "    $_" }) -join "`r`n") + "`r`n"
    Write-Utf8NoBom (Join-Path $stage 'exports.def') $defText

    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    $resolvedOutDir = (Resolve-Path -LiteralPath $OutDir).Path

    Write-Host ("Building {0} components as Scaled Kotor Complete..." -f $components.Count)
    $env:SKIP_PAUSE = '1'
    Push-Location $stage
    try {
        & cmd.exe /d /c "call `"$builder`" `"$packageName`""
        if ($LASTEXITCODE -ne 0) {
            throw "Combined patch build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }

    $builtPackage = Join-Path $stage "$packageName.kpatch"
    if (-not (Test-Path -LiteralPath $builtPackage -PathType Leaf)) {
        throw "Builder succeeded but did not produce $builtPackage"
    }
    $outputPackage = Join-Path $resolvedOutDir "$packageName.kpatch"
    Copy-Item -LiteralPath $builtPackage -Destination $outputPackage -Force
    Write-Host "Combined patch written to $outputPackage"
}
finally {
    if ($null -eq $oldSkipPause) {
        Remove-Item Env:SKIP_PAUSE -ErrorAction SilentlyContinue
    }
    else {
        $env:SKIP_PAUSE = $oldSkipPause
    }
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
