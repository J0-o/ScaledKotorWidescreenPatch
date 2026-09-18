#pragma once

struct UniversalScaleState {
    int screenWidth;
    int screenHeight;
    // Uniformly scaled 640x480 UI coordinate canvas.
    int uiWidth;
    int uiHeight;
    // Resolution-derived layout scale. Manual overrides never change this.
    int scaleNumerator;
    int scaleDenominator;
    // Configurable scale used by fonts and compact UI content.
    int contentScaleNumerator;
    int contentScaleDenominator;
    // The provider owns the resolution guard. Consumers only honor this flag.
    int contentScalingEnabled;
    // Increments whenever the provider observes a different screen size.
    // Consumers can use this to reapply base-derived layout exactly once.
    unsigned int layoutGeneration;
};

typedef void(__cdecl *ResolutionRefreshCallback)();

inline int divideRoundedNearest(long long numerator, long long denominator) {
    if (denominator <= 0) {
        return static_cast<int>(numerator);
    }

    const long long half = denominator / 2;
    return numerator >= 0 ?
        static_cast<int>((numerator + half) / denominator) :
        -static_cast<int>((-numerator + half) / denominator);
}

inline int scaleUiValue(int value, const UniversalScaleState& scale) {
    if (scale.scaleNumerator <= 0 || scale.scaleDenominator <= 0) {
        return value;
    }

    return divideRoundedNearest(
        static_cast<long long>(value) * scale.scaleNumerator,
        scale.scaleDenominator);
}

inline int scaleContentValue(int value, const UniversalScaleState& scale) {
    if (!scale.contentScalingEnabled ||
        scale.contentScaleNumerator <= 0 ||
        scale.contentScaleDenominator <= 0) {
        return value;
    }

    return divideRoundedNearest(
        static_cast<long long>(value) * scale.contentScaleNumerator,
        scale.contentScaleDenominator);
}

inline int scaleUiValueFromBase(int value, int sourceBase, int uiBase,
                                const UniversalScaleState& scale) {
    if (sourceBase <= 0 || uiBase <= 0 ||
        scale.scaleNumerator <= 0 || scale.scaleDenominator <= 0) {
        return value;
    }

    return divideRoundedNearest(
        static_cast<long long>(value) * uiBase * scale.scaleNumerator,
        static_cast<long long>(sourceBase) * scale.scaleDenominator);
}

inline int unscaleUiValueToBase(int value, int sourceBase, int uiBase,
                                const UniversalScaleState& scale) {
    if (sourceBase <= 0 || uiBase <= 0 ||
        scale.scaleNumerator <= 0 || scale.scaleDenominator <= 0) {
        return value;
    }

    return divideRoundedNearest(
        static_cast<long long>(value) * sourceBase * scale.scaleDenominator,
        static_cast<long long>(uiBase) * scale.scaleNumerator);
}

inline float twoXScaleAdjustment(const UniversalScaleState& scale) {
    if (!scale.contentScalingEnabled ||
        scale.contentScaleNumerator <= 0 ||
        scale.contentScaleDenominator <= 0) {
        return 1.0f;
    }

    return static_cast<float>(scale.contentScaleNumerator * 4.0) /
        static_cast<float>(scale.contentScaleDenominator * 9.0);
}

inline int scaleTwoXValue(int value, const UniversalScaleState& scale) {
    if (!scale.contentScalingEnabled ||
        scale.contentScaleNumerator <= 0 ||
        scale.contentScaleDenominator <= 0) {
        return value;
    }

    return divideRoundedNearest(
        static_cast<long long>(value) * scale.contentScaleNumerator * 4,
        static_cast<long long>(scale.contentScaleDenominator) * 9);
}

inline bool isIdentityUiScale(const UniversalScaleState& scale) {
    return scale.scaleNumerator == scale.scaleDenominator;
}

inline bool isIdentityContentScale(const UniversalScaleState& scale) {
    return !scale.contentScalingEnabled ||
        scale.contentScaleNumerator == scale.contentScaleDenominator;
}

extern "C" const UniversalScaleState* __cdecl getUniversalScaleState();
extern "C" void __cdecl onResolutionModeCommitted(void* guiInGame);
extern "C" int __cdecl registerResolutionRefreshCallback(
    ResolutionRefreshCallback callback);
extern "C" void __cdecl unregisterResolutionRefreshCallback(
    ResolutionRefreshCallback callback);
