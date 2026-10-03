#include "font_draw.h"
#include "font_geometry.h"
#include "system_font_atlas.h"

namespace FontDraw {
namespace {
// Stack-local locations at the verified GUI glyph hook sites.
constexpr unsigned Left = 0x1c, Right = 0x3c, Top = 0x38, Bottom = 0x34;
constexpr unsigned XFactor = 0x44, YFactor = 0x40;

struct GuiGlyphState {
    char* frame = nullptr;
    FontGeometry::Quad original{};
    SystemFontAtlas::GlyphUvRestore uv{};
} g_gui;

float& local(char* frame, unsigned offset) {
    return *reinterpret_cast<float*>(frame + offset);
}

void restoreGuiGlyph() {
    SystemFontAtlas::restoreGlyphUv(g_gui.uv);
    local(g_gui.frame, Left) = g_gui.original.left;
    local(g_gui.frame, Right) = g_gui.original.right;
    local(g_gui.frame, Top) = g_gui.original.top;
    local(g_gui.frame, Bottom) = g_gui.original.bottom;
    g_gui.frame = nullptr;
}

void beginGui(void* frame, void* fontInfo, unsigned int glyph) {
    SystemFontAtlas::RenderPadding padding;
    if (!SystemFontAtlas::renderPadding(fontInfo, padding)) return;

    char* stackFrame = static_cast<char*>(frame);
    const FontGeometry::Quad original = {
        local(stackFrame, Left), local(stackFrame, Right),
        local(stackFrame, Top), local(stackFrame, Bottom)
    };
    FontGeometry::Quad expanded = original;
    FontGeometry::expandQuad(expanded,
        padding.uv * local(stackFrame, XFactor),
        padding.uv * local(stackFrame, YFactor));

    SystemFontAtlas::GlyphUvRestore uv;
    if (!SystemFontAtlas::expandGlyphUv(fontInfo, glyph, padding, uv)) return;

    g_gui.frame = stackFrame;
    g_gui.original = original;
    g_gui.uv = uv;
    local(stackFrame, Left) = expanded.left;
    local(stackFrame, Right) = expanded.right;
    local(stackFrame, Top) = expanded.top;
    local(stackFrame, Bottom) = expanded.bottom;
}
} // namespace

void beginGuiGlyph(void* frame, void* fontInfo, const unsigned char* character) {
    beginGui(frame, fontInfo, *character);
}

void beginGuiHyphen(void* frame, void* fontInfo) {
    beginGui(frame, fontInfo, '-');
}

void endGuiGlyph() {
    if (g_gui.frame) restoreGuiGlyph();
}

void textOutGeometry(void* fontInfo, void* vertices, int count) {
    SystemFontAtlas::RenderPadding padding;
    if (!SystemFontAtlas::renderPadding(fontInfo, padding)) return;

    auto* data = static_cast<FontGeometry::Vertex*>(vertices);
    for (int i = 0; i < count; i += 6)
        FontGeometry::expandGlyph(data + i, padding.logical, padding.uv);
}
} // namespace FontDraw
