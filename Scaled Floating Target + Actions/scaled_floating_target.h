#pragma once

#include <windows.h>

namespace FloatingTargetScale {

struct Rect {
    int left;
    int top;
    int width;
    int height;
};

void scaleTargetControls(void* owner);
void correctTargetVerticalBounds(void* hud);
void roundTargetNameHeight(void* owner);

}
