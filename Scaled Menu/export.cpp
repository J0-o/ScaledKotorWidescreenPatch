#include "scaled_menu.h"
#include "../Common/ResolutionScale.h"

extern "C" void __cdecl scaleMenuPanelTree(void* panel) {
    __try {
        MenuScale::scaleMenuPanelTree(panel);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl fixAlignmentSliderThumb(void* slider) {
    __try {
        MenuScale::fixAlignmentSliderThumb(slider);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl scalePazaakGameCards(void* pazaakGame) {
    __try {
        MenuScale::scalePazaakGameCards(pazaakGame);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl refreshResolutionDependentUi() {
    __try {
        MenuScale::refreshMenuPanelTrees();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
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
