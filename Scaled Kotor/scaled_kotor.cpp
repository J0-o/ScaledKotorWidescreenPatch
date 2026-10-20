// Scaled Kotor provider and deferred refresh implementation.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GameAPI/GameVersion.h"
#include "resolution_scale.h"

namespace {

constexpr uintptr_t ScreenWidthAddress = 0x0078D1D4;
constexpr uintptr_t ScreenHeightAddress = 0x0078D1D8;
constexpr uintptr_t CenterGuiRootAddress = 0x0040A600;
constexpr uintptr_t ActivateRenderWindowAddress = 0x00401E00;
constexpr uintptr_t VirtualMachinePointerAddress = 0x007A3A00;
constexpr uintptr_t AppManagerPointerAddress = 0x007A39FC;
constexpr uintptr_t PreviousAntiAliasAddress = 0x0078D440;
constexpr uintptr_t RequestedAntiAliasAddress = 0x007A6888;
constexpr uintptr_t VideoModeChangeRequestAddress = 0x007A3A2C;
constexpr size_t ClientAppOffset = 0x04;
constexpr size_t ClientAppInternalOffset = 0x04;
constexpr size_t GuiInGameOffset = 0x40;
constexpr size_t ResolutionButtonOffset = 0x08BC;
constexpr size_t AdvancedOptionsReinitOffset = 0x2330;
constexpr int DefaultScreenWidth = 800;
constexpr int DefaultScreenHeight = 600;
constexpr int BaseWidth = 640;
constexpr int BaseHeight = 480;
constexpr int MinimumContentScaleHeight = 1080;
constexpr int DefaultScaleFactor = 12;
constexpr double DefaultAdditionalScaleFactor = 1.2;

char IniFile[] = "ScaledKotor.ini";
char IniSection[] = "Scaled Kotor";
char ScaleFactorKey[] = "ScaleFactor";

UniversalScaleState scaleState = {
    DefaultScreenWidth,
    DefaultScreenHeight,
    BaseWidth,
    BaseHeight,
    1,
    1,
    1,
    1,
    0,
    0,
};

bool settingsLoaded = false;
bool scaleStateInitialized = false;
double additionalScaleFactor = DefaultAdditionalScaleFactor;
constexpr int MaxResolutionRefreshCallbacks = 32;
ResolutionRefreshCallback refreshCallbacks[MaxResolutionRefreshCallbacks] = {};

struct RefreshTarget {
    const char* combinedExport;
    const char* standaloneModule;
};

constexpr RefreshTarget FontRefreshTarget = {
    "refreshResolutionDependentUi_scaled_font",
    "font-scale-2x-v1.dll",
};
constexpr RefreshTarget RefreshTargets[] = {
    { "refreshResolutionDependentUi_scaled_menu", "menu-scale-v1.dll" },
    { "refreshResolutionDependentUi_scaled_container_popup", "container-popup-scale-test-v1.dll" },
    { "refreshResolutionDependentUi_scaled_popups", "scaled-popups.dll" },
    { "refreshResolutionDependentUi_scaled_class_menu", "class-selection-layout-constants-test-v1.dll" },
    { "refreshResolutionDependentUi_scaled_panels", "small-root-panel-scale-test-v1.dll" },
    { "refreshResolutionDependentUi_scaled_scrollbars", "scaled-scrollbars-v1.dll" },
    { "refreshResolutionDependentUi_scaled_map_minimap", "area-map-hud-minimap-2x-scale-v1.dll" },
    { "refreshResolutionDependentUi_scaled_list_items", "list-item-height-2x-v1.dll" },
    { "refreshResolutionDependentUi_scaled_text_padding", "scaled-text-padding-v1.dll" },
};
constexpr char ResolutionRefreshExport[] = "refreshResolutionDependentUi";

using SetButtonEnabledFn = void(__thiscall*)(void*, int);
using GetClientOptionsFn = void*(__thiscall*)(void*);

// CGuiInGame::ResetInterfaceForSize (0x0062F5F0) already handles these fields:
// 0x40, 0x54, 0x58, 0x5C, 0x70, 0x74, 0x90, 0x98, 0x9C, and 0xA0.
// Its remaining persistent screen owners were never recentered or dirtied.
constexpr DWORD AdditionalGuiOwnerOffsets[] = {
    0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28,
    0x44, 0x48, 0x4C, 0x50, 0x60, 0x64, 0x68, 0x6C,
    0x78, 0x7C, 0x80, 0x84, 0x8C, 0x94, 0xA4, 0xA8,
};

void centerAndDirtyGuiOwner(void* owner) {
    if (!owner) {
        return;
    }

    __try {
        char* object = static_cast<char*>(owner);
        void* guiManager = *reinterpret_cast<void**>(object + 0x18);
        const int width = *reinterpret_cast<int*>(object + 0x0C);
        const int height = *reinterpret_cast<int*>(object + 0x10);
        if (!guiManager || width <= 0 || height <= 0 ||
            width > 8192 || height > 8192) {
            return;
        }

        typedef void(__thiscall *CenterGuiRootFn)(void*);
        reinterpret_cast<CenterGuiRootFn>(CenterGuiRootAddress)(owner);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void refreshAdditionalNativeGuiRoots(void* guiInGame) {
    if (!guiInGame) {
        return;
    }

    char* base = static_cast<char*>(guiInGame);
    for (DWORD offset : AdditionalGuiOwnerOffsets) {
        void* owner = nullptr;
        __try {
            owner = *reinterpret_cast<void**>(base + offset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }
        centerAndDirtyGuiOwner(owner);
    }
}

ResolutionRefreshCallback findKnownRefreshCallback(const RefreshTarget& target) {
    HMODULE module = GetModuleHandleA("scaled-kotor.dll");
    if (module) {
        ResolutionRefreshCallback callback =
            reinterpret_cast<ResolutionRefreshCallback>(
                GetProcAddress(module, target.combinedExport));
        if (callback) {
            return callback;
        }
    }

    module = GetModuleHandleA(target.standaloneModule);
    return reinterpret_cast<ResolutionRefreshCallback>(
        module ? GetProcAddress(module, ResolutionRefreshExport) : nullptr);
}

bool isKnownRefreshCallback(ResolutionRefreshCallback callback) {
    if (findKnownRefreshCallback(FontRefreshTarget) == callback) {
        return true;
    }
    for (const RefreshTarget& target : RefreshTargets) {
        if (findKnownRefreshCallback(target) == callback) {
            return true;
        }
    }
    return false;
}

void invokeRefreshCallback(ResolutionRefreshCallback callback) {
    if (!callback) {
        return;
    }
    __try {
        callback();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void notifyResolutionRefreshCallbacks() {
    // Resolve family members at event time so DLL load order cannot suppress
    // the notification. Generic menu layout runs before specialized children.
    for (const RefreshTarget& target : RefreshTargets) {
        invokeRefreshCallback(findKnownRefreshCallback(target));
    }

    for (ResolutionRefreshCallback callback : refreshCallbacks) {
        if (callback && !isKnownRefreshCallback(callback)) {
            invokeRefreshCallback(callback);
        }
    }
}

void* currentGuiInGame() {
    __try {
        void* appManager = *reinterpret_cast<void**>(AppManagerPointerAddress);
        if (!appManager) {
            return nullptr;
        }
        void* clientApp = *reinterpret_cast<void**>(
            static_cast<char*>(appManager) + ClientAppOffset);
        if (!clientApp) {
            return nullptr;
        }
        void* internal = *reinterpret_cast<void**>(
            static_cast<char*>(clientApp) + ClientAppInternalOffset);
        return internal ? *reinterpret_cast<void**>(
            static_cast<char*>(internal) + GuiInGameOffset) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool isCurrentGuiInGame(void* guiInGame) {
    return guiInGame && currentGuiInGame() == guiInGame;
}

bool pathBesideExe(const char* name, char* output, DWORD size) {
    const DWORD length = GetModuleFileNameA(nullptr, output, size);
    if (length == 0 || length >= size) {
        return false;
    }

    char* separator = strrchr(output, '\\');
    if (!separator ||
        static_cast<size_t>(separator + 1 - output) + strlen(name) + 1 > size) {
        return false;
    }

    strcpy(separator + 1, name);
    return true;
}

bool readIniInt(const char* path, char* key, int& value) {
    char raw[64] = {};
    if (GetPrivateProfileStringA(
            IniSection, key, nullptr, raw, sizeof(raw), path) == 0) {
        return false;
    }

    char* end = nullptr;
    const long parsed = strtol(raw, &end, 10);
    if (end == raw || *end != '\0' || parsed <= 0) {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

void writeIniInt(const char* path, char* key, int value) {
    char buffer[16];
    sprintf(buffer, "%d", value);
    WritePrivateProfileStringA(IniSection, key, buffer, path);
}

bool loadSettings(bool forceReload = false) {
    if (settingsLoaded && !forceReload) {
        return false;
    }
    settingsLoaded = true;

    char path[MAX_PATH];
    if (!pathBesideExe(IniFile, path, MAX_PATH)) {
        return false;
    }

    int scaleValue = DefaultScaleFactor;
    if (!readIniInt(path, ScaleFactorKey, scaleValue)) {
        writeIniInt(path, ScaleFactorKey, DefaultScaleFactor);
    }

    const double previousScaleFactor = additionalScaleFactor;
    additionalScaleFactor =
        scaleValue >= 10 ? static_cast<double>(scaleValue) / 10.0 : scaleValue;
    return additionalScaleFactor != previousScaleFactor;
}

int readPositiveInt(uintptr_t address, int fallback) {
    __try {
        const int value = *reinterpret_cast<volatile int*>(address);
        return value > 0 ? value : fallback;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

bool updateScaleState(bool reloadSettings = false) {
    const bool settingsChanged = loadSettings(reloadSettings);

    const int screenWidth = readPositiveInt(ScreenWidthAddress, DefaultScreenWidth);
    const int screenHeight = readPositiveInt(ScreenHeightAddress, DefaultScreenHeight);
    if (scaleStateInitialized &&
        !settingsChanged &&
        screenWidth == scaleState.screenWidth &&
        screenHeight == scaleState.screenHeight) {
        return false;
    }

    int scaleNumerator = 0;
    int scaleDenominator = 0;
    if (static_cast<long long>(screenHeight) * BaseWidth <=
        static_cast<long long>(screenWidth) * BaseHeight) {
        scaleNumerator = screenHeight;
        scaleDenominator = BaseHeight;
    }
    else {
        scaleNumerator = screenWidth;
        scaleDenominator = BaseWidth;
    }

    const int contentScalingEnabled =
        screenHeight >= MinimumContentScaleHeight ? 1 : 0;
    int contentScaleNumerator = 1;
    int contentScaleDenominator = 1;
    if (contentScalingEnabled) {
        contentScaleNumerator =
            static_cast<int>(
                scaleNumerator * additionalScaleFactor * 1000.0 + 0.5);
        contentScaleDenominator = scaleDenominator * 1000;
    }

    const int uiWidth = divideRoundedNearest(
        static_cast<long long>(BaseWidth) * scaleNumerator, scaleDenominator);
    const int uiHeight = divideRoundedNearest(
        static_cast<long long>(BaseHeight) * scaleNumerator, scaleDenominator);

    scaleState = {
        screenWidth,
        screenHeight,
        uiWidth,
        uiHeight,
        scaleNumerator,
        scaleDenominator,
        contentScaleNumerator,
        contentScaleDenominator,
        contentScalingEnabled,
        scaleState.layoutGeneration + 1,
    };
    scaleStateInitialized = true;
    return true;
}

enum PendingScaleRefresh {
    PendingGeometryRefresh = 1,
    PendingContentRefresh = 2,
};

LONG pendingScaleRefresh = 0;

void queueScaleRefresh(LONG flags) {
    // All three entry points run on the game's main thread.
    pendingScaleRefresh |= flags;
}

void invokeKnownRefreshTarget(size_t index) {
    invokeRefreshCallback(findKnownRefreshCallback(RefreshTargets[index]));
}

}

extern "C" void __cdecl setResolutionButtonAvailability(void* graphicsOptions) {
    if (!graphicsOptions) {
        return;
    }

    __try {
        void* resolutionButton =
            static_cast<unsigned char*>(graphicsOptions) + ResolutionButtonOffset;
        SetButtonEnabledFn setEnabled =
            reinterpret_cast<SetButtonEnabledFn>(0x00418DB0);
        setEnabled(resolutionButton, 1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl queueVideoModeResetForAntiAliasChange(void* advancedOptions) {
    if (!advancedOptions) {
        return;
    }

    __try {
        const int previousAntiAlias =
            *reinterpret_cast<volatile int*>(PreviousAntiAliasAddress);
        const int requestedAntiAlias =
            *reinterpret_cast<volatile int*>(RequestedAntiAliasAddress);
        if (previousAntiAlias == requestedAntiAlias) {
            return;
        }

        const int immediateReinit =
            *reinterpret_cast<const int*>(
                static_cast<const unsigned char*>(advancedOptions) +
                AdvancedOptionsReinitOffset);
        volatile int* videoModeChangeRequest =
            reinterpret_cast<volatile int*>(VideoModeChangeRequestAddress);
        if (immediateReinit != 0 || *videoModeChangeRequest != 0) {
            return;
        }

        void* appManager =
            *reinterpret_cast<void* const volatile*>(AppManagerPointerAddress);
        if (!appManager) {
            return;
        }

        void* clientApp =
            *reinterpret_cast<void**>(
                static_cast<unsigned char*>(appManager) + ClientAppOffset);
        if (!clientApp) {
            return;
        }

        GetClientOptionsFn getClientOptions =
            reinterpret_cast<GetClientOptionsFn>(0x005ED700);
        const unsigned int* clientOptions =
            static_cast<const unsigned int*>(getClientOptions(clientApp));
        if (!clientOptions) {
            return;
        }

        const bool fullscreen = (*clientOptions & 0x08) != 0;
        *videoModeChangeRequest = fullscreen ? 2 : 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" const UniversalScaleState* __cdecl getUniversalScaleState() {
    updateScaleState();
    return &scaleState;
}

extern "C" void __cdecl reloadScaleFactor(const char*, const char*) {
    if (updateScaleState(true)) {
        queueScaleRefresh(PendingContentRefresh);
    }
}

extern "C" int __cdecl registerResolutionRefreshCallback(
    ResolutionRefreshCallback callback) {
    if (!callback) {
        return 0;
    }

    for (ResolutionRefreshCallback registered : refreshCallbacks) {
        if (registered == callback) {
            return 1;
        }
    }

    for (ResolutionRefreshCallback& registered : refreshCallbacks) {
        if (!registered) {
            registered = callback;
            return 1;
        }
    }
    return 0;
}

extern "C" void __cdecl unregisterResolutionRefreshCallback(
    ResolutionRefreshCallback callback) {
    for (ResolutionRefreshCallback& registered : refreshCallbacks) {
        if (registered == callback) {
            registered = nullptr;
            return;
        }
    }
}

extern "C" void __cdecl onResolutionModeCommitted(void* guiInGame) {
    updateScaleState(true);
    if (!isCurrentGuiInGame(guiInGame)) {
        return;
    }

    // Map/minimap publishes absolute engine dimensions rather than mutating a
    // live GUI tree. The resolution transition still consumes those shared
    // dimensions before the next frame, so keep this one update synchronous.
    invokeKnownRefreshTarget(6);

    // The stock resolution path has already rebuilt its native subset. Defer
    // patch-owned work until the event stack has unwound to the next frame.
    queueScaleRefresh(PendingGeometryRefresh | PendingContentRefresh);

    using ActivateRenderWindowFn = void(__cdecl*)();
    reinterpret_cast<ActivateRenderWindowFn>(ActivateRenderWindowAddress)();
}

extern "C" void __cdecl flushPendingScaleRefresh() {
    const LONG pending = pendingScaleRefresh;
    pendingScaleRefresh = 0;
    if (pending == 0 || !currentGuiInGame()) {
        return;
    }

    if ((pending & PendingContentRefresh) != 0) {
        invokeRefreshCallback(findKnownRefreshCallback(FontRefreshTarget));
    }

    if ((pending & PendingGeometryRefresh) != 0) {
        // Menu, class selection, small panels, and scrollbars.
        invokeKnownRefreshTarget(0);
        invokeKnownRefreshTarget(3);
        invokeKnownRefreshTarget(4);
        invokeKnownRefreshTarget(5);
    }

    if ((pending & PendingContentRefresh) != 0) {
        // Inventory/container, popups, list-item metrics, then list padding.
        invokeKnownRefreshTarget(1);
        invokeKnownRefreshTarget(2);
        invokeKnownRefreshTarget(7);
        invokeKnownRefreshTarget(8);
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(reserved);

    if (reason == DLL_PROCESS_ATTACH) {
        GameVersion::Initialize();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        GameVersion::Reset();
    }

    return TRUE;
}
