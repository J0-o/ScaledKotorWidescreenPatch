#include "scaled_floating_target.h"
#include "../Common/ResolutionScale.h"

namespace FloatingTargetScale {

namespace {

constexpr int BaseWidth = 800;
constexpr int BaseHeight = 600;
constexpr int UiBaseWidth = 640;
constexpr int UiBaseHeight = 480;
constexpr DWORD TargetMenuBase = 0x54;
constexpr DWORD TargetMenuStride = 0x71C;
constexpr DWORD PauseControlOffset = 0xC0AC;
constexpr DWORD TargetClampHeightOffset = 0x1684;
constexpr DWORD TargetNameControlOffset = 0x15CC;
constexpr DWORD TextRenderableOffset = 0xE4;
constexpr int TargetNameBaseLineHeight = 16;

constexpr DWORD TargetMenuControlOffsets[] = {
    0x000, // BTN_TARGETn
    0x1C4, // LBL_TARGETn
    0x388, // BTN_TARGETUPn
    0x54C  // BTN_TARGETDOWNn
};

constexpr DWORD TargetLabelControlOffsets[] = {
    0x15CC, // LBL_NAME
    0x170C, // LBL_NAMEBG
    0x184C, // LBL_HEALTHBG
    0x198C  // PB_HEALTH
};

constexpr Rect FloatingTargetActionRects[] = {
    { 43, 35, 35, 59 },
    { 45, 49, 32, 32 },
    { 43, 36, 35, 12 },
    { 44, 80, 35, 12 },
    { 83, 35, 35, 59 },
    { 85, 49, 32, 32 },
    { 83, 36, 35, 12 },
    { 84, 80, 35, 12 },
    { 122, 35, 35, 59 },
    { 124, 49, 32, 32 },
    { 122, 36, 35, 12 },
    { 123, 80, 35, 12 },
    { 0, 0, 200, 26 },
    { 0, 27, 200, 6 }
};

constexpr Rect PauseRect = { 6, 465, 35, 35 };

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

bool isRect(const Rect& rect, const Rect& expected) {
    return rect.left == expected.left &&
        rect.top == expected.top &&
        rect.width == expected.width &&
        rect.height == expected.height;
}

void callControlSetRect(char* control, const Rect& rect) {
    __try {
        const DWORD vtable = *reinterpret_cast<const DWORD*>(control);
        const DWORD setRect = *reinterpret_cast<const DWORD*>(vtable + 4);
        if (setRect != 0) {
            typedef void(__thiscall *SetRectFn)(void*, const Rect*);
            reinterpret_cast<SetRectFn>(setRect)(control, &rect);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void scaleKnownControl(char* control, const UniversalScaleState& scale) {
    Rect rect = {};
    if (!safeReadRect(control + 0x04, rect)) {
        return;
    }

    for (const Rect& expected : FloatingTargetActionRects) {
        if (!isRect(rect, expected)) {
            continue;
        }

        rect.left = scaleUiValueFromBase(rect.left, BaseWidth, UiBaseWidth, scale);
        rect.top = scaleUiValueFromBase(rect.top, BaseHeight, UiBaseHeight, scale);
        rect.width = scaleUiValueFromBase(rect.width, BaseWidth, UiBaseWidth, scale);
        rect.height = scaleUiValueFromBase(rect.height, BaseHeight, UiBaseHeight, scale);
        callControlSetRect(control, rect);
        return;
    }
}

}

void scaleTargetControls(void* owner) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!owner || !scale ||
        (scale->uiWidth == BaseWidth && scale->uiHeight == BaseHeight)) {
        return;
    }

    char* base = static_cast<char*>(owner);
    for (DWORD offset : TargetLabelControlOffsets) {
        scaleKnownControl(base + offset, *scale);
    }

    for (int menuIndex = 0; menuIndex < 3; ++menuIndex) {
        char* menu = base + TargetMenuBase + (TargetMenuStride * menuIndex);
        for (DWORD offset : TargetMenuControlOffsets) {
            scaleKnownControl(menu + offset, *scale);
        }
    }
}

void correctTargetVerticalBounds(void* hud) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!hud || !scale ||
        (scale->uiWidth == BaseWidth && scale->uiHeight == BaseHeight)) {
        return;
    }

    char* base = static_cast<char*>(hud);
    Rect pause = {};
    if (!safeReadRect(base + PauseControlOffset + 0x04, pause) ||
        !isRect(pause, PauseRect)) {
        return;
    }

    int clampHeight = 0;
    if (!safeReadInt(base + TargetClampHeightOffset, clampHeight)) {
        return;
    }

    clampHeight += scaleUiValueFromBase(
        PauseRect.top, BaseHeight, UiBaseHeight, *scale) - PauseRect.top;
    *reinterpret_cast<int*>(base + TargetClampHeightOffset) = clampHeight;
}

void roundTargetNameHeight(void* owner) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!owner || !scale || !scale->contentScalingEnabled ||
        scale->contentScaleNumerator <= 0 ||
        scale->contentScaleDenominator <= 0) {
        return;
    }

    const long long numerator = static_cast<long long>(TargetNameBaseLineHeight) *
        scale->contentScaleNumerator * 4;
    const long long denominator =
        static_cast<long long>(scale->contentScaleDenominator) * 9;
    const int roundedHeight = static_cast<int>(numerator / denominator);

    char* label = static_cast<char*>(owner) + TargetNameControlOffset;
    Rect rect = {};
    if (!safeReadRect(label + 0x04, rect) || rect.height >= roundedHeight) {
        return;
    }

    __try {
        char* renderable = *reinterpret_cast<char**>(label + TextRenderableOffset);
        rect.height = roundedHeight;
        *reinterpret_cast<Rect*>(label + 0x04) = rect;
        if (renderable) {
            *reinterpret_cast<int*>(renderable + 0x10) = roundedHeight;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}
