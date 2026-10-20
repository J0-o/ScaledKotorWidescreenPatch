#include "class_selection_layout_constants_test.h"
#include "../Common/ResolutionScale.h"

extern "C" void __cdecl prepareClassSelectionLayout(void* owner) {
    __try {
        ClassSelectionLayoutConstantsTest::prepareClassSelectionLayout(owner);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl finishClassSelectionLayout(void* ownerStackSlot) {
    __try {
        ClassSelectionLayoutConstantsTest::finishClassSelectionLayout(ownerStackSlot);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl patchInitialClassSelectionRects(void* owner) {
    __try {
        ClassSelectionLayoutConstantsTest::patchInitialClassSelectionRects(owner);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
