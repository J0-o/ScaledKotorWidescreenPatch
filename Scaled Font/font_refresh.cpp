#include "font_refresh.h"
#include "system_font_atlas.h"
#include "../Common/ResolutionScale.h"
#include "GameAPI/GameVersion.h"


namespace FontRefresh {
namespace {
using UpdateAllFontsFn = void (__thiscall*)(void*);
using WrapTextFn = void (__thiscall*)(void*);

UpdateAllFontsFn g_updateAllFonts = nullptr;
WrapTextFn g_wrapText = nullptr;
void** g_guiManager = nullptr;
int g_textObjectOffset = -1;
bool g_resolved = false;
bool g_refreshing = false;
float g_contentScale = 0.0f;

bool resolveGameApi() {
    if (g_resolved) return true;
    if (!GameVersion::IsInitialized() && !GameVersion::Initialize()) return false;
    try {
        g_updateAllFonts = reinterpret_cast<UpdateAllFontsFn>(
            GameVersion::GetFunctionAddress("CSWGuiManager", "UpdateAllFonts"));
        g_wrapText = reinterpret_cast<WrapTextFn>(
            GameVersion::GetFunctionAddress("CSWGuiText", "wrapText"));
        g_guiManager = static_cast<void**>(
            GameVersion::GetGlobalPointer("GUI_MANAGER_PTR"));
        g_textObjectOffset = GameVersion::GetOffset("CSWGuiTextParams", "text_object");
    } catch (const GameVersionException&) {
        return false;
    }
    g_resolved = g_updateAllFonts && g_wrapText && g_guiManager &&
        g_textObjectOffset >= 0;
    return g_resolved;
}

void updateAllFonts() {
    if (resolveGameApi() && *g_guiManager) g_updateAllFonts(*g_guiManager);
}

float readFontScale() {
    const UniversalScaleState& scale = *ResolutionScale::get();
    return twoXScaleAdjustment(scale);
}
} // namespace

float contentScale() {
    if (g_contentScale <= 0.0f) g_contentScale = readFontScale();
    return g_contentScale;
}

void resolutionChanged() {
    g_refreshing = true;
    g_contentScale = readFontScale();
    SystemFontAtlas::refreshMetrics();
    updateAllFonts();
    g_refreshing = false;
}


void guiFontReset(void* textParams) {
    if (!g_refreshing || !textParams || !resolveGameApi()) return;
    void* text = *reinterpret_cast<void**>(
        static_cast<char*>(textParams) + g_textObjectOffset);
    if (text) g_wrapText(text);
}

} // namespace FontRefresh
