#include "scaled_menu.h"
#include "ResolutionScale.h"

namespace {

constexpr DWORD GenericPanelVtable = 0x0073E010;
constexpr size_t MaxTrackedPanels = 128;
constexpr size_t MaxTrackedChildren = 256;

struct ChildBaseline {
    void* control;
    MenuScale::Rect rect;
};

struct PanelBaseline {
    void* panel;
    DWORD vtable;
    MenuScale::Rect root;
    ChildBaseline children[MaxTrackedChildren];
    DWORD childCount;
    unsigned int appliedGeneration;
};

PanelBaseline panelBaselines[MaxTrackedPanels] = {};

bool usefulRect(const MenuScale::Rect& rect) {
    return rect.width > 0 && rect.height > 0 &&
        rect.width < 8192 && rect.height < 8192;
}

bool readPanel(void* panel, DWORD& vtable, MenuScale::Rect& root,
               DWORD& children, DWORD& childCount) {
    if (!panel) {
        return false;
    }

    __try {
        char* object = static_cast<char*>(panel);
        vtable = *reinterpret_cast<DWORD*>(object);
        root = *reinterpret_cast<MenuScale::Rect*>(object + 0x04);
        children = *reinterpret_cast<DWORD*>(object + 0x20);
        childCount = *reinterpret_cast<DWORD*>(object + 0x24);
        return vtable != 0 && vtable != GenericPanelVtable &&
            usefulRect(root) && childCount <= MaxTrackedChildren &&
            (childCount == 0 || children != 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void setRect(void* control, const MenuScale::Rect& rect) {
    if (!control) {
        return;
    }

    __try {
        DWORD vtable = *reinterpret_cast<DWORD*>(control);
        DWORD function = *reinterpret_cast<DWORD*>(vtable + 4);
        if (function) {
            using SetRectFn = void(__thiscall*)(void*, const MenuScale::Rect*);
            reinterpret_cast<SetRectFn>(function)(control, &rect);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

PanelBaseline* findOrAllocateBaseline(void* panel) {
    PanelBaseline* empty = nullptr;
    for (PanelBaseline& baseline : panelBaselines) {
        if (baseline.panel == panel) {
            return &baseline;
        }
        if (!baseline.panel && !empty) {
            empty = &baseline;
        }
    }
    return empty;
}

void captureBaseline(void* panel) {
    DWORD vtable = 0;
    DWORD children = 0;
    DWORD childCount = 0;
    MenuScale::Rect root = {};
    if (!readPanel(panel, vtable, root, children, childCount)) {
        return;
    }

    PanelBaseline* baseline = findOrAllocateBaseline(panel);
    if (!baseline) {
        return;
    }

    baseline->panel = panel;
    baseline->vtable = vtable;
    baseline->root = root;
    baseline->childCount = 0;
    baseline->appliedGeneration = 0;

    __try {
        for (DWORD i = 0; i < childCount; ++i) {
            void* child = reinterpret_cast<void**>(children)[i];
            if (!child) {
                continue;
            }
            const MenuScale::Rect rect =
                *reinterpret_cast<MenuScale::Rect*>(static_cast<char*>(child) + 0x04);
            if (usefulRect(rect)) {
                ChildBaseline& saved = baseline->children[baseline->childCount++];
                saved.control = child;
                saved.rect = rect;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        baseline->panel = nullptr;
        baseline->childCount = 0;
    }
}

void restoreAndScale(PanelBaseline& baseline, unsigned int generation) {
    __try {
        if (!baseline.panel ||
            *reinterpret_cast<DWORD*>(baseline.panel) != baseline.vtable) {
            baseline.panel = nullptr;
            baseline.childCount = 0;
            return;
        }

        setRect(baseline.panel, baseline.root);
        for (DWORD i = 0; i < baseline.childCount; ++i) {
            setRect(baseline.children[i].control, baseline.children[i].rect);
        }
        MenuScale::scaleMenuPanelTree(baseline.panel);
        baseline.appliedGeneration = generation;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        baseline.panel = nullptr;
        baseline.childCount = 0;
    }
}

}

extern "C" void __cdecl scaleMenuPanelTree(void* panel) {
    __try {
        // StopLoadFromLayout supplies a fresh vanilla layout, so replacing an
        // older entry also handles object-address reuse after reconstruction.
        captureBaseline(panel);
        MenuScale::scaleMenuPanelTree(panel);
        const UniversalScaleState* scale = ResolutionScale::get();
        PanelBaseline* baseline = findOrAllocateBaseline(panel);
        if (scale && baseline && baseline->panel == panel) {
            baseline->appliedGeneration = scale->layoutGeneration;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl fixAlignmentSliderThumb(void* slider) {
    __try {
        MenuScale::fixAlignmentSliderThumb(slider);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl scalePazaakGameCards(void* pazaakGame) {
    __try {
        MenuScale::scalePazaakGameCards(pazaakGame);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" void __cdecl refreshResolutionDependentUi() {
    __try {
        const UniversalScaleState* scale = ResolutionScale::get();
        if (!scale) {
            return;
        }
        for (PanelBaseline& baseline : panelBaselines) {
            if (baseline.panel &&
                baseline.appliedGeneration != scale->layoutGeneration) {
                restoreAndScale(baseline, scale->layoutGeneration);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(reserved);
    if (reason == DLL_PROCESS_ATTACH) {
        ResolutionScale::subscribe(refreshResolutionDependentUi);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        ResolutionScale::unsubscribe(refreshResolutionDependentUi);
    }
    return TRUE;
}
