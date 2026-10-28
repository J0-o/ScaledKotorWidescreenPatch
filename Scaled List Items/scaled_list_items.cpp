#include <windows.h>

#include "../Common/ResolutionScale.h"

namespace {

constexpr DWORD ItemVisualHeightAddress = 0x006B527F;
constexpr DWORD QuantityHeightAddress = 0x006B5332;
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
constexpr DWORD GuiManagerPointerAddress = 0x007A39F4;
constexpr DWORD GuiManagerPanelsOffset = 0x88;
constexpr DWORD GuiManagerPanelCountOffset = 0x8C;
constexpr DWORD PanelChildrenOffset = 0x20;
constexpr DWORD PanelChildCountOffset = 0x24;
constexpr DWORD ListboxSetExtentAddress = 0x0041BF80;
constexpr DWORD ListboxOrganizeControlsAddress = 0x0041B140;
constexpr DWORD OptionsCheckboxVtable = 0x00758258;
constexpr DWORD OptionsCheckboxBorderOffset = 0x6C;
constexpr DWORD OptionsCheckboxHilightOffset = 0xE0;
constexpr DWORD OptionsCheckboxTextOffset = 0x154;
constexpr DWORD OptionsCheckboxSelectedOffset = 0x1CC;
constexpr DWORD OptionsCheckboxHilightSelectedOffset = 0x240;
constexpr int OptionsCheckboxIconSize = 25;
constexpr int OptionsCheckboxTextGap = 5;
constexpr int SkillRowHeight = 0x2A;
constexpr int FeatPowerGroupRowHeight = 0x28;
constexpr int MaximumAbilityRows = 4096;
constexpr int MaximumModOptionRows = 512;
constexpr DWORD MaximumPanelCount = 256;
constexpr DWORD MaximumChildCount = 1024;

struct Rect {
    int left;
    int top;
    int width;
    int height;
};

struct ModOptionRowBaseline {
    void* row;
    int height;
};

ModOptionRowBaseline modOptionRows[MaximumModOptionRows] = {};

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

void callControlSetRect(void* control, const Rect& rect) {
    if (!control) {
        return;
    }

    __try {
        const DWORD vtable = *reinterpret_cast<DWORD*>(control);
        const DWORD setRect = *reinterpret_cast<DWORD*>(vtable + 4);
        if (setRect != 0) {
            typedef void(__thiscall *SetRectFn)(void*, const Rect*);
            reinterpret_cast<SetRectFn>(setRect)(control, &rect);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void scaleOptionsCheckboxControl(void* controlPtr,
                                 const UniversalScaleState& scale) {
    if (!controlPtr) {
        return;
    }

    __try {
        char* control = static_cast<char*>(controlPtr);
        if (*reinterpret_cast<DWORD*>(control) != OptionsCheckboxVtable) {
            return;
        }

        const Rect row = *reinterpret_cast<Rect*>(control + 0x04);
        const int iconSize = adjustedValue(OptionsCheckboxIconSize, scale);
        const int textOffset = iconSize +
            adjustedValue(OptionsCheckboxTextGap, scale);
        const Rect icon = {
            row.left,
            row.top + (row.height - iconSize) / 2,
            iconSize,
            iconSize,
        };
        const Rect text = {
            row.left + textOffset,
            row.top,
            row.width > textOffset ? row.width - textOffset : 0,
            row.height,
        };

        callControlSetRect(control + OptionsCheckboxBorderOffset, icon);
        callControlSetRect(control + OptionsCheckboxHilightOffset, icon);
        callControlSetRect(control + OptionsCheckboxSelectedOffset, icon);
        callControlSetRect(control + OptionsCheckboxHilightSelectedOffset, icon);
        callControlSetRect(control + OptionsCheckboxTextOffset, text);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool isListbox(void* control) {
    DWORD vtable = 0;
    DWORD setExtent = 0;
    return safeReadDword(control, vtable) && vtable != 0 &&
        safeReadDword(reinterpret_cast<const void*>(vtable + 4), setExtent) &&
        setExtent == ListboxSetExtentAddress;
}

void refreshListOptionsCheckboxes(void* listbox,
                                  const UniversalScaleState& scale) {
    if (!isListbox(listbox)) {
        return;
    }

    __try {
        char* list = static_cast<char*>(listbox);
        void** rows = *reinterpret_cast<void***>(list + ListRowsOffset);
        const int rowCount = *reinterpret_cast<int*>(list + ListRowCountOffset);
        if (!rows || rowCount <= 0 || rowCount > MaximumAbilityRows) {
            return;
        }
        for (int index = 0; index < rowCount; ++index) {
            scaleOptionsCheckboxControl(rows[index], scale);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool isModOptionsListbox(void* listbox) {
    if (!listbox || !isListbox(listbox)) {
        return false;
    }

    HMODULE module = GetModuleHandleA("mod-options-gui.dll");
    if (!module) {
        return false;
    }

    __try {
        const DWORD vtable = *reinterpret_cast<DWORD*>(listbox);
        const void* mouseDown = *reinterpret_cast<void**>(vtable + 6 * sizeof(DWORD));
        MEMORY_BASIC_INFORMATION region = {};
        return mouseDown &&
            VirtualQuery(mouseDown, &region, sizeof(region)) == sizeof(region) &&
            region.AllocationBase == module;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void rememberModOptionRow(void* row, int height) {
    ModOptionRowBaseline* empty = nullptr;
    for (ModOptionRowBaseline& baseline : modOptionRows) {
        if (baseline.row == row) {
            baseline.height = height;
            return;
        }
        if (!baseline.row && !empty) {
            empty = &baseline;
        }
    }
    if (empty) {
        empty->row = row;
        empty->height = height;
    }
}

int modOptionRowHeight(void* row) {
    for (const ModOptionRowBaseline& baseline : modOptionRows) {
        if (baseline.row == row) {
            return baseline.height;
        }
    }
    return 0;
}

void scaleModOptionRow(void* row, int baseHeight,
                       const UniversalScaleState& scale) {
    if (!row || baseHeight <= 0) {
        return;
    }

    __try {
        Rect rect = *reinterpret_cast<Rect*>(static_cast<char*>(row) + 0x04);
        rect.height = adjustedValue(baseHeight, scale);
        callControlSetRect(row, rect);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void refreshModOptionsListbox(void* listbox,
                              const UniversalScaleState& scale) {
    if (!isModOptionsListbox(listbox)) {
        return;
    }

    __try {
        char* list = static_cast<char*>(listbox);
        void** rows = *reinterpret_cast<void***>(list + ListRowsOffset);
        char* extents = *reinterpret_cast<char**>(list + ListExtentsOffset);
        const int rowCount = *reinterpret_cast<int*>(list + ListRowCountOffset);
        const int extentCount = *reinterpret_cast<int*>(list + ListExtentCountOffset);
        if (!rows || !extents || rowCount <= 0 || rowCount != extentCount ||
            rowCount > MaximumModOptionRows) {
            return;
        }

        bool changed = false;
        for (int index = 0; index < rowCount; ++index) {
            const int baseHeight = modOptionRowHeight(rows[index]);
            if (baseHeight <= 0) {
                continue;
            }

            const int scaledHeight = adjustedValue(baseHeight, scale);
            int* storedHeight = reinterpret_cast<int*>(
                extents + index * RectSize + RectHeightOffset);
            Rect* rowRect = reinterpret_cast<Rect*>(
                static_cast<char*>(rows[index]) + 0x04);
            if (*storedHeight != scaledHeight || rowRect->height != scaledHeight) {
                Rect rect = *rowRect;
                rect.height = scaledHeight;
                callControlSetRect(rows[index], rect);
                *storedHeight = scaledHeight;
                changed = true;
            }
        }

        if (changed) {
            *reinterpret_cast<DWORD*>(list + ListFlagsOffset) |=
                ListLayoutDirtyFlag;
            typedef void(__thiscall *OrganizeControlsFn)(void*);
            reinterpret_cast<OrganizeControlsFn>(
                ListboxOrganizeControlsAddress)(listbox);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void refreshLiveModOptionsRows(const UniversalScaleState& scale) {
    DWORD guiManager = 0;
    DWORD panels = 0;
    DWORD panelCount = 0;
    if (!safeReadDword(reinterpret_cast<const void*>(GuiManagerPointerAddress), guiManager) ||
        guiManager == 0 ||
        !safeReadDword(reinterpret_cast<const void*>(guiManager + GuiManagerPanelsOffset), panels) ||
        !safeReadDword(reinterpret_cast<const void*>(guiManager + GuiManagerPanelCountOffset), panelCount) ||
        panels == 0 || panelCount > MaximumPanelCount) {
        return;
    }

    for (DWORD panelIndex = 0; panelIndex < panelCount; ++panelIndex) {
        DWORD panel = 0;
        DWORD children = 0;
        DWORD childCount = 0;
        if (!safeReadDword(reinterpret_cast<const void*>(panels + panelIndex * sizeof(DWORD)), panel) ||
            panel == 0 ||
            !safeReadDword(reinterpret_cast<const void*>(panel + PanelChildrenOffset), children) ||
            !safeReadDword(reinterpret_cast<const void*>(panel + PanelChildCountOffset), childCount) ||
            children == 0 || childCount > MaximumChildCount) {
            continue;
        }

        for (DWORD childIndex = 0; childIndex < childCount; ++childIndex) {
            DWORD child = 0;
            if (safeReadDword(reinterpret_cast<const void*>(children + childIndex * sizeof(DWORD)), child) &&
                child != 0) {
                scaleOptionsCheckboxControl(reinterpret_cast<void*>(child), scale);
                refreshListOptionsCheckboxes(reinterpret_cast<void*>(child), scale);
                refreshModOptionsListbox(reinterpret_cast<void*>(child), scale);
            }
        }
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
    refreshLiveModOptionsRows(*scale);
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

extern "C" void __cdecl scaleInventoryQuantityRect(
        void* rectPtr, int quantity) {
    const UniversalScaleState* scale = getScale();
    if (!scale || !rectPtr) {
        return;
    }

    Rect* rect = static_cast<Rect*>(rectPtr);
    // Native layout already derives this edge from the scaled item extent.
    const int right = rect->left + rect->width;
    rect->width = adjustedValue(rect->width, *scale);
    rect->left = right - rect->width;
    if (quantity >= 10 && quantity <= 99) {
        using GetUiMsdfWrapInsetFn = int (__cdecl*)(float);
        HMODULE module = GetModuleHandleA("scaled-kotor.dll");
        GetUiMsdfWrapInsetFn getInset = module ?
            reinterpret_cast<GetUiMsdfWrapInsetFn>(
                GetProcAddress(module, "getUiMsdfWrapInset")) : nullptr;
        if (getInset) {
            const int inset = getInset(1.0f);
            rect->left -= inset / 2;
            rect->width += inset;
        }
    }
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

extern "C" void __cdecl scaleModOptionsRows(void* listbox,
                                               void* controlsArray) {
    const UniversalScaleState* scale = getScale();
    if (!scale || !controlsArray || !isModOptionsListbox(listbox)) {
        return;
    }

    __try {
        void** rows = *reinterpret_cast<void***>(controlsArray);
        const int rowCount = *reinterpret_cast<int*>(
            static_cast<char*>(controlsArray) + sizeof(void*));
        if (!rows || rowCount <= 0 || rowCount > MaximumModOptionRows) {
            return;
        }

        for (int index = 0; index < rowCount; ++index) {
            if (!rows[index]) {
                continue;
            }
            const Rect& rect = *reinterpret_cast<Rect*>(
                static_cast<char*>(rows[index]) + 0x04);
            if (rect.height <= 0 || rect.height >= 8192) {
                continue;
            }
            rememberModOptionRow(rows[index], rect.height);
            scaleModOptionRow(rows[index], rect.height, *scale);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl scaleOptionsCheckbox(void* extentPtr) {
    const UniversalScaleState* scale = getScale();
    if (!scale || !extentPtr) {
        return;
    }
    scaleOptionsCheckboxControl(static_cast<char*>(extentPtr) - 0x04, *scale);
}

extern "C" void __cdecl refreshResolutionDependentUi() {
    refreshPatchedListConstants();
}
