#include "scaled_floating_target.h"

extern "C" void __cdecl scaleFloatingTargetControls(void* owner) {
    __try {
        FloatingTargetScale::scaleTargetControls(owner);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl correctFloatingTargetVerticalBounds(void* hud) {
    __try {
        FloatingTargetScale::correctTargetVerticalBounds(hud);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl roundFloatingTargetNameHeight(void* owner) {
    __try {
        FloatingTargetScale::roundTargetNameHeight(owner);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(reason);
    UNREFERENCED_PARAMETER(reserved);
    return TRUE;
}
