// HUD minimap scaling implementation.
#include "scaled_minimap.h"
#include "../Scaled Kotor/resolution_scale.h"

namespace HudMinimapScale {

namespace {

constexpr int BaseHeight = 600;
constexpr int BaseWidth = 800;
constexpr int UiBaseHeight = 480;
constexpr float AreaMapViewportWidth = 440.0f;
constexpr float AreaMapViewportHeight = 256.0f;
constexpr DWORD AreaMapViewportWidthAddress = 0x00747748;
constexpr DWORD AreaMapViewportHeightAddress = 0x007455D4;
constexpr int MinimapViewportSize = 120;
constexpr int MinimapTextureSize = 512;
constexpr int MinimapAtlasHalfHeight = 256;
constexpr DWORD ViewportIndexAddress = 0x007B9460;
constexpr DWORD ViewportWidthAddress = 0x007B946C;
constexpr DWORD ViewportHeightAddress = 0x007B946E;
constexpr int ViewportEntryStride = 10;
constexpr int BorderLeft = -2;
constexpr int BorderTop = -3;
constexpr int BorderWidth = 136;
constexpr int BorderHeight = 137;
constexpr int MapViewLeft = 6;
constexpr int MapViewTop = 6;
constexpr int FogViewLeft = 8;
constexpr int FogViewTop = 8;
constexpr int FogViewSize = 118;
constexpr int ArrowLeft = 47;
constexpr int ArrowTop = 49;
constexpr int ArrowSize = 32;

float g_areaMapViewportWidthBeforeGrid = AreaMapViewportWidth;
float g_areaMapViewportHeightBeforeGrid = AreaMapViewportHeight;
bool g_haveAreaMapViewportBeforeGrid = false;

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

bool safeReadShort(const void* address, short& value) {
    __try {
        value = *reinterpret_cast<const short*>(address);
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

void restoreAreaMapViewportAfterGrid() {
    if (!g_haveAreaMapViewportBeforeGrid) {
        return;
    }

    writeMemory(reinterpret_cast<void*>(AreaMapViewportWidthAddress),
        &g_areaMapViewportWidthBeforeGrid,
        sizeof(g_areaMapViewportWidthBeforeGrid));
    writeMemory(reinterpret_cast<void*>(AreaMapViewportHeightAddress),
        &g_areaMapViewportHeightBeforeGrid,
        sizeof(g_areaMapViewportHeightBeforeGrid));
    g_haveAreaMapViewportBeforeGrid = false;
}

int scaleMinimapValue(int value, const UniversalScaleState& scale) {
    return scaleHudValueFromBase(value, BaseHeight, UiBaseHeight, scale);
}

bool isNativeScale(const UniversalScaleState& scale) {
    return isIdentityHudScale(scale);
}

void callControlSetRect(char* control, const Rect& rect) {
    if (!control) {
        return;
    }

    __try {
        DWORD vtable = 0;
        DWORD setRect = 0;
        if (safeReadDword(control, vtable) &&
            safeReadDword(reinterpret_cast<const void*>(vtable + 4), setRect) &&
            setRect != 0) {
            typedef void(__thiscall *SetRectFn)(void*, const Rect*);
            reinterpret_cast<SetRectFn>(setRect)(control, &rect);
            return;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    writeMemory(control + 0x04, &rect, sizeof(rect));
}

void setControlRect(char* control, const Rect& rect) {
    if (!control) {
        return;
    }

    callControlSetRect(control, rect);
}

bool isRect(const Rect& rect, int left, int top, int width, int height) {
    return rect.left == left &&
        rect.top == top &&
        rect.width == width &&
        rect.height == height;
}

bool scaleKnownHudMinimapRect(Rect* rect, const UniversalScaleState& scale) {
    const Rect original = *rect;

    if (isRect(original, BorderLeft, BorderTop, BorderWidth, BorderHeight)) {
        rect->left = scaleMinimapValue(BorderLeft, scale);
        rect->top = scaleMinimapValue(BorderTop, scale);
        rect->width = scaleMinimapValue(BorderWidth, scale);
        rect->height = scaleMinimapValue(BorderHeight, scale);
        return true;
    }

    if (isRect(original, MapViewLeft, MapViewTop, MinimapTextureSize, MinimapTextureSize)) {
        rect->left = scaleMinimapValue(MapViewLeft, scale);
        rect->top = scaleMinimapValue(MapViewTop, scale);
        rect->width = MinimapTextureSize;
        rect->height = MinimapAtlasHalfHeight;
        return true;
    }

    if (isRect(original, MapViewLeft, MapViewTop, MinimapViewportSize, MinimapViewportSize)) {
        rect->left = scaleMinimapValue(MapViewLeft, scale);
        rect->top = scaleMinimapValue(MapViewTop, scale);
        rect->width = scaleMinimapValue(MinimapViewportSize, scale);
        rect->height = scaleMinimapValue(MinimapViewportSize, scale);
        return true;
    }

    if (isRect(original, FogViewLeft, FogViewTop, FogViewSize, FogViewSize)) {
        rect->left = scaleMinimapValue(FogViewLeft, scale);
        rect->top = scaleMinimapValue(FogViewTop, scale);
        rect->width = scaleMinimapValue(FogViewSize, scale);
        rect->height = scaleMinimapValue(FogViewSize, scale);
        return true;
    }

    if (isRect(original, ArrowLeft, ArrowTop, ArrowSize, ArrowSize)) {
        rect->left = scaleMinimapValue(ArrowLeft, scale);
        rect->top = scaleMinimapValue(ArrowTop, scale);
        rect->width = scaleMinimapValue(ArrowSize, scale);
        rect->height = scaleMinimapValue(ArrowSize, scale);
        return true;
    }

    return false;
}

bool isMinimapViewportActive(const UniversalScaleState& scale) {
    DWORD viewportIndex = 0;
    if (!safeReadDword(reinterpret_cast<const void*>(ViewportIndexAddress), viewportIndex) ||
        viewportIndex > 31) {
        return false;
    }

    const DWORD offset = viewportIndex * ViewportEntryStride;
    short width = 0;
    short height = 0;
    if (!safeReadShort(reinterpret_cast<const void*>(ViewportWidthAddress + offset), width) ||
        !safeReadShort(reinterpret_cast<const void*>(ViewportHeightAddress + offset), height)) {
        return false;
    }

    const int viewportSize = scaleMinimapValue(MinimapViewportSize, scale);
    return width == viewportSize && height == viewportSize;
}

}

void scaleHudMinimapExtent(Rect* rect, DWORD* stack) {
    UNREFERENCED_PARAMETER(stack);

    const UniversalScaleState* scale = ResolutionScale::get();
    if (!rect || !scale || isNativeScale(*scale)) {
        return;
    }

    scaleKnownHudMinimapRect(rect, *scale);
}

void prepareHudMinimapScale(
    void* hud, int* mapX, int* mapY, int* rectWidth, int* rectHeight) {
    UNREFERENCED_PARAMETER(mapX);
    UNREFERENCED_PARAMETER(mapY);
    UNREFERENCED_PARAMETER(rectWidth);
    UNREFERENCED_PARAMETER(rectHeight);

    const UniversalScaleState* scale = ResolutionScale::get();
    if (!hud || !scale || isNativeScale(*scale)) {
        return;
    }

    // Map coordinates already remain in the stable 440x256 domain. Only the
    // minimap viewport and controls scale; the draw paths then zoom the
    // 512x256 atlas and 440x256 fog grid from their respective native bases.
    const int viewportSize = scaleMinimapValue(MinimapViewportSize, *scale);
    char* base = static_cast<char*>(hud);
    writeMemory(base + 0x6088, &viewportSize, sizeof(viewportSize));
    writeMemory(base + 0x608C, &viewportSize, sizeof(viewportSize));

    Rect borderRect = {
        scaleMinimapValue(BorderLeft, *scale),
        scaleMinimapValue(BorderTop, *scale),
        scaleMinimapValue(BorderWidth, *scale),
        scaleMinimapValue(BorderHeight, *scale)
    };
    setControlRect(base + 0x5CC0, borderRect);

    Rect arrowRect = {
        (viewportSize - scaleMinimapValue(ArrowSize, *scale)) / 2,
        (viewportSize - scaleMinimapValue(ArrowSize, *scale)) / 2,
        scaleMinimapValue(ArrowSize, *scale),
        scaleMinimapValue(ArrowSize, *scale)
    };
    setControlRect(base + 0x5F40, arrowRect);
    setControlRect(base + 0x6098, arrowRect);
}

void zoomHudMinimapImageDraw(void* image, int* x, int* y, int* width, int* height) {
    UNREFERENCED_PARAMETER(image);

    const UniversalScaleState* scale = ResolutionScale::get();
    if (!scale || isNativeScale(*scale) ||
        !x || !y || !width || !height ||
        !isMinimapViewportActive(*scale)) {
        return;
    }

    const int originalWidth = *width;
    const int originalHeight = *height;
    if (originalWidth < MinimapTextureSize ||
        originalHeight < MinimapAtlasHalfHeight ||
        originalWidth > 4096 ||
        originalHeight > 4096) {
        return;
    }

    const int viewportCenter = scaleMinimapValue(MinimapViewportSize, *scale) / 2;
    *x = viewportCenter + scaleMinimapValue(*x - viewportCenter, *scale);
    *y = viewportCenter + scaleMinimapValue(*y - viewportCenter, *scale);
    *width = scaleMinimapValue(originalWidth, *scale);
    *height = scaleMinimapValue(originalHeight, *scale);
}

void beginHudMinimapGridZoom(void* hud, Rect* rect) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!hud || !rect || !scale || isNativeScale(*scale)) {
        return;
    }

    const bool readWidth = safeReadFloat(
        reinterpret_cast<const void*>(AreaMapViewportWidthAddress),
        g_areaMapViewportWidthBeforeGrid);
    const bool readHeight = safeReadFloat(
        reinterpret_cast<const void*>(AreaMapViewportHeightAddress),
        g_areaMapViewportHeightBeforeGrid);
    g_haveAreaMapViewportBeforeGrid = readWidth && readHeight;

    writeMemory(reinterpret_cast<void*>(AreaMapViewportWidthAddress),
        &AreaMapViewportWidth,
        sizeof(AreaMapViewportWidth));
    writeMemory(reinterpret_cast<void*>(AreaMapViewportHeightAddress),
        &AreaMapViewportHeight,
        sizeof(AreaMapViewportHeight));

    const int viewportSize = scaleMinimapValue(MinimapViewportSize, *scale);
    const int gridBasis = MinimapViewportSize;
    const int centerShift = (viewportSize - gridBasis) / 2;
    char* base = static_cast<char*>(hud);

    rect->left -= centerShift;
    rect->top -= centerShift;
    writeMemory(base + 0x6088, &gridBasis, sizeof(gridBasis));
    writeMemory(base + 0x608C, &gridBasis, sizeof(gridBasis));
}

void endHudMinimapGridZoom(void* hud) {
    restoreAreaMapViewportAfterGrid();

    const UniversalScaleState* scale = ResolutionScale::get();
    if (!hud || !scale || isNativeScale(*scale)) {
        return;
    }

    const int viewportSize = scaleMinimapValue(MinimapViewportSize, *scale);
    char* base = static_cast<char*>(hud);
    writeMemory(base + 0x6088, &viewportSize, sizeof(viewportSize));
    writeMemory(base + 0x608C, &viewportSize, sizeof(viewportSize));
}

}
