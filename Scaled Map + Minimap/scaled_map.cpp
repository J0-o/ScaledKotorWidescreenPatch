// Menu-map scaling implementation.
#include "scaled_map.h"
#include "../Common/ResolutionScale.h"

namespace WidescreenUiScale {

namespace {

constexpr int UiBaseHeight = 480;
constexpr int AreaMapTextureWidth = 512;
constexpr int AreaMapTextureHeight = 256;
constexpr int AreaMapViewportWidth = 440;
constexpr int AreaMapViewportHeight = 256;
constexpr int ArrowIconSize = 0x20;
constexpr int CircleIconSize = 0x10;
constexpr int TargetIconSize = 0x14;
constexpr DWORD MapViewOffset = 0x0064;
constexpr DWORD MapHiderOffset = 0x0E38;
constexpr DWORD MapTextureOffset = 0x1080;

Rect makeRect(int left, int top, int width, int height) {
    return { left, top, width, height };
}

bool writeMemory(void* address, const void* replacement, size_t size) {
    __try {
        DWORD oldProtect = 0;
        if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        CopyMemory(address, replacement, size);
        FlushInstructionCache(GetCurrentProcess(), address, size);

        DWORD ignored = 0;
        VirtualProtect(address, size, oldProtect, &ignored);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void writeInt(int address, int value) {
    writeMemory(reinterpret_cast<void*>(address), &value, sizeof(value));
}

void writeFloat(int address, float value) {
    writeMemory(reinterpret_cast<void*>(address), &value, sizeof(value));
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

int scaleMapValue(int value, const UniversalScaleState& scale) {
    return scaleUiValueFromBase(value, BaseHeight, UiBaseHeight, scale);
}

void setControlSize(char* control, int width, int height) {
    if (!control) {
        return;
    }

    writeMemory(control + 0x0C, &width, sizeof(width));
    writeMemory(control + 0x10, &height, sizeof(height));
}

void setControlRect(char* control, const Rect& rect) {
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

bool menuMapGridScaleActive = false;

void patchStableMapIconMaterialSizes() {
    // Materials persist across resolution changes. Keep their cached geometry
    // native and scale each final draw rectangle from that stable base.
    writeInt(0x0069405B, ArrowIconSize);
    writeInt(0x006940DC, CircleIconSize);
    writeInt(0x0069418F, TargetIconSize);
}

void scaleLiveMenuMapPlayerArrow(
    void* map, const UniversalScaleState& scale) {
    if (!map) {
        return;
    }

    DWORD arrowMaterial = 0;
    char* mapHider = static_cast<char*>(map) + MapHiderOffset;
    if (!safeReadDword(mapHider + 0x60, arrowMaterial) ||
        arrowMaterial == 0) {
        return;
    }

    const Rect nativeRect = { 0, 0, ArrowIconSize, ArrowIconSize };
    writeMemory(reinterpret_cast<void*>(arrowMaterial + 0x04),
        &nativeRect, sizeof(nativeRect));

    const int size = scaleMapValue(ArrowIconSize, scale);
    const Rect drawRect = { 0, 0, size, size };
    writeMemory(reinterpret_cast<void*>(arrowMaterial + 0x14),
        &drawRect, sizeof(drawRect));
}

void patchStableMapDimensions(const UniversalScaleState& scale) {
    // Keep all generated marker coordinates in the original 440x256 domain.
    // Only the texture, visible controls, and final draw rectangles are scaled.
    writeInt(0x0069505C, scaleMapValue(AreaMapTextureWidth, scale));
    writeInt(0x00695064, scaleMapValue(AreaMapTextureHeight, scale));
    writeInt(0x00695082, AreaMapViewportWidth);
    writeInt(0x0069508A, AreaMapViewportHeight);

    writeInt(0x00579009, AreaMapViewportWidth);
    writeInt(0x0057901A, AreaMapViewportHeight);
    writeInt(0x00578E9B, AreaMapViewportWidth);
    writeInt(0x00578EA6, AreaMapViewportHeight);
    writeInt(0x00578F15, AreaMapViewportWidth);
    writeInt(0x00578F24, AreaMapViewportHeight);
    writeInt(0x00579344, AreaMapViewportWidth);
    writeInt(0x00579358, AreaMapViewportHeight);
    writeInt(0x00579377, AreaMapViewportWidth);
    writeInt(0x0057937E, AreaMapViewportWidth);
    writeInt(0x00579383, AreaMapViewportHeight);
    writeInt(0x0057938A, AreaMapViewportHeight);
    writeFloat(0x00747748, static_cast<float>(AreaMapViewportWidth));
    writeFloat(0x007455D4, static_cast<float>(AreaMapViewportHeight));

    patchStableMapIconMaterialSizes();
}

bool getScaledMapState(const UniversalScaleState*& scale) {
    scale = ResolutionScale::get();
    return scale && !(scale->uiWidth == BaseWidth && scale->uiHeight == BaseHeight);
}

void scaleLiveMenuMapControlSizes(void* map, const UniversalScaleState& scale) {
    if (!map) {
        return;
    }

    char* base = static_cast<char*>(map);
    char* mapView = base + MapViewOffset;
    char* mapHider = base + MapHiderOffset;
    char* mapTexture = base + MapTextureOffset;
    const int viewportWidth = scaleMapValue(AreaMapViewportWidth, scale);
    const int viewportHeight = scaleMapValue(AreaMapViewportHeight, scale);

    Rect mapViewRect = {};
    if (safeReadRect(mapView + 0x04, mapViewRect)) {
        mapViewRect.left -= (viewportWidth - mapViewRect.width) / 2;
        mapViewRect.top -= (viewportHeight - mapViewRect.height) / 2;
        mapViewRect.width = viewportWidth;
        mapViewRect.height = viewportHeight;
        setControlRect(mapView, mapViewRect);
    }
    else {
        setControlSize(mapView, viewportWidth, viewportHeight);
    }

    setControlRect(mapHider, makeRect(0, 0, viewportWidth, viewportHeight));
    setControlRect(mapTexture, makeRect(
        0, 0,
        scaleMapValue(AreaMapTextureWidth, scale),
        scaleMapValue(AreaMapTextureHeight, scale)));
}

}

void beginMenuMapGridScale() {
    const UniversalScaleState* scale = nullptr;
    if (!getScaledMapState(scale)) {
        return;
    }

    writeFloat(0x00747748, static_cast<float>(
        scaleMapValue(AreaMapViewportWidth, *scale)));
    writeFloat(0x007455D4, static_cast<float>(
        scaleMapValue(AreaMapViewportHeight, *scale)));
    menuMapGridScaleActive = true;
}

void endMenuMapGridScale() {
    if (!menuMapGridScaleActive) {
        return;
    }

    writeFloat(0x00747748, static_cast<float>(AreaMapViewportWidth));
    writeFloat(0x007455D4, static_cast<float>(AreaMapViewportHeight));
    menuMapGridScaleActive = false;
}

void prepareMenuMapScale(void* map) {
    const UniversalScaleState* scale = nullptr;
    if (!getScaledMapState(scale)) {
        return;
    }
    patchStableMapDimensions(*scale);
    scaleLiveMenuMapControlSizes(map, *scale);
    scaleLiveMenuMapPlayerArrow(map, *scale);
}

void refreshResolutionState() {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (scale) {
        patchStableMapDimensions(*scale);
    }
}

void prepareMenuMapIconMaterials() {
    const UniversalScaleState* scale = nullptr;
    if (getScaledMapState(scale)) {
        patchStableMapIconMaterialSizes();
    }
}

void prepareMenuMapDraw(void* map, int* width) {
    const UniversalScaleState* scale = nullptr;
    if (!getScaledMapState(scale)) {
        return;
    }
    patchStableMapDimensions(*scale);
    scaleLiveMenuMapControlSizes(map, *scale);
    scaleLiveMenuMapPlayerArrow(map, *scale);
    if (width) {
        *width = scaleMapValue(AreaMapViewportWidth, *scale);
    }
}

void prepareMenuMapMarkerDraw(Rect* rect) {
    const UniversalScaleState* scale = nullptr;
    if (!rect || !getScaledMapState(scale) ||
        rect->width <= 0 || rect->height <= 0 ||
        rect->width > 64 ||
        rect->height > 64) {
        return;
    }

    // The native coordinate remains resolution-independent. Scale the complete
    // rectangle once at the final draw boundary so cached markers cannot retain
    // a previous resolution's coordinate domain.
    rect->left = scaleMapValue(rect->left, *scale);
    rect->top = scaleMapValue(rect->top, *scale);
    rect->width = scaleMapValue(rect->width, *scale);
    rect->height = scaleMapValue(rect->height, *scale);
}

void prepareAreaMapDimensionsForScreen() {
    const UniversalScaleState* scale = nullptr;
    if (getScaledMapState(scale)) {
        patchStableMapDimensions(*scale);
    }
}

}
