#include "scaled_menu.h"
#include "../Common/ResolutionScale.h"

namespace MenuScale {

namespace {

constexpr int BaseWidth = 800;
constexpr int BaseHeight = 600;
constexpr DWORD MainInterfaceAddress = 0x00833BB4;
constexpr DWORD GuiManagerPointerAddress = 0x007A39F4;
constexpr DWORD GuiManagerPanelsOffset = 0x88;
constexpr DWORD GuiManagerPanelCountOffset = 0x8C;
constexpr DWORD MainMenuVtable = 0x00752F70;
constexpr DWORD FadePanelVtable = 0x0074FC60;
constexpr DWORD TooltipPanelVtable = 0x00750030;
constexpr DWORD PazaakPlayerHandBase = 0x2DE0;
constexpr DWORD PazaakOpponentHandBase = 0x564C;
constexpr DWORD PazaakCardStride = 0x31C;
constexpr DWORD PazaakCardLabelOffset = 0x1C4;
constexpr DWORD PazaakCardFaceDrawModeOffset = 0x9C;

struct ScaleState {
    int baseWidth;
    int baseHeight;
    int targetWidth;
    int targetHeight;
    int offsetX;
    int offsetY;
};

struct PazaakCardRect {
    DWORD offset;
    Rect button;
    Rect label;
};

constexpr PazaakCardRect PazaakHandCards[] = {
    { PazaakPlayerHandBase + (PazaakCardStride * 0), { 94, 425, 80, 80 }, { 109, 433, 50, 50 } },
    { PazaakPlayerHandBase + (PazaakCardStride * 1), { 161, 425, 80, 80 }, { 176, 433, 50, 50 } },
    { PazaakPlayerHandBase + (PazaakCardStride * 2), { 229, 425, 80, 80 }, { 244, 433, 50, 50 } },
    { PazaakPlayerHandBase + (PazaakCardStride * 3), { 296, 425, 80, 80 }, { 311, 433, 50, 50 } },
    { PazaakOpponentHandBase + (PazaakCardStride * 0), { 421, 425, 80, 80 }, { 436, 433, 50, 50 } },
    { PazaakOpponentHandBase + (PazaakCardStride * 1), { 489, 425, 80, 80 }, { 505, 433, 50, 50 } },
    { PazaakOpponentHandBase + (PazaakCardStride * 2), { 556, 425, 80, 80 }, { 571, 433, 50, 50 } },
    { PazaakOpponentHandBase + (PazaakCardStride * 3), { 625, 425, 80, 80 }, { 640, 433, 50, 50 } }
};

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

void writeInt(int address, int value) {
    writeMemory(reinterpret_cast<void*>(address), &value, sizeof(value));
}

int scaleValue(int value, int target, int source) {
    if (target <= 0 || source <= 0) {
        return value;
    }

    return divideRoundedNearest(
        static_cast<long long>(value) * target, source);
}

bool isFourByThreeRoot(const Rect& rect) {
    return rect.left == 0 &&
        rect.top == 0 &&
        rect.width >= 640 &&
        rect.height >= 480 &&
        rect.width * 3 == rect.height * 4;
}

bool isTopTabRoot(const Rect& rect) {
    if (rect.left != 0 || rect.top != 0 || rect.width < 640) {
        return false;
    }

    const long long difference =
        static_cast<long long>(rect.height) * 640 -
        static_cast<long long>(rect.width) * 86;
    return difference >= -640 && difference <= 640;
}

bool isMainInterfacePanel(void* panel) {
    DWORD mainInterface = 0;
    return safeReadDword(reinterpret_cast<const void*>(MainInterfaceAddress), mainInterface) &&
        mainInterface != 0 &&
        reinterpret_cast<DWORD>(panel) == mainInterface;
}

bool isMainMenuPanel(void* panel) {
    DWORD vtable = 0;
    return safeReadDword(panel, vtable) && vtable == MainMenuVtable;
}

bool isFadePanel(void* panel) {
    DWORD vtable = 0;
    return safeReadDword(panel, vtable) && vtable == FadePanelVtable;
}

ScaleState makeMenuScale(int baseWidth, int baseHeight,
                         const UniversalScaleState& universalScale) {
    return {
        baseWidth,
        baseHeight,
        universalScale.uiWidth,
        universalScale.uiHeight,
        (universalScale.screenWidth - universalScale.uiWidth) / 2,
        (universalScale.screenHeight - universalScale.uiHeight) / 2
    };
}

ScaleState makeFullscreenMenuScale(int baseWidth, int baseHeight,
                                   const UniversalScaleState& universalScale) {
    return {
        baseWidth,
        baseHeight,
        universalScale.screenWidth,
        universalScale.screenHeight,
        0,
        0
    };
}

void patchCenteringConstants(int width, int height) {
    writeInt(0x0040AA65, width);
    writeInt(0x0040AA85, height);
    writeInt(0x0040B6C7, -width);
    writeInt(0x0040B6DA, -height);
    writeInt(0x0040BA6C, -width);
    writeInt(0x0040BA83, -height);
}

bool hasUsefulRect(const Rect& rect) {
    return rect.width > 0 && rect.height > 0 &&
        rect.width < 8192 && rect.height < 8192;
}

void callControlSetRect(char* control, const Rect& rect) {
    if (!control) {
        return;
    }

    __try {
        DWORD vtable = *reinterpret_cast<DWORD*>(control);
        DWORD setRect = *reinterpret_cast<DWORD*>(vtable + 4);
        if (setRect != 0) {
            typedef void(__thiscall *SetRectFn)(void*, const Rect*);
            reinterpret_cast<SetRectFn>(setRect)(control, &rect);
            return;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    writeMemory(control + 0x04, &rect, sizeof(rect));
}

Rect scaledRect(const Rect& rect, const ScaleState& scale) {
    return {
        scaleValue(rect.left, scale.targetWidth, scale.baseWidth),
        scaleValue(rect.top, scale.targetHeight, scale.baseHeight),
        scaleValue(rect.width, scale.targetWidth, scale.baseWidth),
        scaleValue(rect.height, scale.targetHeight, scale.baseHeight)
    };
}

void scaleControl(char* control, const ScaleState& scale) {
    if (!control) {
        return;
    }

    Rect* rect = reinterpret_cast<Rect*>(control + 0x04);
    if (hasUsefulRect(*rect)) {
        callControlSetRect(control, scaledRect(*rect, scale));
    }
}

void setPazaakCardRect(char* pazaakGame, const PazaakCardRect& card, const ScaleState& scale) {
    char* cardBase = pazaakGame + card.offset;
    const BYTE fillControlDrawMode = 2;
    callControlSetRect(cardBase, scaledRect(card.button, scale));
    callControlSetRect(cardBase + PazaakCardLabelOffset, scaledRect(card.label, scale));
    writeMemory(cardBase + PazaakCardFaceDrawModeOffset, &fillControlDrawMode, sizeof(fillControlDrawMode));
}

void scalePanelControls(char* panel, const ScaleState& scale) {
    DWORD childrenData = 0;
    DWORD childrenSize = 0;
    if (!safeReadDword(panel + 0x20, childrenData) ||
        !safeReadDword(panel + 0x24, childrenSize) ||
        childrenData == 0 ||
        childrenSize > 1024) {
        return;
    }

    for (DWORD i = 0; i < childrenSize; ++i) {
        DWORD child = 0;
        if (safeReadDword(reinterpret_cast<const void*>(childrenData + (i * sizeof(DWORD))), child) &&
            child != 0) {
            scaleControl(reinterpret_cast<char*>(child), scale);
        }
    }
}

void scalePanelBorder(char* panel, const Rect& scaledPanelRect) {
    DWORD border = 0;
    if (!safeReadDword(panel + 0x5C, border) || border == 0) {
        return;
    }

    Rect borderRect = { 0, 0, scaledPanelRect.width, scaledPanelRect.height };
    callControlSetRect(reinterpret_cast<char*>(border), borderRect);
}

bool makeScaleForPanel(void* panel, const Rect& original,
                       const UniversalScaleState& universalScale,
                       ScaleState& scale) {
    if (isMainMenuPanel(panel) && original.left == 0 && original.top == 0 &&
        hasUsefulRect(original)) {
        scale = makeFullscreenMenuScale(
            original.width, original.height, universalScale);
        return true;
    }
    if (isTopTabRoot(original)) {
        scale = makeMenuScale(
            original.width, (original.width * 3) / 4, universalScale);
        return true;
    }
    if (isFourByThreeRoot(original)) {
        scale = makeMenuScale(original.width, original.height, universalScale);
        return true;
    }
    return false;
}

void scalePanelTree(char* panel, DWORD vtable, const Rect& root,
                    const UniversalScaleState& universalScale) {
    if (vtable == FadePanelVtable) {
        const Rect fullscreen = {
            0, 0, universalScale.screenWidth, universalScale.screenHeight,
        };
        callControlSetRect(panel, fullscreen);
        return;
    }

    ScaleState scale = {};
    if (!makeScaleForPanel(panel, root, universalScale, scale)) {
        return;
    }
    patchCenteringConstants(scale.targetWidth, scale.targetHeight);
    const Rect scaledRoot = scaledRect(root, scale);
    callControlSetRect(panel, scaledRoot);
    scalePanelBorder(panel, scaledRoot);

    DWORD childrenData = 0;
    DWORD childrenSize = 0;
    if (!safeReadDword(panel + 0x20, childrenData) ||
        !safeReadDword(panel + 0x24, childrenSize)) {
        return;
    }
    for (DWORD i = 0; i < childrenSize; ++i) {
        DWORD child = 0;
        if (safeReadDword(
                reinterpret_cast<void*>(childrenData + i * sizeof(DWORD)),
                child)) {
            scaleControl(reinterpret_cast<char*>(child), scale);
        }
    }
}

}

void scaleMenuPanelTree(void* panel) {
    const UniversalScaleState* universalScale = ResolutionScale::get();
    if (!panel ||
        !universalScale ||
        isMainInterfacePanel(panel)) {
        return;
    }

    DWORD vtable = 0;
    if (!safeReadDword(panel, vtable)) {
        return;
    }
    if (vtable == TooltipPanelVtable) {
        // This is a hidden coordinate canvas, not a visible menu panel.
        // Scaling its single label tiles icon-only tooltips across the extent.
        return;
    }

    Rect* rect = reinterpret_cast<Rect*>(static_cast<char*>(panel) + 0x04);
    if (!hasUsefulRect(*rect)) {
        return;
    }

    const Rect original = *rect;
    ScaleState ignored = {};
    if (!isFadePanel(panel) &&
        !makeScaleForPanel(panel, original, *universalScale, ignored)) {
        return;
    }
    scalePanelTree(static_cast<char*>(panel), vtable, original, *universalScale);
}

void scalePazaakGameCards(void* pazaakGame) {
    const UniversalScaleState* universalScale = ResolutionScale::get();
    if (!pazaakGame || !universalScale) {
        return;
    }

    const ScaleState scale = makeMenuScale(
        BaseWidth, BaseHeight, *universalScale);
    char* base = static_cast<char*>(pazaakGame);
    for (const PazaakCardRect& card : PazaakHandCards) {
        setPazaakCardRect(base, card, scale);
    }
}

void refreshMenuPanelTrees() {
    const UniversalScaleState* universalScale = ResolutionScale::get();
    if (!universalScale) {
        return;
    }
    patchCenteringConstants(universalScale->uiWidth, universalScale->uiHeight);

    DWORD guiManager = 0;
    DWORD panels = 0;
    DWORD panelCount = 0;
    if (!safeReadDword(reinterpret_cast<const void*>(GuiManagerPointerAddress), guiManager) ||
        guiManager == 0 ||
        !safeReadDword(reinterpret_cast<const void*>(guiManager + GuiManagerPanelsOffset), panels) ||
        !safeReadDword(reinterpret_cast<const void*>(guiManager + GuiManagerPanelCountOffset), panelCount) ||
        panels == 0 || panelCount > 256) {
        return;
    }

    for (DWORD i = 0; i < panelCount; ++i) {
        DWORD panel = 0;
        if (safeReadDword(reinterpret_cast<const void*>(panels + i * sizeof(DWORD)), panel) &&
            panel != 0) {
            scaleMenuPanelTree(reinterpret_cast<void*>(panel));
        }
    }
}

}
