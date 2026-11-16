#pragma once

namespace SystemFontAtlas {

bool isApplied(void* fontInfo);
void refreshMetrics();

// The metadata-complete hook identifies and installs supported logical fonts.
// Texture destruction removes the associated live state.
void registerTextureFont(void* texture);
void forgetTextureFont(void* texture);

// Logical metrics and UVs remain unpadded except while a glyph is emitted.
struct RenderPadding { float uv, logical; };
struct GlyphUvRestore {
    float* upper;
    float* lower;
    float upperX, upperY;
    float lowerX, lowerY;
};
bool renderPadding(void* fontInfo, RenderPadding& out);
bool expandGlyphUv(void* fontInfo, unsigned int glyph, const RenderPadding& padding,
                   GlyphUvRestore& saved);
void restoreGlyphUv(const GlyphUvRestore& saved);

// Called with texture unit zero active. Ensures the selected atlas texture
// exists in the current context, binds it, and returns its square side length.
bool bindTexture(void* fontInfo, int& side);
void invalidateGlContext();

} // namespace SystemFontAtlas
