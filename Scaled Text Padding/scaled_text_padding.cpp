#include <windows.h>
#include "../Common/ResolutionScale.h"

namespace {

constexpr DWORD ListboxSetExtentAddress = 0x0041BF80;
constexpr DWORD ListboxSetPaddingAddress = 0x0041C190;
constexpr DWORD MainInterfacePointerAddress = 0x00833BB4;
constexpr DWORD AppManagerPointerAddress = 0x007A39FC;
constexpr DWORD ControlOwnerOffset = 0x34;
constexpr DWORD ClientAppOffset = 0x04;
constexpr DWORD ClientAppInternalOffset = 0x04;
constexpr DWORD GuiInGameOffset = 0x40;
constexpr DWORD AbilitiesMenuOffset = 0x18;
constexpr DWORD AbilitiesListboxOffset = 0x30DC;
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

extern "C" void __cdecl refreshResolutionDependentUi() {
    applyScaledTextPadding(currentAbilitiesListbox());
}
