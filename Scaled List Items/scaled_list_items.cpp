#include <windows.h>

#include "../Common/ResolutionScale.h"

namespace {

constexpr DWORD ItemVisualHeightAddress = 0x006B527F;
constexpr DWORD QuantityHeightAddress = 0x006B5332;
constexpr DWORD QuantityWidthShortAddress = 0x006B5339;
constexpr DWORD QuantityWidthLongAddress = 0x006B533C;
constexpr DWORD QuantityTopAddress = 0x006B5351;
constexpr DWORD SkillVisualHeightAddress = 0x006AB8EF;
constexpr DWORD AppManagerPointerAddress = 0x007A39FC;
constexpr DWORD SkillRowVtable = 0x00755ED0;
constexpr DWORD FeatPowerGroupRowVtable = 0x007578A8;
constexpr DWORD ClientAppOffset = 0x04;
constexpr DWORD ClientAppInternalOffset = 0x04;
constexpr DWORD GuiInGameOffset = 0x40;
constexpr DWORD AbilitiesMenuOffset = 0x18;
constexpr DWORD AbilitiesListboxOffset = 0x30DC;
constexpr DWORD ListRowsOffset = 0x29C;
constexpr DWORD ListRowCountOffset = 0x2A0;
constexpr DWORD ListExtentsOffset = 0x2A8;
constexpr DWORD ListExtentCountOffset = 0x2AC;
constexpr DWORD ListFlagsOffset = 0x2BC;
constexpr DWORD ListLayoutDirtyFlag = 0x08;
constexpr DWORD RectHeightOffset = 0x0C;
constexpr DWORD RectSize = 0x10;
constexpr int SkillRowHeight = 0x2A;
constexpr int FeatPowerGroupRowHeight = 0x28;
constexpr int MaximumAbilityRows = 4096;

void writeMemory(void* address, const void* value, size_t size) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return;
    }

    CopyMemory(address, value, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);

    DWORD ignored = 0;
    VirtualProtect(address, size, oldProtect, &ignored);
}

void writeInt(DWORD address, int value) {
    writeMemory(reinterpret_cast<void*>(address), &value, sizeof(value));
}

void writeByte(DWORD address, BYTE value) {
    writeMemory(reinterpret_cast<void*>(address), &value, sizeof(value));
}

int adjustedValue(int vanillaValue, const UniversalScaleState& scale) {
    return scaleTwoXValue(vanillaValue, scale);
}

BYTE adjustedByte(int vanillaValue, const UniversalScaleState& scale) {
    const int value = adjustedValue(vanillaValue, scale);
    return static_cast<BYTE>(value < 0 ? 0 : (value > 255 ? 255 : value));
}

void writeStackValue(void* slot, int vanillaValue,
                     const UniversalScaleState& scale) {
    if (slot) {
        *static_cast<int*>(slot) = adjustedValue(vanillaValue, scale);
    }
}

void patchItemVisualConstants(const UniversalScaleState& scale) {
    writeInt(ItemVisualHeightAddress, adjustedValue(0x38, scale));
    writeInt(QuantityHeightAddress, adjustedValue(0x13, scale));
    writeByte(QuantityWidthShortAddress, adjustedByte(0x15, scale));
    writeByte(QuantityWidthLongAddress, adjustedByte(0x15, scale));
    writeByte(QuantityTopAddress, adjustedByte(0x25, scale));
}

const UniversalScaleState* getScale() {
    return ResolutionScale::get();
}

bool safeReadDword(const void* address, DWORD& value) {
    __try {
        value = *reinterpret_cast<const DWORD*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 0;
        return false;
    }
}

void* currentAbilitiesListbox() {
    DWORD appManager = 0;
    DWORD clientApp = 0;
    DWORD internal = 0;
    DWORD guiInGame = 0;
    DWORD abilitiesMenu = 0;
    if (!safeReadDword(
            reinterpret_cast<const void*>(AppManagerPointerAddress),
            appManager) ||
        !safeReadDword(
            reinterpret_cast<const void*>(appManager + ClientAppOffset),
            clientApp) ||
        !safeReadDword(
            reinterpret_cast<const void*>(clientApp + ClientAppInternalOffset),
            internal) ||
        !safeReadDword(
            reinterpret_cast<const void*>(internal + GuiInGameOffset),
            guiInGame) ||
        !safeReadDword(
            reinterpret_cast<const void*>(guiInGame + AbilitiesMenuOffset),
            abilitiesMenu)) {
        return nullptr;
    }

    return reinterpret_cast<void*>(abilitiesMenu + AbilitiesListboxOffset);
}

void refreshAbilityListExtents(void* listbox,
                               const UniversalScaleState& scale) {
    if (!listbox || listbox != currentAbilitiesListbox()) {
        return;
    }

    __try {
        char* list = static_cast<char*>(listbox);
        void** rows = *reinterpret_cast<void***>(list + ListRowsOffset);
        char* extents = *reinterpret_cast<char**>(list + ListExtentsOffset);
        const int rowCount = *reinterpret_cast<int*>(list + ListRowCountOffset);
        const int extentCount =
            *reinterpret_cast<int*>(list + ListExtentCountOffset);
        if (!rows || !extents || rowCount <= 0 || rowCount != extentCount ||
            rowCount > MaximumAbilityRows) {
            return;
        }

        bool changed = false;
        for (int index = 0; index < rowCount; ++index) {
            void* row = rows[index];
            if (!row) {
                continue;
            }

            const DWORD vtable = *reinterpret_cast<DWORD*>(row);
            int vanillaHeight = 0;
            if (vtable == SkillRowVtable) {
                vanillaHeight = SkillRowHeight;
            }
            else if (vtable == FeatPowerGroupRowVtable) {
                vanillaHeight = FeatPowerGroupRowHeight;
            }
            else {
                continue;
            }

            int* storedHeight = reinterpret_cast<int*>(
                extents + index * RectSize + RectHeightOffset);
            const int scaledHeight = adjustedValue(vanillaHeight, scale);
            if (*storedHeight != scaledHeight) {
                *storedHeight = scaledHeight;
                changed = true;
            }
        }

        if (changed) {
            *reinterpret_cast<DWORD*>(list + ListFlagsOffset) |=
                ListLayoutDirtyFlag;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __cdecl refreshPatchedListConstants() {
    const UniversalScaleState* scale = getScale();
    if (!scale) {
        return;
    }
    patchItemVisualConstants(*scale);
    writeInt(SkillVisualHeightAddress, adjustedValue(0x2A, *scale));
}

}

extern "C" void __cdecl setInventoryItemRowHeight(void* heightSlot) {
    const UniversalScaleState* scale = getScale();
    if (!scale) {
        return;
    }

    writeStackValue(heightSlot, 0x38, *scale);
    patchItemVisualConstants(*scale);
}

extern "C" void __cdecl setSkillItemRowHeight(void* heightSlot) {
    const UniversalScaleState* scale = getScale();
    if (!scale) {
        return;
    }

    writeStackValue(heightSlot, 0x2A, *scale);
    writeInt(SkillVisualHeightAddress, adjustedValue(0x2A, *scale));
}

extern "C" void __cdecl setFeatGroupRowHeight(void* heightSlot) {
    const UniversalScaleState* scale = getScale();
    if (!scale) {
        return;
    }

    writeStackValue(heightSlot, 0x28, *scale);
}

extern "C" void __cdecl setFeatStoredHeight(void* destinationRect) {
    const UniversalScaleState* scale = getScale();
    if (!scale) {
        return;
    }

    *reinterpret_cast<int*>(static_cast<char*>(destinationRect) + 0x0C) =
        adjustedValue(0x28, *scale);
}

extern "C" void __cdecl setGenericListRowHeight(void* sourceRect,
                                                  void* destinationRect) {
    const UniversalScaleState* scale = getScale();
    if (!scale) {
        return;
    }

    const int sourceHeight = *reinterpret_cast<int*>(
        static_cast<char*>(sourceRect) + 0x0C);
    *reinterpret_cast<int*>(static_cast<char*>(destinationRect) + 0x0C) =
        adjustedValue(sourceHeight, *scale);
}

extern "C" void __cdecl prepareAbilitiesListLayout(void* listbox) {
    const UniversalScaleState* scale = getScale();
    if (scale) {
        refreshAbilityListExtents(listbox, *scale);
    }
}

extern "C" void __cdecl refreshResolutionDependentUi() {
    refreshPatchedListConstants();
}
