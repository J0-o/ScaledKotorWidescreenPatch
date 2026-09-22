#include "container_popup_scale_test.h"
#include "../Common/ResolutionScale.h"

namespace ContainerPopupScaleTest {

namespace {

constexpr DWORD ContainerVtable = 0x007567E0;
constexpr DWORD MaxSnapshotChildren = 64;

struct ControlSnapshot {
    Rect rect;
    bool valid;
};

struct PanelSnapshot {
    char* owner;
    Rect root;
    ControlSnapshot children[MaxSnapshotChildren];
    DWORD childCount;
};

PanelSnapshot livePanel = {};

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

bool hasUsefulRect(const Rect& rect) {
    return rect.width > 0 && rect.height > 0 &&
        rect.width < 8192 && rect.height < 8192;
}

int scaleContainerValue(int value, const UniversalScaleState& scale) {
    if (!scale.contentScalingEnabled ||
        scale.contentScaleNumerator <= 0 ||
        scale.contentScaleDenominator <= 0) {
        return value;
    }

    return divideRoundedNearest(
        static_cast<long long>(value) * scale.contentScaleNumerator * 8,
        static_cast<long long>(scale.contentScaleDenominator) * 9);
}

Rect scaledChildRect(const Rect& rect, const UniversalScaleState& scale) {
    return {
        scaleContainerValue(rect.left, scale),
        scaleContainerValue(rect.top, scale),
        scaleContainerValue(rect.width, scale),
        scaleContainerValue(rect.height, scale),
    };
}

Rect scaledRootRect(const Rect& rect, const UniversalScaleState& scale) {
    return scaledChildRect(rect, scale);
}

void callControlSetRect(char* control, const Rect& rect) {
    if (!control) {
        return;
    }

    __try {
        DWORD vtable = *reinterpret_cast<DWORD*>(control);
        DWORD setRect = *reinterpret_cast<DWORD*>(vtable + 4);
        if (setRect != 0) {
            typedef void(__thiscall *SetRectFn)(void*, const Rect*);
            reinterpret_cast<SetRectFn>(setRect)(control, &rect);
            return;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    __try {
        *reinterpret_cast<Rect*>(control + sizeof(DWORD)) = rect;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool capturePanel(char* owner) {
    DWORD childrenData = 0;
    DWORD childrenSize = 0;
    if (!safeReadDword(owner + 0x20, childrenData) ||
        !safeReadDword(owner + 0x24, childrenSize) ||
        childrenSize > MaxSnapshotChildren ||
        (childrenSize != 0 && childrenData == 0) ||
        !hasUsefulRect(*reinterpret_cast<Rect*>(owner + sizeof(DWORD)))) {
        return false;
    }

    livePanel = {};
    livePanel.owner = owner;
    livePanel.root = *reinterpret_cast<Rect*>(owner + sizeof(DWORD));
    livePanel.childCount = childrenSize;
    for (DWORD i = 0; i < childrenSize; ++i) {
        DWORD childValue = 0;
        if (!safeReadDword(reinterpret_cast<void*>(childrenData + i * sizeof(DWORD)),
                           childValue) || childValue == 0) {
            continue;
        }
        char* child = reinterpret_cast<char*>(childValue);
        Rect rect = *reinterpret_cast<Rect*>(child + sizeof(DWORD));
        if (hasUsefulRect(rect)) {
            livePanel.children[i].rect = rect;
            livePanel.children[i].valid = true;
        }
    }
    return true;
}

void applyPanel(const UniversalScaleState& scale) {
    DWORD vtable = 0;
    if (!livePanel.owner ||
        !safeReadDword(livePanel.owner, vtable) || vtable != ContainerVtable) {
        livePanel = {};
        return;
    }

    DWORD childrenData = 0;
    DWORD childrenSize = 0;
    if (!safeReadDword(livePanel.owner + 0x20, childrenData) ||
        !safeReadDword(livePanel.owner + 0x24, childrenSize) ||
        childrenSize != livePanel.childCount ||
        (childrenSize != 0 && childrenData == 0)) {
        return;
    }

    for (DWORD i = 0; i < livePanel.childCount; ++i) {
        const ControlSnapshot& child = livePanel.children[i];
        if (!child.valid) {
            continue;
        }

        DWORD childAddress = 0;
        if (safeReadDword(
                reinterpret_cast<const void*>(childrenData + (i * sizeof(DWORD))),
                childAddress) &&
            childAddress != 0) {
            callControlSetRect(reinterpret_cast<char*>(childAddress),
                scaledChildRect(child.rect, scale));
        }
    }
    callControlSetRect(livePanel.owner, scaledRootRect(livePanel.root, scale));
}

}

void scaleContainerPanel(void* ownerPtr) {
    char* owner = static_cast<char*>(ownerPtr);
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!owner || !scale) {
        return;
    }

    DWORD vtable = 0;
    if (!safeReadDword(owner, vtable) || vtable != ContainerVtable) {
        return;
    }

    if (!capturePanel(owner)) {
        return;
    }

    applyPanel(*scale);
}

void refreshContainerPanel() {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (scale) {
        applyPanel(*scale);
    }
}

}
