#pragma once

// Height, baseline and textureWidth use CAurFontInfo logical units (100 pixels).
// The original height and spacing values are retained so resolution changes use
// an absolute scale rather than multiplying already-scaled game data.
namespace FontMetrics {

struct Original {
    float fontHeight;
    float spacingR;
    float spacingB;
};

struct Values {
    float fontHeight;
    float baseline;
    float textureWidth;
    float spacingR;
    float spacingB;
};

struct AtlasGeometry {
    int width;
    int lineHeight;
    int ascent;
};

inline Values derive(const Original& original, const AtlasGeometry& atlas,
                     float absoluteScale) {
    const double desiredPixels = static_cast<double>(original.fontHeight) *
        100.0 * absoluteScale;
    const double glyphScale = desiredPixels / atlas.lineHeight;
    return {
        static_cast<float>(desiredPixels / 100.0),
        static_cast<float>(atlas.ascent * glyphScale / 100.0),
        static_cast<float>(atlas.width * glyphScale / 100.0),
        static_cast<float>(original.spacingR * static_cast<double>(absoluteScale)),
        static_cast<float>(original.spacingB * static_cast<double>(absoluteScale))
    };
}

} // namespace FontMetrics
