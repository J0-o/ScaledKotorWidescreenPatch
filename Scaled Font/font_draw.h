#pragma once
namespace FontDraw {
void beginGuiGlyph(void* frame, void* fontInfo, const unsigned char* character);
void beginGuiHyphen(void* frame, void* fontInfo);
void endGuiGlyph();
void textOutGeometry(void* fontInfo, void* vertices, int count);
}
