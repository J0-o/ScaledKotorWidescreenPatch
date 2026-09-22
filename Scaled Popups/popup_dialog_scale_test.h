#pragma once

#include <windows.h>

namespace PopupDialogScaleTest {

struct Rect {
    int left;
    int top;
    int width;
    int height;
};

void scaleCenteredPopup(void* owner, DWORD* returnAddressSlot);
void scaleLayoutPopup(void* owner, bool centerHorizontally);
void scaleLateResolutionPopup(void* owner);
void captureStatusSummary(void* owner);
void restoreStatusSummary(void* owner);
void refreshTrackedPopups();
void clearTrackedPopup(void* owner);
void restoreFitCeilings();
void scaleStatusSummarySetRect(void* control, DWORD* returnAddressSlot, DWORD* rectPointerSlot);
void scaleMessageBoxButtonSetRect(void* control, DWORD* returnAddressSlot, DWORD* rectPointerSlot);

}
