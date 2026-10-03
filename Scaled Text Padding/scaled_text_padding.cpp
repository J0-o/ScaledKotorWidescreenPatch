#include <windows.h>
#include "../Common/ResolutionScale.h"

namespace {

constexpr DWORD ListboxSetExtentAddress = 0x0041BF80;
constexpr DWORD ListboxSetPaddingAddress = 0x0041C190;
constexpr DWORD MainInterfacePointerAddress = 0x00833BB4;
constexpr DWORD ControlOwnerOffset = 0x34;
constexpr int TextPadding = 7;
constexpr int PaddingBaseHeight = 1080;
constexpr int UiBaseHeight = 480;

int scaledTextPadding() {
    const UniversalScaleState* scale = ResolutionScale::get();
    return scale ? scaleUiValueFromBase(
        TextPadding, PaddingBaseHeight, UiBaseHeight, *scale) : TextPadding;
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

bool isListbox(void* control) {
    DWORD vtable = 0;
    DWORD setExtent = 0;
    return safeReadDword(control, vtable) && vtable != 0 &&
        safeReadDword(reinterpret_cast<const void*>(vtable + 4), setExtent) &&
        setExtent == ListboxSetExtentAddress;
}

bool belongsToMainInterface(void* control) {
    DWORD owner = 0;
    DWORD mainInterface = 0;
    return safeReadDword(static_cast<char*>(control) + ControlOwnerOffset, owner) &&
        safeReadDword(
            reinterpret_cast<const void*>(MainInterfacePointerAddress),
            mainInterface) &&
        mainInterface != 0 && owner == mainInterface;
}

}

extern "C" void __cdecl applyScaledTextPadding(void* listbox) {
    if (!listbox) {
        return;
    }

    __try {
        if (!isListbox(listbox) || belongsToMainInterface(listbox)) {
            return;
        }

        typedef void(__thiscall *SetPaddingFn)(void*, int);
        reinterpret_cast<SetPaddingFn>(ListboxSetPaddingAddress)(
            listbox, scaledTextPadding());
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
