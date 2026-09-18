#include "font_scale_2x.h"
#include "../Common/ResolutionScale.h"

#include <cmath>

namespace FontScale2x {

namespace {

constexpr DWORD FontInfoOffset = 0x38;
constexpr DWORD FontHeightOffset = 0x04;
constexpr DWORD BaselineHeightOffset = 0x08;
constexpr DWORD TextureWidthOffset = 0x0C;
constexpr DWORD SpacingROffset = 0x10;
constexpr DWORD SpacingBOffset = 0x14;
constexpr DWORD GuiManagerPointerAddress = 0x007A39F4;
constexpr DWORD GuiStringTextObjectOffset = 0x50;
constexpr DWORD TextObjectRenderableOffset = 0x14;
constexpr DWORD RenderableTextureWrapperOffset = 0x18;
constexpr DWORD GetFontInfoAddress = 0x0041EFA0;
constexpr DWORD UpdateAllFontsAddress = 0x0040B420;
constexpr DWORD MaxFontsPerRefresh = 64;
constexpr float FontMetricPixelsPerUnit = 100.0f;
constexpr float FontMetricFloorEpsilon = 0.00001f;
constexpr DWORD FontMetricOffsets[] = {
    FontHeightOffset, BaselineHeightOffset, TextureWidthOffset,
    SpacingROffset, SpacingBOffset,
};

struct FontMetricState {
    void* fontInfo;
    float unrounded[sizeof(FontMetricOffsets) / sizeof(FontMetricOffsets[0])];
};

float g_appliedScale = 0.0f;
float g_refreshAdjustment = 1.0f;
bool g_refreshInProgress = false;
void* g_scaledFonts[MaxFontsPerRefresh] = {};
DWORD g_scaledFontCount = 0;
FontMetricState g_fontMetricStates[MaxFontsPerRefresh] = {};
DWORD g_fontMetricStateCount = 0;

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

bool safeReadFloat(const void* address, float& value) {
    __try {
        value = *reinterpret_cast<const float*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 0.0f;
        return false;
    }
}

bool hasSaneFontMetrics(char* fontInfo) {
    float fontHeight = 0.0f;
    float baselineHeight = 0.0f;
    float textureWidth = 0.0f;
    return fontInfo &&
        safeReadFloat(fontInfo + FontHeightOffset, fontHeight) &&
        safeReadFloat(fontInfo + BaselineHeightOffset, baselineHeight) &&
        safeReadFloat(fontInfo + TextureWidthOffset, textureWidth) &&
        fontHeight > 0.0f && fontHeight < 512.0f &&
        baselineHeight > 0.0f && baselineHeight < 512.0f &&
        textureWidth > 0.0f && textureWidth < 4096.0f;
}

FontMetricState* findFontMetricState(void* fontInfo) {
    for (DWORD i = 0; i < g_fontMetricStateCount; ++i) {
        if (g_fontMetricStates[i].fontInfo == fontInfo) {
            return &g_fontMetricStates[i];
        }
    }
    return nullptr;
}

FontMetricState* createFontMetricState(void* fontInfo) {
    if (g_fontMetricStateCount == MaxFontsPerRefresh) {
        return nullptr;
    }

    FontMetricState* state = &g_fontMetricStates[g_fontMetricStateCount++];
    *state = {};
    state->fontInfo = fontInfo;
    return state;
}

float floorFontMetricToPixel(float value) {
    return std::floor(
        value * FontMetricPixelsPerUnit + FontMetricFloorEpsilon) /
        FontMetricPixelsPerUnit;
}

void multiplyFontInfo(void* fontInfoPtr, float scale, bool loadedBase) {
    if (!fontInfoPtr || scale <= 0.0f) {
        return;
    }

    __try {
        char* fontInfo = static_cast<char*>(fontInfoPtr);
        if (!hasSaneFontMetrics(fontInfo)) {
            return;
        }

        FontMetricState* state = findFontMetricState(fontInfoPtr);
        const bool haveStoredMetrics = state != nullptr;
        if (!state) {
            state = createFontMetricState(fontInfoPtr);
        }

        for (DWORD i = 0;
             i < sizeof(FontMetricOffsets) / sizeof(FontMetricOffsets[0]);
             ++i) {
            float* metric = reinterpret_cast<float*>(
                fontInfo + FontMetricOffsets[i]);
            const float unrounded = loadedBase || !haveStoredMetrics ?
                *metric * scale : state->unrounded[i] * scale;
            if (state) {
                state->unrounded[i] = unrounded;
            }
            *metric = floorFontMetricToPixel(unrounded);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void* fontInfoFromTexture(void* texture) {
    DWORD fontInfo = 0;
    if (!texture ||
        !safeReadDword(static_cast<char*>(texture) + FontInfoOffset, fontInfo)) {
        return nullptr;
    }

    return reinterpret_cast<void*>(fontInfo);
}

void* fontInfoFromGuiString(void* guiString) {
    if (!guiString) {
        return nullptr;
    }

    DWORD textObject = 0;
    DWORD renderable = 0;
    DWORD textureWrapper = 0;
    if (!safeReadDword(
            static_cast<char*>(guiString) + GuiStringTextObjectOffset,
            textObject) ||
        !safeReadDword(
            reinterpret_cast<char*>(textObject) + TextObjectRenderableOffset,
            renderable) ||
        !safeReadDword(
            reinterpret_cast<char*>(renderable) + RenderableTextureWrapperOffset,
            textureWrapper) ||
        textureWrapper == 0) {
        return nullptr;
    }

    typedef void*(__thiscall *GetFontInfoFn)(void*);
    return reinterpret_cast<GetFontInfoFn>(GetFontInfoAddress)(
        reinterpret_cast<void*>(textureWrapper));
}

bool markFontForCurrentRefresh(void* fontInfo) {
    for (DWORD i = 0; i < g_scaledFontCount; ++i) {
        if (g_scaledFonts[i] == fontInfo) {
            return false;
        }
    }

    if (g_scaledFontCount == MaxFontsPerRefresh) {
        return false;
    }
    g_scaledFonts[g_scaledFontCount++] = fontInfo;
    return true;
}

}

void scaleLoadedTextureMetadata(void* texture) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!scale) {
        return;
    }

    const float currentScale = twoXScaleAdjustment(*scale);
    if (g_appliedScale <= 0.0f) {
        g_appliedScale = currentScale;
    }

    void* fontInfo = fontInfoFromTexture(texture);
    multiplyFontInfo(fontInfo, currentScale, true);

    // UpdateAllFonts can load a new font partway through a resolution refresh.
    // Its metadata is now at the new absolute scale, so later GUI strings that
    // share it must not also apply the old-to-new adjustment.
    if (g_refreshInProgress && fontInfo) {
        markFontForCurrentRefresh(fontInfo);
    }
}

void scaleResetGuiStringFont(void* guiString) {
    if (!g_refreshInProgress) {
        return;
    }

    void* fontInfo = fontInfoFromGuiString(guiString);
    if (fontInfo && markFontForCurrentRefresh(fontInfo)) {
        multiplyFontInfo(fontInfo, g_refreshAdjustment, false);
    }
}

void refreshResolutionDependentUi() {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!scale || g_appliedScale <= 0.0f) {
        return;
    }

    const float newScale = twoXScaleAdjustment(*scale);
    const float adjustment = newScale / g_appliedScale;
    if (adjustment == 1.0f) {
        return;
    }

    DWORD guiManager = 0;
    if (!safeReadDword(reinterpret_cast<const void*>(GuiManagerPointerAddress), guiManager) ||
        guiManager == 0) {
        return;
    }

    g_scaledFontCount = 0;
    g_refreshAdjustment = adjustment;
    g_refreshInProgress = true;

    typedef void(__thiscall *UpdateAllFontsFn)(void*);
    __try {
        reinterpret_cast<UpdateAllFontsFn>(UpdateAllFontsAddress)(
            reinterpret_cast<void*>(guiManager));
    }
    __finally {
        g_refreshInProgress = false;
    }
    g_appliedScale = newScale;
}

}
