#include "scaled_hud.h"
#include "../Scaled Kotor/resolution_scale.h"

namespace HudScale {

namespace {

struct Rect {
    int left;
    int top;
    int width;
    int height;
};

struct OffsetRect {
    DWORD offset;
    Rect rect;
};

constexpr int BaseWidth = 800;
constexpr int BaseHeight = 600;
constexpr int UiBaseWidth = 640;
constexpr int UiBaseHeight = 480;

constexpr DWORD MinimapOffsets[] = {
    0x5CC0, // map_border_label
    0x5E00, // map_label
    0x5F40, // arrow_label
    0x6098  // mini_map_button
};

constexpr DWORD HudControlOffsets[] = {
    0x1BC8, 0x1D08, 0x1E48,             // moulding_labels
    0x6E94,                             // clear button
    0x735C, 0x749C,                     // combat mode message
    0xB00C, 0xB1D0, 0xB394, 0xB558,     // menu buttons
    0xB71C, 0xB8E0, 0xBAA4, 0xBC68,
    0xBE2C, 0xBF6C,                     // menu background/arrow margin
    0xC0AC, 0xC360, 0xC614              // toggles
};

constexpr DWORD ActionDescriptionControlOffsets[] = {
    0xA1D4, 0xA314
};
constexpr DWORD ActionDescriptionBottomOffset = 0xA454;

constexpr DWORD QueuedActionMarkerOffsets[] = {
    0x625C, 0x639C, 0x64DC, 0x661C
};

constexpr DWORD QueuedActionMarkerControlOffsets[] = {
    0x00, 0x5C, 0xD0
};

constexpr DWORD CenteredHudControlOffsets[] = {
    0x675C, 0x689C, 0x69DC,
    0x6CD0, 0x7058
};

constexpr DWORD ActionMenuControlOffsets[] = {
    0x00, 0x1C4, 0x388, 0x54C
};

constexpr DWORD CharacterControlOffsets[] = {
    0x28, 0x168, 0x2A8, 0x3E8, 0x528,
    0x668, 0x7A8, 0x8F8, 0xA48, 0xB88, 0xCC8
};

constexpr DWORD StatusSummaryBase = 0xA460;
constexpr DWORD StatusSummaryStride = 0x14C;
constexpr OffsetRect StatusSummaryRects[] = {
    { 0 * StatusSummaryStride, { 130, 4,  32, 32 } }, // journal
    { 1 * StatusSummaryStride, { 163, 4,  32, 32 } }, // credits
    { 2 * StatusSummaryStride, { 130, 39, 32, 32 } }, // plot XP
    { 3 * StatusSummaryStride, { 163, 39, 32, 32 } }, // stealth XP
    { 4 * StatusSummaryStride, { 130, 39, 32, 32 } }, // dark-side shift
    { 5 * StatusSummaryStride, { 163, 39, 32, 32 } }, // light-side shift
    { 7 * StatusSummaryStride, { 130, 74, 32, 32 } }, // item received
    { 8 * StatusSummaryStride, { 163, 74, 32, 32 } }  // item lost
};

constexpr Rect AreaTransitionOwnerRect = { 150, 100, 500, 103 };
constexpr OffsetRect AreaTransitionControlRects[] = {
    { 0x64,  { 0,   0,  500, 40 } }, // text background
    { 0x1A4, { 218, 39, 63,  63 } }, // icon
    { 0x2E4, { 33,  11, 435, 25 } }  // description
};

bool writeMemory(void* address, const void* replacement, size_t size) {
    __try {
        DWORD oldProtect = 0;
        if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        CopyMemory(address, replacement, size);

        DWORD ignored = 0;
        VirtualProtect(address, size, oldProtect, &ignored);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safeReadInt(const void* address, int& value) {
    __try {
        value = *reinterpret_cast<const int*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 0;
        return false;
    }
}

bool safeWriteInt(void* address, int value) {
    return writeMemory(address, &value, sizeof(value));
}

bool safeReadRect(const void* address, Rect& value) {
    __try {
        value = *reinterpret_cast<const Rect*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        value = {};
        return false;
    }
}

int scaleX(int value, const UniversalScaleState& scale) {
    return scaleHudValueFromBase(value, BaseWidth, UiBaseWidth, scale);
}

int scaleY(int value, const UniversalScaleState& scale) {
    return scaleHudValueFromBase(value, BaseHeight, UiBaseHeight, scale);
}

int hudCanvasLeft(const UniversalScaleState& scale) {
    return (scale.screenWidth - scale.hudWidth) / 2;
}

bool hasUsefulRect(const Rect& rect) {
    return rect.width > 0 && rect.height > 0 &&
        rect.width < 4096 && rect.height < 4096;
}

bool isBaseRect(const Rect& rect) {
    return rect.left >= 0 &&
        rect.top >= 0 &&
        rect.left + rect.width <= BaseWidth &&
        rect.top + rect.height <= BaseHeight;
}

bool getBaseRectForControl(char* control, Rect& rect) {
    if (!control) {
        return false;
    }

    return safeReadRect(control + 0x04, rect) &&
        hasUsefulRect(rect) &&
        isBaseRect(rect);
}

bool isMinimapControl(DWORD offset) {
    for (DWORD minimapOffset : MinimapOffsets) {
        if (offset == minimapOffset) {
            return true;
        }
    }

    return false;
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

    writeMemory(control + 0x04, &rect, sizeof(rect));
}

Rect scaleRect(const Rect& original, const UniversalScaleState& scale) {
    Rect scaled = {
        scaleX(original.left, scale),
        scaleY(original.top, scale),
        scaleX(original.width, scale),
        scaleY(original.height, scale)
    };

    if (original.left >= BaseWidth / 2) {
        const int rightGap = BaseWidth - (original.left + original.width);
        scaled.left = scale.screenWidth - scaleX(rightGap, scale) - scaled.width;
    }

    if (original.top >= BaseHeight / 2) {
        const int bottomGap = BaseHeight - (original.top + original.height);
        scaled.top = scale.screenHeight - scaleY(bottomGap, scale) - scaled.height;
    }

    return scaled;
}

Rect scaleQueuedActionMarkerRect(const Rect& original, const UniversalScaleState& scale) {
    Rect scaled = {
        hudCanvasLeft(scale) + scaleX(original.left, scale),
        scaleY(original.top, scale),
        scaleX(original.width, scale),
        scaleY(original.height, scale)
    };

    if (original.top >= BaseHeight / 2) {
        const int bottomGap = BaseHeight - (original.top + original.height);
        scaled.top = scale.screenHeight - scaleY(bottomGap, scale) - scaled.height;
    }

    return scaled;
}

int scaleVerticalCoordinate(int value, const UniversalScaleState& scale) {
    int scaled = scaleY(value, scale);
    if (value >= BaseHeight / 2) {
        scaled = scale.screenHeight - scaleY(BaseHeight - value, scale);
    }

    return scaled;
}

void scaleControl(char* base, DWORD offset, const UniversalScaleState& scale) {
    if (!base || isMinimapControl(offset)) {
        return;
    }

    char* control = base + offset;
    Rect original = {};
    if (!getBaseRectForControl(control, original)) {
        return;
    }

    callControlSetRect(control, scaleRect(original, scale));
}

void scaleVolatileControl(char* base, DWORD offset, const UniversalScaleState& scale) {
    if (!base) {
        return;
    }

    char* control = base + offset;
    Rect original = {};
    if (!safeReadRect(control + 0x04, original) || !hasUsefulRect(original)) {
        return;
    }

    Rect scaled = original;
    if (original.left >= 0 && original.left + original.width <= BaseWidth) {
        scaled.left = scaleX(original.left, scale);
        scaled.width = scaleX(original.width, scale);
        if (original.left >= BaseWidth / 2) {
            const int rightGap = BaseWidth - (original.left + original.width);
            scaled.left = scale.screenWidth - scaleX(rightGap, scale) - scaled.width;
        }
    }

    if (original.top >= 0 && original.top + original.height <= BaseHeight) {
        scaled.top = scaleY(original.top, scale);
        scaled.height = scaleY(original.height, scale);
        if (original.top >= BaseHeight / 2) {
            const int bottomGap = BaseHeight - (original.top + original.height);
            scaled.top = scale.screenHeight - scaleY(bottomGap, scale) - scaled.height;
        }
    }
    else if (original.top > BaseHeight && original.height <= BaseHeight / 4) {
        const int bottom = original.top + original.height;
        scaled.height = scaleY(original.height, scale);
        scaled.top = bottom - scaled.height;
    }

    if (scaled.left != original.left ||
        scaled.top != original.top ||
        scaled.width != original.width ||
        scaled.height != original.height) {
        callControlSetRect(control, scaled);
    }
}

void scaleActionDescriptionBottom(char* base, const UniversalScaleState& scale) {
    if (!base) {
        return;
    }

    int bottom = 0;
    if (!safeReadInt(base + ActionDescriptionBottomOffset, bottom) ||
        bottom <= 0 ||
        bottom > BaseHeight) {
        return;
    }

    const int scaled = scaleVerticalCoordinate(bottom, scale);
    if (scaled != bottom) {
        safeWriteInt(base + ActionDescriptionBottomOffset, scaled);
    }
}

void scaleActionDescriptionControls(char* base, const UniversalScaleState& scale) {
    scaleActionDescriptionBottom(base, scale);
    for (DWORD offset : ActionDescriptionControlOffsets) {
        scaleVolatileControl(base, offset, scale);
    }
}

void scaleQueuedActionMarkerControl(char* control, const UniversalScaleState& scale,
                                    bool useVirtualSetRect) {
    if (!control) {
        return;
    }

    Rect original = {};
    if (!getBaseRectForControl(control, original)) {
        return;
    }

    const Rect scaled = scaleQueuedActionMarkerRect(original, scale);
    if (useVirtualSetRect) {
        callControlSetRect(control, scaled);
    }
    else {
        writeMemory(control + 0x04, &scaled, sizeof(scaled));
    }
}

void scaleQueuedActionMarkers(char* base, const UniversalScaleState& scale) {
    if (!base) {
        return;
    }

    for (DWORD markerOffset : QueuedActionMarkerOffsets) {
        char* marker = base + markerOffset;
        for (DWORD controlOffset : QueuedActionMarkerControlOffsets) {
            scaleQueuedActionMarkerControl(marker + controlOffset, scale, true);
        }

        int drawable = 0;
        if (safeReadInt(marker + 0xE4, drawable) && drawable != 0) {
            scaleQueuedActionMarkerControl(reinterpret_cast<char*>(drawable), scale, false);
        }
    }
}

void scaleCenteredHudControls(char* base, const UniversalScaleState& scale) {
    if (!base) {
        return;
    }

    for (DWORD offset : CenteredHudControlOffsets) {
        scaleQueuedActionMarkerControl(base + offset, scale, true);
    }
}

void scaleActionMenu(char* base, DWORD offset, const UniversalScaleState& scale) {
    for (DWORD childOffset : ActionMenuControlOffsets) {
        scaleControl(base, offset + childOffset, scale);
    }
}

void scaleActionQueue(char* base, const UniversalScaleState& scale) {
    constexpr DWORD ActionQueueBase = 0x772C;
    constexpr DWORD ActionQueueStride = 0x71C;
    for (int i = 0; i < 6; ++i) {
        scaleActionMenu(base, ActionQueueBase + (ActionQueueStride * i), scale);
    }
}

void scaleCharacter(char* base, DWORD offset, const UniversalScaleState& scale) {
    for (DWORD childOffset : CharacterControlOffsets) {
        scaleControl(base, offset + childOffset, scale);
    }
}

void scaleStatusSummaries(char* base, const UniversalScaleState& scale) {
    if (!base) {
        return;
    }

    for (const OffsetRect& summary : StatusSummaryRects) {
        const Rect scaled = {
            scaleX(summary.rect.left, scale),
            scaleY(summary.rect.top, scale),
            scaleX(summary.rect.width, scale),
            scaleY(summary.rect.height, scale)
        };
        callControlSetRect(base + StatusSummaryBase + summary.offset, scaled);
    }
}

void scaleRootPanel(char* base, const UniversalScaleState& scale) {
    Rect root = { 0, 0, scale.screenWidth, scale.screenHeight };
    callControlSetRect(base, root);
}

void scaleAreaTransitionOwner(char* owner, const UniversalScaleState& scale) {
    if (!owner) {
        return;
    }

    for (const OffsetRect& control : AreaTransitionControlRects) {
        const Rect scaled = {
            scaleX(control.rect.left, scale),
            scaleY(control.rect.top, scale),
            scaleX(control.rect.width, scale),
            scaleY(control.rect.height, scale)
        };
        callControlSetRect(owner + control.offset, scaled);
    }

    const Rect scaledOwner = {
        (scale.hudWidth - scaleX(AreaTransitionOwnerRect.width, scale)) / 2,
        scaleY(AreaTransitionOwnerRect.top, scale),
        scaleX(AreaTransitionOwnerRect.width, scale),
        scaleY(AreaTransitionOwnerRect.height, scale)
    };
    writeMemory(owner + 0x04, &scaledOwner, sizeof(scaledOwner));
}

}

void scaleHudControls(void* hud) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!hud || !scale || isIdentityHudScale(*scale)) {
        return;
    }

    char* base = static_cast<char*>(hud);

    scaleActionDescriptionBottom(base, *scale);

    scaleRootPanel(base, *scale);

    scaleActionQueue(base, *scale);
    scaleQueuedActionMarkers(base, *scale);
    scaleCenteredHudControls(base, *scale);

    for (DWORD offset : HudControlOffsets) {
        scaleControl(base, offset, *scale);
    }

    constexpr DWORD PartyBase = 0x1F88;
    constexpr DWORD PartyStride = 0xEA8;
    for (int i = 0; i < 3; ++i) {
        scaleCharacter(base, PartyBase + (PartyStride * i), *scale);
    }
    scaleCharacter(base, 0x4B80, *scale); // main_character

    scaleStatusSummaries(base, *scale);
}

void scaleHudActionDescription(void* hud) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!hud || !scale || isIdentityHudScale(*scale)) {
        return;
    }

    scaleActionDescriptionControls(static_cast<char*>(hud), *scale);
}

void scaleAreaTransitionPrompt(void* prompt) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!prompt || !scale || isIdentityHudScale(*scale)) {
        return;
    }

    scaleAreaTransitionOwner(static_cast<char*>(prompt), *scale);
}

}
