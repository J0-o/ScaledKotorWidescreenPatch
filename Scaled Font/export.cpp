#include "font_refresh.h"
#include "msdf_renderer.h"
#include "font_draw.h"
#include "system_font_atlas.h"

namespace {
void* fontInfoFromCaurFont(void* caurFont) {
    // CAurFont's font-info pointer is confirmed at every TextOutA hook site.
    return *reinterpret_cast<void**>(static_cast<char*>(caurFont) + 0x18);
}

void* fontInfoFromStackSlot(void* caurFontSlot) {
    // KPM passes esp+N as the address of the original game-stack slot.
    return fontInfoFromCaurFont(*static_cast<void**>(caurFontSlot));
}
} // namespace

extern "C" void __cdecl onFontTextureMetadataLoaded(void* texture) {
    SystemFontAtlas::registerTextureFont(texture);
}

extern "C" void __cdecl rewrapResetGuiFont(void* textParams) {
    FontRefresh::guiFontReset(textParams);
}

extern "C" void __cdecl refreshResolutionDependentUi() {
    FontRefresh::resolutionChanged();
}

extern "C" void __cdecl forgetMsdfTextureFont(void* texture) {
    SystemFontAtlas::forgetTextureFont(texture);
}

extern "C" void __cdecl beginMsdfGuiText(void* fontInfo) {
    MsdfRenderer::begin(fontInfo);
}

extern "C" void __cdecl beginMsdfPrimaryTextBatch(void* caurFontSlot) {
    MsdfRenderer::begin(fontInfoFromStackSlot(caurFontSlot));
}

extern "C" void __cdecl beginMsdfInlineTextBatch(void* caurFont) {
    MsdfRenderer::begin(fontInfoFromCaurFont(caurFont));
}

extern "C" void __cdecl endMsdfTextBatch() {
    MsdfRenderer::end();
}

extern "C" void __cdecl beginMsdfGuiGlyph(void* frame, void* fontInfo, const unsigned char* character) {
    FontDraw::beginGuiGlyph(frame, fontInfo, character);
}

extern "C" void __cdecl beginMsdfGuiHyphen(void* frame, void* fontInfo) {
    FontDraw::beginGuiHyphen(frame, fontInfo);
}

extern "C" void __cdecl endMsdfGuiGlyph() {
    FontDraw::endGuiGlyph();
}

extern "C" void __cdecl prepareMsdfPrimaryGeometry(
        void* vertices, int count, void* caurFontSlot) {
    FontDraw::textOutGeometry(fontInfoFromStackSlot(caurFontSlot), vertices, count);
}

extern "C" void __cdecl prepareMsdfInlineGeometry(
        void* vertices, int count, void* caurFont) {
    FontDraw::textOutGeometry(fontInfoFromCaurFont(caurFont), vertices, count);
}
