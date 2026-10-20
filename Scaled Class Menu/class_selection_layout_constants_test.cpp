#include "class_selection_layout_constants_test.h"
#include "../Common/ResolutionScale.h"

namespace ClassSelectionLayoutConstantsTest {

namespace {

constexpr DWORD ClassSelectionOwnerVtable = 0x00758020;
constexpr DWORD ClassSlotWrapperVtable = 0x00752E30;
constexpr DWORD SlotArrayOffset = 0x6C;
constexpr DWORD SlotStride = 0x25C;
constexpr int SlotCount = 6;
constexpr int BaseWidth = 800;
constexpr int BaseHeight = 600;
constexpr int UiBaseWidth = 640;
constexpr int UiBaseHeight = 480;
constexpr int BaseBoxTop = 128;
constexpr int BaseBoxWidth = 94;
constexpr int BaseBoxHeight = 240;
constexpr int BaseHoverGrow = 10;
constexpr int BaseBoxLefts[] = {
    77,
    190,
    299,
    406,
    516,
    625,
};
constexpr DWORD SlotRectOffsets[] = {
    0x000,
    0x06C,
    0x0E0,
    0x154,
    0x1C4,
    0x220,
};

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

bool safeReadRect(const void* address, Rect& rect) {
    __try {
        rect = *reinterpret_cast<const Rect*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        rect = {};
        return false;
    }
}

bool writeRect(void* address, const Rect& rect) {
    __try {
        *reinterpret_cast<Rect*>(address) = rect;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool isUsefulRootRect(const Rect& rect) {
    return rect.left == 0 &&
        rect.top == 0 &&
        rect.width >= 640 &&
        rect.height >= 480 &&
        rect.width <= 8192 &&
        rect.height <= 8192;
}

int clampHoverGrow(int value) {
    return value < 0 ? 0 : (value > BaseHoverGrow ? BaseHoverGrow : value);
}

Rect nativeSlotRect(int slotIndex, int grow) {
    grow = clampHoverGrow(grow);
    return {
        BaseBoxLefts[slotIndex] - grow,
        BaseBoxTop - grow,
        BaseBoxWidth + (grow * 2),
        BaseBoxHeight + (grow * 2),
    };
}

Rect scaledSlotRect(int slotIndex, int grow, const Rect& root,
                    const UniversalScaleState& scale) {
    grow = clampHoverGrow(grow);
    const int offsetX = (root.width - scale.uiWidth) / 2;
    const int scaledGrow = scaleUiValueFromBase(
        grow, BaseHeight, UiBaseHeight, scale);
    const int normalHeight = scaleUiValueFromBase(
        BaseBoxHeight, BaseHeight, UiBaseHeight, scale);

    return {
        offsetX + scaleUiValueFromBase(
            BaseBoxLefts[slotIndex] - grow,
            BaseWidth, UiBaseWidth, scale),
        (root.height - normalHeight) / 2 - scaledGrow,
        scaleUiValueFromBase(
            BaseBoxWidth + (grow * 2), BaseWidth, UiBaseWidth, scale),
        scaleUiValueFromBase(
            BaseBoxHeight + (grow * 2), BaseHeight, UiBaseHeight, scale),
    };
}

bool validateOwner(char* owner, Rect& root) {
    if (!owner) {
        return false;
    }
    DWORD ownerVtable = 0;
    if (!safeReadDword(owner, ownerVtable) || ownerVtable != ClassSelectionOwnerVtable) {
        return false;
    }
    return safeReadRect(owner + sizeof(DWORD), root) && isUsefulRootRect(root);
}

void writeSlotRects(char* slot, const Rect& rect) {
    for (int rectIndex = 0;
         rectIndex < static_cast<int>(sizeof(SlotRectOffsets) /
                                      sizeof(SlotRectOffsets[0]));
         ++rectIndex) {
        writeRect(slot + SlotRectOffsets[rectIndex] + sizeof(DWORD), rect);
    }
}

int displayedHoverGrow(const Rect& current, int slotIndex, const Rect& root,
                       const UniversalScaleState& scale) {
    const Rect normal = scaledSlotRect(slotIndex, 0, root, scale);
    return clampHoverGrow(unscaleUiValueToBase(
        normal.top - current.top, BaseHeight, UiBaseHeight, scale));
}

void scaleNativeSlotRects(char* owner, const Rect& root,
                          const UniversalScaleState& scale) {
    for (int slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
        char* slot = owner + SlotArrayOffset + (SlotStride * slotIndex);
        Rect native = {};
        if (!safeReadRect(slot + sizeof(DWORD), native)) {
            continue;
        }
        const int grow = clampHoverGrow(BaseBoxTop - native.top);
        writeSlotRects(slot, scaledSlotRect(slotIndex, grow, root, scale));
    }
}

}

void prepareClassSelectionLayout(void* ownerPtr) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!scale) {
        return;
    }

    char* owner = static_cast<char*>(ownerPtr);
    Rect root = {};
    if (!validateOwner(owner, root)) {
        return;
    }

    for (int slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
        char* slot = owner + SlotArrayOffset + (SlotStride * slotIndex);
        Rect displayed = {};
        if (!safeReadRect(slot + sizeof(DWORD), displayed)) {
            continue;
        }
        const int grow = displayedHoverGrow(displayed, slotIndex, root, *scale);
        writeSlotRects(slot, nativeSlotRect(slotIndex, grow));
    }
}

void finishClassSelectionLayout(void* ownerStackSlot) {
    const UniversalScaleState* scale = ResolutionScale::get();
    DWORD ownerValue = 0;
    if (!scale || !safeReadDword(ownerStackSlot, ownerValue)) {
        return;
    }

    char* owner = reinterpret_cast<char*>(ownerValue);
    Rect root = {};
    if (!validateOwner(owner, root)) {
        return;
    }
    scaleNativeSlotRects(owner, root, *scale);
}

void patchInitialClassSelectionRects(void* ownerPtr) {
    const UniversalScaleState* scale = ResolutionScale::get();
    char* owner = static_cast<char*>(ownerPtr);
    Rect root = {};
    if (!scale || !validateOwner(owner, root)) {
        return;
    }

    for (int slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
        char* slot = owner + SlotArrayOffset + (SlotStride * slotIndex);
        char* wrapper = slot + 0x1C4;
        DWORD wrapperVtable = 0;
        if (safeReadDword(wrapper, wrapperVtable) &&
            wrapperVtable == ClassSlotWrapperVtable) {
            writeSlotRects(slot, scaledSlotRect(slotIndex, 0, root, *scale));
        }
    }
}

}
