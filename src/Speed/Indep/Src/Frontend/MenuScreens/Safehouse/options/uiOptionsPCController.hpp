#ifndef _UIOPTIONSPCCONTROLLER
#define _UIOPTIONSPCCONTROLLER

#include <types.h>

#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feArrayScrollerMenu.hpp"
#include "Speed/Indep/Src/Input/PCInputBindings.h"

// The PC's controller configuration screen (Options_PC_Controller.fng,
// Pause_PC_Controller.fng): two columns (primary, secondary binding) of the
// rebindable actions, for the keyboard or the joystick.

// One binding (speed.exe 1.3 vtable 0x89C244, ctor sub_51D330).
// total size: 0x120
class PCControllerDatum : public ArrayDatum {
  public:
    PCControllerDatum(uint32 action_hash) : ArrayDatum(0, 0), ActionHash(action_hash) {
        Text[0] = '\0';
    }
    ~PCControllerDatum() override {}

    char Text[PCINPUT_BINDING_NAME_SIZE]; // offset 0x18, size 0x104
    uint32 ActionHash;                    // offset 0x11C, size 0x4
};

// A binding's box, with the row's action label and the binding's text
// (vtable 0x89C23C).
// total size: 0x1C
class PCControllerSlot : public ArraySlot {
  public:
    PCControllerSlot(FEObject *box, FEString *label, FEString *text) : ArraySlot(box), Label(label), Text(text) {}
    ~PCControllerSlot() override {}
    void Update(ArrayDatum *datum, bool isSelected) override;

  private:
    FEString *Label; // offset 0x14, size 0x4
    FEString *Text;  // offset 0x18, size 0x4
};

// total size: 0x2B4
class UIOptionsPCController : public ArrayScrollerMenu {
  public:
    UIOptionsPCController(ScreenConstructorData *sd);
    ~UIOptionsPCController() override {}
    void NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) override;
    void RefreshHeader() override;

  private:
    void Setup();
    void SetupBindings();
    void ToggleCapture();
    void ToggleDevice();
    void RefreshBindings();
    bool BindingsUnchanged();
    void RestoreBindings();
    void ClearBinding();
    void DefaultBindings();
    int NumActions() {
        return mDevice != PCINPUT_KEYBOARD ? PCINPUT_NUM_CONFIG_ACTIONS : PCINPUT_NUM_KEYBOARD_CONFIG_ACTIONS;
    }

    bool mCalledFromPauseMenu;                                // offset 0xE0, size 0x1
    bool mCapturing;                                          // offset 0xE1, size 0x1
    int mDevice;                                              // offset 0xE4, size 0x4
    PCInputBinding mOriginalBindings[PCINPUT_NUM_CONFIG_ACTIONS][2]; // offset 0xE8, size 0x1C8
    bool mOriginalForceFeedback;                              // offset 0x2B0, size 0x1
};

#endif
