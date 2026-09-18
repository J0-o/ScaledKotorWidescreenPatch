#include "menu_background_shift_test.h"
#include "../Common/ResolutionScale.h"

namespace MenuBackgroundShiftTest {

namespace {

constexpr DWORD BackgroundChildOffset = 0x5C;
constexpr DWORD BackgroundLeftOffset = 0x04;
constexpr DWORD BackgroundWidthOffset = 0x0C;
constexpr DWORD BackgroundNameOffset = 0x54;
constexpr DWORD BackgroundChildVtable = 0x0073E338;
constexpr int ImageWidth = 4096;
constexpr int CenterSafeWidth = 2048;
constexpr char MenuBackgroundName[] = "800x600back";
constexpr char ComputerBackgroundName[] = "800x600comp0";
constexpr char PazaakBackgroundName[] = "800x600pazaak";
constexpr char LoadBackgroundName[] = "800x600load";
constexpr char MapBackgroundName[] = "800x600map";
constexpr char StoreBackgroundName[] = "800x600store";
constexpr char ComputerAltBackgroundName[] = "800x600comp1";

bool stringEquals(const char* actual, const char* expected) {
    for (;;) {
        if (*actual != *expected) {
            return false;
        }

        if (*actual == '\0') {
            return true;
        }

        ++actual;
        ++expected;
    }
}

void centerControlFor4096Background(char* control,
                                    const UniversalScaleState& scale) {
    const int backgroundWidth = divideRoundedNearest(
        static_cast<long long>(scale.uiWidth) * ImageWidth, CenterSafeWidth);
    const int backgroundLeft = (scale.screenWidth - backgroundWidth) / 2;

    *reinterpret_cast<int*>(control + BackgroundLeftOffset) = backgroundLeft;
    *reinterpret_cast<int*>(control + BackgroundWidthOffset) = backgroundWidth;
}

bool isKnownBackgroundName(const char* name) {
    return stringEquals(name, MenuBackgroundName) ||
        stringEquals(name, ComputerBackgroundName) ||
        stringEquals(name, PazaakBackgroundName) ||
        stringEquals(name, LoadBackgroundName) ||
        stringEquals(name, MapBackgroundName) ||
        stringEquals(name, StoreBackgroundName) ||
        stringEquals(name, ComputerAltBackgroundName);
}

}

void centerMenuBackgroundDynamic(void* parent) {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!parent || !scale) {
        return;
    }

    __try {
        char* child = static_cast<char*>(parent) + BackgroundChildOffset;
        DWORD vtable = *reinterpret_cast<DWORD*>(child);
        if (vtable != BackgroundChildVtable) {
            return;
        }

        const char* name = child + BackgroundNameOffset;
        if (!isKnownBackgroundName(name)) {
            return;
        }

        centerControlFor4096Background(child, *scale);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}
