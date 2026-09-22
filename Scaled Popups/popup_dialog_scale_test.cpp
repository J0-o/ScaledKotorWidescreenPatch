#include "popup_dialog_scale_test.h"
#include "../Common/ResolutionScale.h"

namespace PopupDialogScaleTest {

namespace {

constexpr DWORD ConfirmCenterReturn = 0x00626FF8;
constexpr DWORD DebugCenterReturn = 0x006BDDBB;
constexpr DWORD SaveNameCenterReturn = 0x006CAFFD;
constexpr DWORD SkillInfoCenterReturn = 0x006CE9A9;
constexpr DWORD DebugAltCenterReturn = 0x006CF6CB;
constexpr DWORD StatusSummaryRootReturn = 0x006262B7;
constexpr DWORD StatusSummaryButtonReturn = 0x00626301;
constexpr DWORD StatusSummaryButtonOffset = 0x1980;
constexpr DWORD StatusSummaryTextRowOffset = 0xE40;
constexpr DWORD StatusSummaryRowStride = 0x140;
constexpr DWORD StatusSummaryControlFlagsOffset = 0x44;
constexpr DWORD StatusSummaryVisibleFlag = 0x02;
constexpr int StatusSummaryRowCount = 9;
constexpr int StatusSummaryRightMargin = 10;
constexpr int StatusSummaryButtonBaseWidth = 100;
constexpr int StatusSummaryButtonBaseHeight = 22;
constexpr int StatusSummaryButtonBaseBottomMargin = 10;
constexpr DWORD MessageBoxOkButtonFinalReturn = 0x0062588D;
constexpr DWORD MessageBoxCancelButtonFinalReturn = 0x006258DB;
constexpr DWORD MessageBoxFrameIconOffset = 0x1B4;
constexpr DWORD MessageBoxIconFillStyleOffset =
    MessageBoxFrameIconOffset + 0x5C + 0x30;
constexpr DWORD MessageBoxIconFillStyleMask = 0x03;
constexpr DWORD MessageBoxIconStretchFillStyle = 0x02;
constexpr DWORD MessageBoxOkButtonOffset = 0x2F4;
constexpr DWORD MessageBoxCancelButtonOffset = 0x4B8;
constexpr DWORD MessageBoxControlOffset = 0x67C;
constexpr DWORD MessageBoxPanelBaseRectOffset = 0x95C;
constexpr DWORD MessageBoxMessageBaseRectOffset = 0x96C;
constexpr DWORD MessageBoxFitWidthOperand1 = 0x006256DC;
constexpr DWORD MessageBoxFitHeightOperand1 = 0x006256E3;
constexpr DWORD MessageBoxFitWidthOperand2 = 0x006256F6;
constexpr DWORD MessageBoxFitHeightOperand2 = 0x00625759;
constexpr DWORD MessageBoxIconInsetOperand = 0x0062540D;
constexpr DWORD StatusSummaryFitWidthOperand1 = 0x006261B0;
constexpr DWORD StatusSummaryFitWidthOperand2 = 0x006261F6;
constexpr DWORD MaximumPanelChildren = 1024;
constexpr int MaximumConfirmPopups = 8;
constexpr DWORD SkillInfoRowsOffset = 0x648;
constexpr DWORD SkillInfoRowStride = 0x310;
constexpr int SkillInfoRowCount = 10;

enum PopupSlot {
    DebugPopup,
    SaveNamePopup,
    SkillInfoPopup,
    DebugAltPopup,
    BarkPopup,
    PausePopup,
    ResolutionPopup,
    PopupSlotCount,
};

enum RootPlacement {
    AuthoredPosition,
    CenterHorizontally,
    CenterBothAxes,
};

struct ControlRectSnapshot {
    Rect rect;
    bool valid;
};

struct PanelSnapshot {
    char* owner;
    Rect root;
    ControlRectSnapshot children[MaximumPanelChildren];
    DWORD childCount;
    unsigned int layoutGeneration;
};

struct MessageBoxSnapshot {
    char* owner;
    Rect okButton;
    Rect cancelButton;
    Rect message;
    bool okButtonValid;
    bool cancelButtonValid;
    bool messageValid;
    DWORD iconFillStyle;
    bool iconFillStyleValid;
};

struct SkillInfoSnapshot {
    char* owner;
    ControlRectSnapshot rows[SkillInfoRowCount];
};

PanelSnapshot popupSnapshots[PopupSlotCount] = {};
PanelSnapshot confirmPopupSnapshots[MaximumConfirmPopups] = {};
PanelSnapshot statusSummaryBaseline = {};
PanelSnapshot statusSummaryRendered = {};
MessageBoxSnapshot messageBoxSnapshots[MaximumConfirmPopups] = {};
SkillInfoSnapshot skillInfoSnapshot = {};

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

bool hasUsefulRect(const Rect& rect) {
    return rect.width > 0 && rect.height > 0 &&
        rect.width < 8192 && rect.height < 8192;
}

Rect scaledRect(const Rect& rect, const UniversalScaleState& scale) {
    return {
        scaleTwoXValue(rect.left, scale),
        scaleTwoXValue(rect.top, scale),
        scaleTwoXValue(rect.width, scale),
        scaleTwoXValue(rect.height, scale),
    };
}

bool isIdentityPopupScale(const UniversalScaleState& scale) {
    return !scale.contentScalingEnabled ||
        static_cast<long long>(scale.contentScaleNumerator) * 4 ==
        static_cast<long long>(scale.contentScaleDenominator) * 9;
}

Rect placedRoot(const Rect& rect, const UniversalScaleState& scale,
                RootPlacement placement) {
    Rect scaled = scaledRect(rect, scale);
    if (placement == CenterHorizontally || placement == CenterBothAxes) {
        scaled.left = (scale.screenWidth - scaled.width) / 2;
    }
    if (placement == CenterBothAxes) {
        scaled.top = (scale.screenHeight - scaled.height) / 2;
    }
    return scaled;
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

    __try {
        *reinterpret_cast<Rect*>(control + sizeof(DWORD)) = rect;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool getPanelChildren(char* owner, DWORD& childrenData, DWORD& childrenSize) {
    return owner &&
        safeReadDword(owner + 0x20, childrenData) &&
        safeReadDword(owner + 0x24, childrenSize) &&
        childrenData != 0 &&
        childrenSize <= MaximumPanelChildren;
}

bool capturePanel(PanelSnapshot& snapshot, char* owner) {
    DWORD childrenData = 0;
    DWORD childrenSize = 0;
    Rect* root = reinterpret_cast<Rect*>(owner + sizeof(DWORD));
    if (!hasUsefulRect(*root) ||
        !getPanelChildren(owner, childrenData, childrenSize)) {
        return false;
    }

    snapshot = {};
    snapshot.owner = owner;
    snapshot.root = *root;
    snapshot.childCount = childrenSize;

    for (DWORD i = 0; i < childrenSize; ++i) {
        DWORD childAddress = 0;
        if (!safeReadDword(
                reinterpret_cast<const void*>(childrenData +
                    (i * sizeof(DWORD))),
                childAddress) ||
            childAddress == 0) {
            continue;
        }

        Rect* rect = reinterpret_cast<Rect*>(childAddress + sizeof(DWORD));
        if (hasUsefulRect(*rect)) {
            snapshot.children[i].rect = *rect;
            snapshot.children[i].valid = true;
        }
    }
    return true;
}

bool resolveSnapshotChildren(const PanelSnapshot& snapshot,
                             DWORD& childrenData) {
    DWORD childrenSize = 0;
    return snapshot.owner &&
        getPanelChildren(snapshot.owner, childrenData, childrenSize) &&
        childrenSize == snapshot.childCount;
}

void applyPanel(const PanelSnapshot& snapshot,
                const UniversalScaleState& scale,
                RootPlacement placement,
                bool unscaled = false) {
    DWORD childrenData = 0;
    if (!resolveSnapshotChildren(snapshot, childrenData)) {
        return;
    }

    for (DWORD i = 0; i < snapshot.childCount; ++i) {
        if (!snapshot.children[i].valid) {
            continue;
        }

        DWORD childAddress = 0;
        if (!safeReadDword(
                reinterpret_cast<const void*>(childrenData +
                    (i * sizeof(DWORD))),
                childAddress) ||
            childAddress == 0) {
            continue;
        }

        callControlSetRect(reinterpret_cast<char*>(childAddress),
            unscaled ? snapshot.children[i].rect :
            scaledRect(snapshot.children[i].rect, scale));
    }

    callControlSetRect(snapshot.owner, unscaled ? snapshot.root :
        placedRoot(snapshot.root, scale, placement));
}

PopupSlot centeredPopupSlot(DWORD returnAddress) {
    switch (returnAddress) {
    case DebugCenterReturn: return DebugPopup;
    case SaveNameCenterReturn: return SaveNamePopup;
    case SkillInfoCenterReturn: return SkillInfoPopup;
    default: return DebugAltPopup;
    }
}

int confirmPopupIndex(char* owner) {
    int emptyIndex = -1;
    for (int i = 0; i < MaximumConfirmPopups; ++i) {
        if (confirmPopupSnapshots[i].owner == owner) {
            return i;
        }
        if (emptyIndex < 0 && !confirmPopupSnapshots[i].owner) {
            emptyIndex = i;
        }
    }
    return emptyIndex;
}

bool isCenteredPopupCall(DWORD returnAddress) {
    return returnAddress == ConfirmCenterReturn ||
        returnAddress == DebugCenterReturn ||
        returnAddress == SaveNameCenterReturn ||
        returnAddress == SkillInfoCenterReturn ||
        returnAddress == DebugAltCenterReturn;
}

bool isMessageBoxButtonSetExtentReturn(DWORD returnAddress) {
    return returnAddress == MessageBoxOkButtonFinalReturn ||
        returnAddress == MessageBoxCancelButtonFinalReturn;
}

char* messageBoxOwnerFromButton(char* control, DWORD returnAddress) {
    if (returnAddress == MessageBoxOkButtonFinalReturn) {
        return control - MessageBoxOkButtonOffset;
    }
    if (returnAddress == MessageBoxCancelButtonFinalReturn) {
        return control - MessageBoxCancelButtonOffset;
    }
    return nullptr;
}

void writeCodeValue(DWORD address, DWORD value) {
    DWORD oldProtection = 0;
    void* operand = reinterpret_cast<void*>(address);
    if (VirtualProtect(operand, sizeof(DWORD), PAGE_EXECUTE_READWRITE,
                       &oldProtection)) {
        *reinterpret_cast<DWORD*>(operand) = value;
        VirtualProtect(operand, sizeof(DWORD), oldProtection, &oldProtection);
    }
}

void updateFitCeilings(const UniversalScaleState& scale) {
    writeCodeValue(MessageBoxIconInsetOperand,
        static_cast<DWORD>(scaleTwoXValue(32, scale)));
    writeCodeValue(MessageBoxFitWidthOperand1,
        static_cast<DWORD>(scale.screenWidth));
    writeCodeValue(MessageBoxFitHeightOperand1,
        static_cast<DWORD>(scale.screenHeight));
    writeCodeValue(MessageBoxFitWidthOperand2,
        static_cast<DWORD>(scale.screenWidth));
    writeCodeValue(MessageBoxFitHeightOperand2,
        static_cast<DWORD>(scale.screenHeight));
    writeCodeValue(StatusSummaryFitWidthOperand1,
        static_cast<DWORD>(scale.screenWidth));
    writeCodeValue(StatusSummaryFitWidthOperand2,
        static_cast<DWORD>(scale.screenWidth));
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
}

void captureMessageBox(MessageBoxSnapshot& snapshot, char* owner) {
    snapshot = {};
    snapshot.owner = owner;

    const DWORD offsets[] = {
        MessageBoxOkButtonOffset,
        MessageBoxCancelButtonOffset,
        MessageBoxControlOffset,
    };
    Rect* destinations[] = {
        &snapshot.okButton,
        &snapshot.cancelButton,
        &snapshot.message,
    };
    bool* valid[] = {
        &snapshot.okButtonValid,
        &snapshot.cancelButtonValid,
        &snapshot.messageValid,
    };

    for (int i = 0; i < 3; ++i) {
        Rect* rect = reinterpret_cast<Rect*>(
            owner + offsets[i] + sizeof(DWORD));
        if (hasUsefulRect(*rect)) {
            *destinations[i] = *rect;
            *valid[i] = true;
        }
    }

    snapshot.iconFillStyleValid = safeReadDword(
        owner + MessageBoxIconFillStyleOffset,
        snapshot.iconFillStyle);
}

void applyMessageBoxIconFillStyle(MessageBoxSnapshot& snapshot,
                                  const UniversalScaleState& scale) {
    if (!snapshot.owner || !snapshot.iconFillStyleValid) {
        return;
    }

    DWORD current = 0;
    DWORD* fillStyle = reinterpret_cast<DWORD*>(
        snapshot.owner + MessageBoxIconFillStyleOffset);
    if (!safeReadDword(fillStyle, current)) {
        return;
    }

    const DWORD style = isIdentityPopupScale(scale) ?
        snapshot.iconFillStyle : MessageBoxIconStretchFillStyle;
    *fillStyle = (current & ~MessageBoxIconFillStyleMask) |
        (style & MessageBoxIconFillStyleMask);
}

void updateMessageBoxLayoutBases(MessageBoxSnapshot& messageSnapshot,
                                 const PanelSnapshot& panelSnapshot,
                                 const UniversalScaleState& scale) {
    if (messageSnapshot.owner != panelSnapshot.owner ||
        !messageSnapshot.messageValid) {
        return;
    }

    Rect* root = reinterpret_cast<Rect*>(
        messageSnapshot.owner + sizeof(DWORD));
    if (hasUsefulRect(*root)) {
        *reinterpret_cast<Rect*>(messageSnapshot.owner +
            MessageBoxPanelBaseRectOffset) = *root;
        *reinterpret_cast<Rect*>(messageSnapshot.owner +
            MessageBoxMessageBaseRectOffset) =
            scaledRect(messageSnapshot.message, scale);
    }
}

void captureSkillInfoRows(char* owner) {
    skillInfoSnapshot = {};
    skillInfoSnapshot.owner = owner;
    for (int i = 0; i < SkillInfoRowCount; ++i) {
        Rect* rect = reinterpret_cast<Rect*>(owner + SkillInfoRowsOffset +
            (i * SkillInfoRowStride) + sizeof(DWORD));
        if (hasUsefulRect(*rect)) {
            skillInfoSnapshot.rows[i].rect = *rect;
            skillInfoSnapshot.rows[i].valid = true;
        }
    }
}

void applySkillInfoRows(const UniversalScaleState& scale) {
    if (skillInfoSnapshot.owner != popupSnapshots[SkillInfoPopup].owner) {
        return;
    }
    for (int i = 0; i < SkillInfoRowCount; ++i) {
        if (skillInfoSnapshot.rows[i].valid) {
            callControlSetRect(skillInfoSnapshot.owner + SkillInfoRowsOffset +
                (i * SkillInfoRowStride),
                scaledRect(skillInfoSnapshot.rows[i].rect, scale));
        }
    }
}

bool isStatusSummaryTextRow(char* owner, char* control) {
    for (int i = 0; i < StatusSummaryRowCount; ++i) {
        if (control == owner + StatusSummaryTextRowOffset +
                (i * StatusSummaryRowStride)) {
            return true;
        }
    }
    return false;
}

void scaleStatusSummaryButton(char* control, Rect& rect,
                              const Rect& root,
                              const UniversalScaleState& scale) {
    if (!control || !hasUsefulRect(root)) {
        return;
    }

    const int centerX = rect.left + (rect.width / 2);
    rect.width = scaleTwoXValue(StatusSummaryButtonBaseWidth, scale);
    rect.height = scaleTwoXValue(StatusSummaryButtonBaseHeight, scale);
    rect.left = centerX - (rect.width / 2);
    rect.top = root.height - rect.height -
        scaleTwoXValue(StatusSummaryButtonBaseBottomMargin, scale);
}

int applyStatusSummaryChildren(const PanelSnapshot& snapshot,
                               const UniversalScaleState& scale) {
    DWORD childrenData = 0;
    if (!resolveSnapshotChildren(snapshot, childrenData)) {
        return 0;
    }

    int rightmostText = 0;
    for (DWORD i = 0; i < snapshot.childCount; ++i) {
        if (!snapshot.children[i].valid) {
            continue;
        }

        DWORD childAddress = 0;
        if (!safeReadDword(
                reinterpret_cast<const void*>(childrenData +
                    (i * sizeof(DWORD))),
                childAddress) ||
            childAddress == 0) {
            continue;
        }

        char* child = reinterpret_cast<char*>(childAddress);
        if (child == snapshot.owner + StatusSummaryButtonOffset) {
            continue;
        }

        Rect scaled = scaledRect(snapshot.children[i].rect, scale);
        if (isStatusSummaryTextRow(snapshot.owner, child)) {
            scaled.width = snapshot.children[i].rect.width;
            DWORD flags = 0;
            if (safeReadDword(child + StatusSummaryControlFlagsOffset, flags) &&
                (flags & StatusSummaryVisibleFlag) != 0) {
                const int right = scaled.left + scaled.width;
                if (right > rightmostText) {
                    rightmostText = right;
                }
            }
        }
        callControlSetRect(child, scaled);
    }
    return rightmostText;
}

Rect scaledStatusSummaryRoot(const PanelSnapshot& snapshot,
                             const UniversalScaleState& scale,
                             int rightmostText) {
    Rect root = scaledRect(snapshot.root, scale);
    root.left = snapshot.root.left +
        ((snapshot.root.width - root.width) / 2);
    root.top = snapshot.root.top +
        ((snapshot.root.height - root.height) / 2);
    if (rightmostText > 0) {
        root.width = rightmostText +
            scaleTwoXValue(StatusSummaryRightMargin, scale);
        root.left = snapshot.root.left +
            ((snapshot.root.width - root.width) / 2);
    }
    return root;
}

void applyRenderedStatusSummary(const UniversalScaleState& scale) {
    if (!statusSummaryRendered.owner) {
        return;
    }

    const int rightmostText =
        applyStatusSummaryChildren(statusSummaryRendered, scale);
    const Rect root = scaledStatusSummaryRoot(
        statusSummaryRendered, scale, rightmostText);
    callControlSetRect(statusSummaryRendered.owner, root);

    DWORD childrenData = 0;
    if (!resolveSnapshotChildren(statusSummaryRendered, childrenData)) {
        return;
    }
    for (DWORD i = 0; i < statusSummaryRendered.childCount; ++i) {
        DWORD childAddress = 0;
        if (statusSummaryRendered.children[i].valid &&
            safeReadDword(reinterpret_cast<const void*>(childrenData +
                (i * sizeof(DWORD))), childAddress) &&
            reinterpret_cast<char*>(childAddress) ==
                statusSummaryRendered.owner + StatusSummaryButtonOffset) {
            Rect button = statusSummaryRendered.children[i].rect;
            scaleStatusSummaryButton(reinterpret_cast<char*>(childAddress),
                button, root, scale);
            callControlSetRect(reinterpret_cast<char*>(childAddress), button);
            break;
        }
    }
    statusSummaryRendered.layoutGeneration = scale.layoutGeneration;
}

}

void scaleCenteredPopup(void* ownerPtr, DWORD* returnAddressSlot) {
    const UniversalScaleState* scale = ResolutionScale::get();
    char* owner = static_cast<char*>(ownerPtr);
    DWORD returnAddress = 0;
    if (!owner || !returnAddressSlot || !scale ||
        !safeReadDword(returnAddressSlot, returnAddress) ||
        !isCenteredPopupCall(returnAddress)) {
        return;
    }

    int confirmIndex = -1;
    PanelSnapshot* snapshot = nullptr;
    if (returnAddress == ConfirmCenterReturn) {
        confirmIndex = confirmPopupIndex(owner);
        if (confirmIndex < 0) {
            return;
        }
        snapshot = &confirmPopupSnapshots[confirmIndex];
    }
    else {
        snapshot = &popupSnapshots[centeredPopupSlot(returnAddress)];
    }

    const bool newOwner = snapshot->owner != owner;
    if (newOwner && !capturePanel(*snapshot, owner)) {
        return;
    }

    if (returnAddress == ConfirmCenterReturn) {
        updateFitCeilings(*scale);
        if (newOwner) {
            captureMessageBox(messageBoxSnapshots[confirmIndex], owner);
        }
    }
    if (returnAddress == SkillInfoCenterReturn && newOwner) {
        captureSkillInfoRows(owner);
    }

    if (returnAddress == ConfirmCenterReturn) {
        applyMessageBoxIconFillStyle(
            messageBoxSnapshots[confirmIndex], *scale);
    }
    applyPanel(*snapshot, *scale, AuthoredPosition);
    if (returnAddress == SkillInfoCenterReturn) {
        applySkillInfoRows(*scale);
    }
    snapshot->layoutGeneration = scale->layoutGeneration;
}

void scaleLayoutPopup(void* ownerPtr, bool centerHorizontally) {
    const UniversalScaleState* scale = ResolutionScale::get();
    char* owner = static_cast<char*>(ownerPtr);
    if (!scale || !owner) {
        return;
    }

    PanelSnapshot& snapshot = popupSnapshots[
        centerHorizontally ? BarkPopup : PausePopup];
    if (snapshot.owner != owner && !capturePanel(snapshot, owner)) {
        return;
    }
    applyPanel(snapshot, *scale,
        centerHorizontally ? CenterHorizontally : AuthoredPosition);
    snapshot.layoutGeneration = scale->layoutGeneration;
}

void scaleLateResolutionPopup(void* ownerPtr) {
    const UniversalScaleState* scale = ResolutionScale::get();
    char* owner = static_cast<char*>(ownerPtr);
    PanelSnapshot& snapshot = popupSnapshots[ResolutionPopup];
    if (!scale || !owner ||
        (snapshot.owner != owner && !capturePanel(snapshot, owner))) {
        return;
    }
    applyPanel(snapshot, *scale, AuthoredPosition);
    snapshot.layoutGeneration = scale->layoutGeneration;
}

void captureStatusSummary(void* ownerPtr) {
    char* owner = static_cast<char*>(ownerPtr);
    if (owner && capturePanel(statusSummaryBaseline, owner)) {
        statusSummaryRendered = {};
    }
}

void restoreStatusSummary(void* ownerPtr) {
    const UniversalScaleState* scale = ResolutionScale::get();
    char* owner = static_cast<char*>(ownerPtr);
    if (!scale || owner != statusSummaryBaseline.owner) {
        return;
    }
    applyPanel(statusSummaryBaseline, *scale, AuthoredPosition, true);
}

void refreshTrackedPopups() {
    const UniversalScaleState* scale = ResolutionScale::get();
    if (!scale) {
        return;
    }

    updateFitCeilings(*scale);
    for (int i = 0; i < MaximumConfirmPopups; ++i) {
        PanelSnapshot& snapshot = confirmPopupSnapshots[i];
        if (!snapshot.owner ||
            snapshot.layoutGeneration == scale->layoutGeneration) {
            continue;
        }

        applyMessageBoxIconFillStyle(messageBoxSnapshots[i], *scale);
        applyPanel(snapshot, *scale, CenterBothAxes);
        updateMessageBoxLayoutBases(
            messageBoxSnapshots[i], snapshot, *scale);
        snapshot.layoutGeneration = scale->layoutGeneration;
    }

    for (int i = 0; i < PopupSlotCount; ++i) {
        PanelSnapshot& snapshot = popupSnapshots[i];
        if (!snapshot.owner ||
            snapshot.layoutGeneration == scale->layoutGeneration) {
            continue;
        }

        RootPlacement placement = AuthoredPosition;
        if (i <= DebugAltPopup) {
            placement = CenterBothAxes;
        }
        else if (i == BarkPopup) {
            placement = CenterHorizontally;
        }
        applyPanel(snapshot, *scale, placement);
        if (i == SkillInfoPopup) {
            applySkillInfoRows(*scale);
        }
        snapshot.layoutGeneration = scale->layoutGeneration;
    }

    if (statusSummaryRendered.owner &&
        statusSummaryRendered.layoutGeneration != scale->layoutGeneration) {
        applyRenderedStatusSummary(*scale);
    }
}

void clearTrackedPopup(void* ownerPtr) {
    char* owner = static_cast<char*>(ownerPtr);
    for (int i = 0; i < MaximumConfirmPopups; ++i) {
        if (confirmPopupSnapshots[i].owner == owner) {
            confirmPopupSnapshots[i] = {};
            messageBoxSnapshots[i] = {};
        }
    }
    for (int i = 0; i < PopupSlotCount; ++i) {
        if (popupSnapshots[i].owner == owner) {
            popupSnapshots[i] = {};
        }
    }
    if (skillInfoSnapshot.owner == owner) {
        skillInfoSnapshot = {};
    }
    if (statusSummaryBaseline.owner == owner) {
        statusSummaryBaseline = {};
        statusSummaryRendered = {};
    }
}

void restoreFitCeilings() {
    writeCodeValue(MessageBoxIconInsetOperand, 32);
    writeCodeValue(MessageBoxFitWidthOperand1, 440);
    writeCodeValue(MessageBoxFitHeightOperand1, 280);
    writeCodeValue(MessageBoxFitWidthOperand2, 440);
    writeCodeValue(MessageBoxFitHeightOperand2, 280);
    writeCodeValue(StatusSummaryFitWidthOperand1, 440);
    writeCodeValue(StatusSummaryFitWidthOperand2, 440);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
}

void scaleStatusSummarySetRect(void* controlPtr, DWORD* returnAddressSlot,
                               DWORD* rectPointerSlot) {
    const UniversalScaleState* scale = ResolutionScale::get();
    char* owner = static_cast<char*>(controlPtr);
    DWORD returnAddress = 0;
    DWORD rectAddress = 0;
    if (!scale || !owner || owner != statusSummaryBaseline.owner ||
        !returnAddressSlot || !rectPointerSlot ||
        !safeReadDword(returnAddressSlot, returnAddress) ||
        returnAddress != StatusSummaryRootReturn ||
        !safeReadDword(rectPointerSlot, rectAddress) || rectAddress == 0 ||
        !hasUsefulRect(*reinterpret_cast<Rect*>(rectAddress)) ||
        !capturePanel(statusSummaryRendered, owner)) {
        return;
    }

    Rect* rect = reinterpret_cast<Rect*>(rectAddress);
    statusSummaryRendered.root = *rect;
    const int rightmostText =
        applyStatusSummaryChildren(statusSummaryRendered, *scale);
    *rect = scaledStatusSummaryRoot(
        statusSummaryRendered, *scale, rightmostText);
    statusSummaryRendered.layoutGeneration = scale->layoutGeneration;
}

void scaleMessageBoxButtonSetRect(void* controlPtr, DWORD* returnAddressSlot,
                                  DWORD* rectPointerSlot) {
    const UniversalScaleState* scale = ResolutionScale::get();
    char* control = static_cast<char*>(controlPtr);
    DWORD returnAddress = 0;
    DWORD rectAddress = 0;
    if (!scale || !control || !returnAddressSlot || !rectPointerSlot ||
        !safeReadDword(returnAddressSlot, returnAddress) ||
        !safeReadDword(rectPointerSlot, rectAddress) || rectAddress == 0) {
        return;
    }

    Rect* rect = reinterpret_cast<Rect*>(rectAddress);
    if (!hasUsefulRect(*rect)) {
        return;
    }

    if (returnAddress == StatusSummaryButtonReturn &&
        statusSummaryRendered.owner &&
        control == statusSummaryRendered.owner + StatusSummaryButtonOffset) {
        DWORD childrenData = 0;
        if (resolveSnapshotChildren(statusSummaryRendered, childrenData)) {
            for (DWORD i = 0; i < statusSummaryRendered.childCount; ++i) {
                DWORD childAddress = 0;
                if (safeReadDword(reinterpret_cast<const void*>(childrenData +
                        (i * sizeof(DWORD))), childAddress) &&
                    reinterpret_cast<char*>(childAddress) == control) {
                    statusSummaryRendered.children[i].rect = *rect;
                    statusSummaryRendered.children[i].valid = true;
                    break;
                }
            }
        }
        Rect root = *reinterpret_cast<Rect*>(
            statusSummaryRendered.owner + sizeof(DWORD));
        scaleStatusSummaryButton(control, *rect, root, *scale);
        return;
    }

    if (!isMessageBoxButtonSetExtentReturn(returnAddress)) {
        return;
    }

    char* owner = messageBoxOwnerFromButton(control, returnAddress);
    const int confirmIndex = confirmPopupIndex(owner);
    if (confirmIndex < 0 ||
        confirmPopupSnapshots[confirmIndex].owner != owner ||
        messageBoxSnapshots[confirmIndex].owner != owner) {
        return;
    }

    MessageBoxSnapshot& messageSnapshot =
        messageBoxSnapshots[confirmIndex];

    const Rect& baseline = returnAddress == MessageBoxOkButtonFinalReturn ?
        messageSnapshot.okButton : messageSnapshot.cancelButton;
    const bool baselineValid = returnAddress == MessageBoxOkButtonFinalReturn ?
        messageSnapshot.okButtonValid : messageSnapshot.cancelButtonValid;
    if (!baselineValid) {
        return;
    }

    const int centerX = rect->left + (rect->width / 2);
    int width = scaleTwoXValue(baseline.width, *scale);
    Rect* root = reinterpret_cast<Rect*>(owner + sizeof(DWORD));
    const int margin = rect->left > 0 ? rect->left : 0;
    const int maxWidth = root->width - (margin * 2);
    if (maxWidth > 0 && width > maxWidth) {
        width = maxWidth;
    }
    rect->width = width;
    rect->left = centerX - (width / 2);
}

}
