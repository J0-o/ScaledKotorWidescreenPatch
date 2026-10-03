#pragma once

#include <windows.h>

namespace MenuScale {

struct Rect {
    int left;
    int top;
    int width;
    int height;
};

void scaleMenuPanelTree(void* panel);
void fixAlignmentSliderThumb(void* slider);
void scalePazaakGameCards(void* pazaakGame);
void refreshMenuPanelTrees();

}
