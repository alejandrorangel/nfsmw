// speed.exe 1.3's UIOptionsPCController (ctor 0x545EB0, CreatePausePCController
// 0x571D00): the PC keyboard / joystick binding screen.
#include "uiOptionsPCController.hpp"

#include "Speed/Indep/Src/FEng/FEObject.h"
#include "Speed/Indep/Src/FEng/FEString.h"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngHashes/FEHash_FeBonusCards.hpp"
#include "Speed/Indep/Src/Frontend/FEngHashes/ScriptHashes.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEObjects.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEStrings.hpp"
#include "Speed/Indep/Src/Frontend/Localization/Localize.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feDialogBox.hpp"
#include "Speed/Indep/Src/Generated/Events/EUnPause.hpp"
#include "Speed/Indep/Src/Generated/LanguageHashes.hpp"
#include "Speed/Indep/Src/Input/IOModule.h"

namespace {

// The screen's FEng messages.
const uint32 kMsgClearBinding = 0x2DCC19C8;
const uint32 kMsgToggleForceFeedback = 0x36D64627;
const uint32 kMsgSaveChangesYes = 0xB850B019;
const uint32 kMsgSaveChangesNo = 0x06DC9631;
const uint32 kMsgLeaveScreen = 0x587C018B;
const uint32 kMsgCaptureStarted0 = 0xD707AE8D;
const uint32 kMsgCaptureStarted1 = 0x13FD3296;
const uint32 kMsgCaptureEnded0 = 0x9B2BA546;
const uint32 kMsgCaptureEnded1 = 0x8CB81F09;

// Objects.
const uint32 kObjJoystickOnly = 0x9CCF9099;
const uint32 kObjForceFeedback = 0x52CC164A;
const uint32 kObjForceFeedbackGroup = 0x8A41F5B9;
const uint32 kObjDeviceName = 0x4E11CF90;
const uint32 kObjCaptureAction = 0xB2D837B0;
const uint32 kObjCaptureBinding = 0x3BBE5E51;

// Language strings.
const uint32 kStrForceFeedback = 0x3DF9961B;
const uint32 kStrOn = 0x417B2604;
const uint32 kStrOff = 0x70DFE5C2;
const uint32 kStrRestoreDefaults = 0x8AEF5AE8;
const uint32 kStrSaveChanges = 0xF98FA043;

FEString *FindString(const char *pkg, uint32 hash) {
    FEObject *obj = FEngFindObject(pkg, hash);
    if (obj == nullptr || obj->Type != FE_String) {
        return nullptr;
    }
    return static_cast<FEString *>(obj);
}

bool &ForceFeedback() {
    return FEDatabase->GetPlayerSettings(0)->Rumble;
}

} // namespace

// sub_51D3A0
void PCControllerSlot::Update(ArrayDatum *datum, bool isSelected) {
    ArraySlot::Update(datum, isSelected);
    if (datum != nullptr) {
        PCControllerDatum *d = static_cast<PCControllerDatum *>(datum);
        FEngSetLanguageHash(Label, d->ActionHash);
        FEPrintf(Text, "%s", d->Text);
    }
}

// 0x545EB0
UIOptionsPCController::UIOptionsPCController(ScreenConstructorData *sd)
    : ArrayScrollerMenu(sd, 2, 8, true), //
      mCalledFromPauseMenu(sd->Arg != 0), //
      mCapturing(false),                  //
      mDevice(PCINPUT_KEYBOARD) {
    SetClickToSelectMode(true);
    SetMouseDownMsg(__BUTTON_RELEASED__);
    Setup();
    SetupBindings();
    RefreshHeader();
}

// 0x52A7B0: a primary and a secondary slot per row.
void UIOptionsPCController::Setup() {
    const char *pkg = GetPackageName();
    for (int row = 1; row <= GetHeight(); row++) {
        FEString *label = FindString(pkg, FEngHashString("KEY_TEXT_%d", row));
        FEObject *box = FEngFindObject(pkg, FEngHashString("KEY_BOX_P%d", row));
        FEString *text = FindString(pkg, FEngHashString("KEY_TEXT_P%d", row));
        AddSlot(new ("PCControllerSlot", 0) PCControllerSlot(box, label, text));
        box = FEngFindObject(pkg, FEngHashString("KEY_BOX_S%d", row));
        text = FindString(pkg, FEngHashString("KEY_TEXT_S%d", row));
        AddSlot(new ("PCControllerSlot", 0) PCControllerSlot(box, label, text));
    }
}

// sub_52AC70: the device's bindings, kept to restore them.
void UIOptionsPCController::SetupBindings() {
    mOriginalForceFeedback = ForceFeedback();
    ClearData();
    const int slot = mDevice != PCINPUT_KEYBOARD ? 2 : 0;
    if (mDevice == PCINPUT_KEYBOARD || mDevice == PCINPUT_JOYSTICK) {
        for (int i = 0; i < NumActions(); i++) {
            PCInputBinding *bindings = PCInputGetBindings(i);
            mOriginalBindings[i][0] = bindings[slot];
            mOriginalBindings[i][1] = bindings[slot + 1];
            PCControllerDatum *datum = new ("PCControllerDatum", 0) PCControllerDatum(PCInputGetActionNameHash(i));
            PCInputGetBindingName(datum->Text, mDevice, i, true);
            AddDatum(datum);
            datum = new ("PCControllerDatum", 0) PCControllerDatum(PCInputGetActionNameHash(i));
            PCInputGetBindingName(datum->Text, mDevice, i, false);
            AddDatum(datum);
        }
    }
    FEObject *joystick_only = FEngFindObject(GetPackageName(), kObjJoystickOnly);
    if (mDevice == PCINPUT_KEYBOARD) {
        FEngSetInvisible(joystick_only);
    } else {
        FEngSetVisible(joystick_only);
    }
    SetInitialPosition(0);
    RefreshHeader();
}

// sub_52AB90
void UIOptionsPCController::RefreshHeader() {
    ArrayScrollerMenu::RefreshHeader();
    if (PCInputJoystickPresent()) {
        const char *state = GetLocalizedString(ForceFeedback() ? kStrOn : kStrOff);
        FEPrintf(GetPackageName(), kObjForceFeedback, GetLocalizedString(kStrForceFeedback), state);
    } else {
        FEngSetScript(GetPackageName(), kObjForceFeedbackGroup, FEHASH_HIDE, true);
    }
    FEPrintf(GetPackageName(), kObjDeviceName, "%s", PCInputGetDeviceName(mDevice));
}

// sub_52AF70: starts capturing the selected binding, or ends the capture.
void UIOptionsPCController::ToggleCapture() {
    const int index = GetCurrentDatumNum();
    const int action = index / GetWidth();
    const bool primary = index % GetWidth() == 0;
    if (!mCapturing) {
        mCapturing = true;
        PCInputStartCapture(mDevice, action, primary);
        FEngSetLanguageHash(GetPackageName(), kObjCaptureAction, PCInputGetActionNameHash(action));
        FEPrintf(GetPackageName(), kObjCaptureBinding, "%s", static_cast<PCControllerDatum *>(GetCurrentDatum())->Text);
        cFEng::Get()->QueuePackageMessage(kMsgCaptureStarted0, nullptr, nullptr); // to every package
        cFEng::Get()->QueuePackageMessage(kMsgCaptureStarted1, nullptr, nullptr); // to every package
    } else {
        mCapturing = false;
        RefreshBindings();
        RefreshHeader();
        cFEng::Get()->QueuePackageMessage(kMsgCaptureEnded0, nullptr, nullptr); // to every package
        cFEng::Get()->QueuePackageMessage(kMsgCaptureEnded1, nullptr, nullptr); // to every package
    }
}

// sub_52AF20: the keyboard page and, with a joystick attached, the joystick's.
void UIOptionsPCController::ToggleDevice() {
    int device = PCINPUT_KEYBOARD;
    if (mDevice == PCINPUT_KEYBOARD && PCInputJoystickPresent()) {
        device = PCINPUT_JOYSTICK;
    }
    if (mDevice != device) {
        mDevice = device;
        SetupBindings();
        RefreshHeader();
    }
}

// sub_51D410
void UIOptionsPCController::RefreshBindings() {
    if (mDevice == PCINPUT_KEYBOARD || mDevice == PCINPUT_JOYSTICK) {
        for (int i = 0; i < NumActions(); i++) {
            PCControllerDatum *datum = static_cast<PCControllerDatum *>(GetDatumAt(GetWidth() * i));
            PCInputGetBindingName(datum->Text, mDevice, i, true);
            datum = static_cast<PCControllerDatum *>(GetDatumAt(GetWidth() * i + 1));
            PCInputGetBindingName(datum->Text, mDevice, i, false);
        }
    }
    RefreshHeader();
}

// sub_51D4B0
bool UIOptionsPCController::BindingsUnchanged() {
    const int slot = mDevice != PCINPUT_KEYBOARD ? 2 : 0;
    for (int i = 0; i < NumActions(); i++) {
        const PCInputBinding *bindings = PCInputGetBindings(i);
        for (int j = 0; j < 2; j++) {
            const PCInputBinding &a = mOriginalBindings[i][j];
            const PCInputBinding &b = bindings[slot + j];
            if (a.Type != b.Type || a.Code != b.Code || a.Index != b.Index) {
                return false;
            }
        }
    }
    return ForceFeedback() == mOriginalForceFeedback;
}

// sub_51D590
void UIOptionsPCController::RestoreBindings() {
    const int slot = mDevice != PCINPUT_KEYBOARD ? 2 : 0;
    for (int i = 0; i < NumActions(); i++) {
        PCInputBinding *bindings = PCInputGetBindings(i);
        bindings[slot] = mOriginalBindings[i][0];
        bindings[slot + 1] = mOriginalBindings[i][1];
    }
    ForceFeedback() = mOriginalForceFeedback;
    RefreshBindings();
}

// sub_51D660
void UIOptionsPCController::ClearBinding() {
    const int index = GetCurrentDatumNum();
    PCInputClearBinding(mDevice, index / GetWidth(), index % GetWidth() == 0);
    RefreshBindings();
}

// sub_51D6E0
void UIOptionsPCController::DefaultBindings() {
    if (PCInputJoystickPresent() && mDevice == PCINPUT_JOYSTICK) {
        PCInputDefaultJoystickBindings();
    } else {
        PCInputDefaultKeyboardBindings();
    }
    ForceFeedback() = true;
    RefreshBindings();
}

// 0x545F70
void UIOptionsPCController::NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) {
    ArrayScrollerMenu::NotificationMessage(msg, pobj, param1, param2);
    switch (msg) {
        case __PAD_START__:
            if (mCalledFromPauseMenu) {
                new EUnPause();
            }
            break;
        case __PAD_BACK__:
            cFEng::Get()->QueuePackageMessage(kMsgLeaveScreen, GetPackageName(), nullptr);
            break;
        case __BUTTON_RELEASED__:
            ToggleCapture();
            break;
        case kMsgClearBinding:
            ClearBinding();
            break;
        case kMsgToggleForceFeedback:
            if (mDevice == PCINPUT_JOYSTICK) {
                ForceFeedback() = !ForceFeedback();
                RefreshHeader();
            }
            break;
        case kMsgSaveChangesNo:
            RestoreBindings();
            ToggleDevice();
            break;
        case __PAD_LTRIGGER__:
        case __PAD_RTRIGGER__:
            if (!PCInputJoystickPresent()) {
                break;
            }
            if (!BindingsUnchanged()) {
                DialogInterface::ShowTwoButtons(GetPackageName(), mCalledFromPauseMenu ? "InGameDialog.fng" : "Dialog.fng", dialog_alert,
                                                LANGUAGE_COMMON_YES, LANGUAGE_COMMON_NO, kMsgSaveChangesYes, kMsgSaveChangesNo,
                                                dialog_message_no, first_dialog_button2, kStrSaveChanges);
                break;
            }
            ToggleDevice();
            break;
        case dialog_message_yes:
            DefaultBindings();
            break;
        case kMsgSaveChangesYes:
            FEDatabase->SetOptionsDirty(FEDatabase->IsOptionsDirty() || !BindingsUnchanged());
            ToggleDevice();
            break;
        case __PAD_BUTTON5__:
            DialogInterface::ShowTwoButtons(GetPackageName(), mCalledFromPauseMenu ? "InGameDialog.fng" : "Dialog.fng", dialog_alert,
                                            LANGUAGE_COMMON_YES, LANGUAGE_COMMON_NO, dialog_message_yes, dialog_message_no,
                                            dialog_message_no, first_dialog_button2, kStrRestoreDefaults);
            break;
        case FEMSG_SCREEN_TICK:
            if (mCapturing && !PCInputIsCapturing()) {
                ToggleCapture();
            }
            break;
        case __EXIT_COMPLETE__:
            FEDatabase->SetOptionsDirty(FEDatabase->IsOptionsDirty() || !BindingsUnchanged());
#if defined(EA_PORT_NATIVE)
            // The PC saves the bindings with the profile.
            PCInputSaveBindings();
#endif
            if (mCalledFromPauseMenu) {
                cFEng::Get()->QueuePackageSwitch("Pause_Main.fng", 1, 0, false);
            } else if (FEDatabase->IsOnlineMode()) {
                cFEng::Get()->QueuePackageSwitch("OL_MAIN.fng", 0, 0, false);
            } else {
                cFEng::Get()->QueuePackageSwitch("MainMenu_Sub.fng", 0, 0, false);
            }
            break;
        default:
            break;
    }
}
