#include <windows.h>

#include "../Common/ResolutionScale.h"

namespace {

constexpr DWORD ScrollbarWidthOffset = 0x110;
constexpr DWORD GuiManagerPointerAddress = 0x007A39F4;
constexpr DWORD GuiManagerPanelsOffset = 0x88;
constexpr DWORD GuiManagerPanelCountOffset = 0x8C;
constexpr DWORD PanelChildrenOffset = 0x20;
constexpr DWORD PanelChildCountOffset = 0x24;
constexpr DWORD ListboxSetExtentAddress = 0x0041BF80;
constexpr int BaseScrollbarWidth = 16;
constexpr DWORD MaximumPanelCount = 256;
constexpr DWORD MaximumChildCount = 1024;

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

void applyScrollbarWidth(void* listbox, int scaledWidth) {
    char* object = static_cast<char*>(listbox);
    int* width = reinterpret_cast<int*>(object + ScrollbarWidthOffset);
    if (*width != 0) {
        *width = scaledWidth;
    }
}

bool isListbox(void* control) {
    DWORD vtable = 0;
    DWORD setExtent = 0;
    return safeReadDword(control, vtable) && vtable != 0 &&
        safeReadDword(reinterpret_cast<const void*>(vtable + 4), setExtent) &&
        setExtent == ListboxSetExtentAddress;
}

void __cdecl refreshLiveScrollbars() {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!scale) {
        return;
    }

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

    const int scaledWidth = scaleUiValue(BaseScrollbarWidth, *scale);
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
                child != 0 && isListbox(reinterpret_cast<void*>(child))) {
                applyScrollbarWidth(reinterpret_cast<void*>(child), scaledWidth);
            }
        }
    }
}

}

extern "C" void __cdecl scaleScrollbarWidth(void* listboxPtr) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!listboxPtr || !scale) {
        return;
    }

    __try {
        applyScrollbarWidth(listboxPtr, scaleUiValue(BaseScrollbarWidth, *scale));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl scaleScrollbarArrowExtent(void* extentPtr) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!extentPtr || !scale) {
        return;
    }

    __try {
        int* extent = static_cast<int*>(extentPtr);
        extent[2] = scaleUiValue(extent[2], *scale);
        extent[3] = scaleUiValue(extent[3], *scale);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl refreshResolutionDependentUi() {
    refreshLiveScrollbars();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(reserved);
    if (reason == DLL_PROCESS_ATTACH) {
        ResolutionScale::subscribe(refreshResolutionDependentUi);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        ResolutionScale::unsubscribe(refreshResolutionDependentUi);
    }
    return TRUE;
}
