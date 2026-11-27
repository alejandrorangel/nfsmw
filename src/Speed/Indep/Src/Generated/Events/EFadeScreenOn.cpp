#include "EFadeScreenOn.hpp"
#include "ESndGameState.hpp"
#include "Speed/Indep/Src/EAXSound/SoundPause.h"
#include "Speed/Indep/Src/FEng/FEngine.h"
#include "Speed/Indep/Src/Frontend/FEJoyInput.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Frontend/FEngHashes/ScriptHashes.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEObjects.hpp"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"

void Main_SkipFrame(int numToSkip);
void SetSoundControlState(bool bON, eSNDCTLSTATE esndstate, const char *Reason);

// PC: EFadeScreenOn 0x62C9A0, ~EFadeScreenOn 0x621EF0 (trivial),
// EFadeScreenOn_MakeEvent_Callback 0x62CB00. The constructor does the work:
// it puts FadeScreen.fng up and starts its fade ("APPEAR", or the invisible
// variant that only shows the loading bar).
EFadeScreenOn::EFadeScreenOn(bool pLoadingBarOnly)
    : Event(0x10), //
      fLoadingBarOnly(pLoadingBarOnly) {
    Main_SkipFrame(2);
    if (!cFEng::Get()->IsPackagePushed("FadeScreen.fng")) {
        cFEng::Get()->PushNoControlPackage("FadeScreen.fng", FE_PACKAGE_PRIORITY_SECOND_CLOSEST);
    }
    if (TheGameFlowManager.GetState() == GAMEFLOW_STATE_IN_FRONTEND) {
        if (cFEngJoyInput::Get()->IsJoyEnabled(JOYSTICK_PORT_ALL)) {
            cFEngJoyInput::Get()->JoyDisable(JOYSTICK_PORT_ALL, true);
        }
    }
    FEManager::Get()->SuppressControllerError(true);
    if (!FEngIsScriptSet("FadeScreen.fng", 0x027FF2DC, FEHASH_APPEAR)) {
        FEngSetScript("FadeScreen.fng", 0x027FF2DC, fLoadingBarOnly ? FEHASH_APPEARINVISIBLE : FEHASH_APPEAR, true);
    }
    new ESndGameState(11, true); // GameFlowSndState[11]
    SetSoundControlState(true, SNDSTATE_MINILOAD, "EFadeScreenOn");
}

EFadeScreenOn::~EFadeScreenOn() {}

const char *EFadeScreenOn::GetEventName() {
    return "EFadeScreenOn";
}

void EFadeScreenOn_MakeEvent_Callback(const void *staticData) {
    const EFadeScreenOn::StaticData *data = static_cast<const EFadeScreenOn::StaticData *>(staticData);
    new EFadeScreenOn(data->fLoadingBarOnly);
}
