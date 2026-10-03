#include "scaled_floating_target.h"
#include "../Common/ResolutionScale.h"
#include "GameAPI/GameVersion.h"

#include <cmath>

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

using GetFontInfoFn = void* (__thiscall*)(void*);
using GetSizeFn = void (__thiscall*)(void*, int*, int*);
using SetSizeFn = void (__thiscall*)(void*, int, int);

struct TargetNameApi {
    int nameLabel = -1, labelText = -1, controlExtent = -1;
    int textExtent = -1, guiString = -1, extentHeight = -1;
    int fontTexture = -1, pointSize = -1, fontHeight = -1;
    GetFontInfoFn getFontInfo = nullptr;
    GetSizeFn getSize = nullptr;
    SetSizeFn setSize = nullptr;
    bool ready = false;
} g_nameApi;

bool resolveNameApi() {
    if (g_nameApi.ready) return true;
    if (!GameVersion::IsInitialized() && !GameVersion::Initialize()) return false;
    try {
        TargetNameApi api;
        api.nameLabel = GameVersion::GetOffset("CSWGuiTargetActionMenu", "name_label");
        api.labelText = GameVersion::GetOffset("CSWGuiLabel", "text");
        api.controlExtent = GameVersion::GetOffset("CSWGuiControl", "extent");
        api.textExtent = GameVersion::GetOffset("CSWGuiText", "extent");
        api.guiString = GameVersion::GetOffset("CSWGuiText", "gui_string");
        api.extentHeight = GameVersion::GetOffset("CSWGuiExtent", "height");
        api.fontTexture = GameVersion::GetOffset("CAurGUIStringInternal", "font_texture");
        api.pointSize = GameVersion::GetOffset("CAurGUIStringInternal", "point_size");
        api.fontHeight = GameVersion::GetOffset("CAurFontInfo", "fontheight");
        api.getFontInfo = reinterpret_cast<GetFontInfoFn>(
            GameVersion::GetFunctionAddress("CAurTexture", "GetFontInfo"));
        api.getSize = reinterpret_cast<GetSizeFn>(
            GameVersion::GetFunctionAddress("CAurGUIStringInternal", "GetSizeBoundingRect_2"));
        api.setSize = reinterpret_cast<SetSizeFn>(
            GameVersion::GetFunctionAddress("CAurGUIStringInternal", "SetSizeBoundingRect_2"));
        if (api.nameLabel < 0 || api.labelText < 0 || api.controlExtent < 0 ||
            api.textExtent < 0 || api.guiString < 0 || api.extentHeight < 0 ||
            api.fontTexture < 0 || api.pointSize < 0 || api.fontHeight < 0 ||
            !api.getFontInfo || !api.getSize || !api.setSize)
            return false;
        api.ready = true;
        g_nameApi = api;
        return true;
    } catch (const GameVersionException&) {
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
    if (!owner || !resolveNameApi()) return;

    char* label = static_cast<char*>(owner) + g_nameApi.nameLabel;
    char* text = label + g_nameApi.labelText;
    char* renderable = *reinterpret_cast<char**>(text + g_nameApi.guiString);
    if (!renderable) return;

    void* texture = *reinterpret_cast<void**>(renderable + g_nameApi.fontTexture);
    if (!texture) return;
    char* info = static_cast<char*>(g_nameApi.getFontInfo(texture));
    if (!info) return;

    const float height = *reinterpret_cast<float*>(info + g_nameApi.fontHeight);
    const float point = *reinterpret_cast<float*>(renderable + g_nameApi.pointSize);
    const double pixels = static_cast<double>(height) * 100.0 * point;
    if (!std::isfinite(pixels) || pixels <= 0.0) return;
    const int required = static_cast<int>(std::ceil(pixels));

    int& labelHeight = *reinterpret_cast<int*>(
        label + g_nameApi.controlExtent + g_nameApi.extentHeight);
    int& textHeight = *reinterpret_cast<int*>(
        text + g_nameApi.textExtent + g_nameApi.extentHeight);
    int width = 0, currentHeight = 0;
    g_nameApi.getSize(renderable, &width, &currentHeight);
    if (labelHeight < required) labelHeight = required;
    if (textHeight < required) textHeight = required;
    if (currentHeight < required) g_nameApi.setSize(renderable, width, required);
}

}
