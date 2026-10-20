#pragma once

#include <windows.h>

namespace ClassSelectionLayoutConstantsTest {

struct Rect {
    int left;
    int top;
    int width;
    int height;
};

void prepareClassSelectionLayout(void* owner);
void finishClassSelectionLayout(void* ownerStackSlot);
void patchInitialClassSelectionRects(void* owner);

}
