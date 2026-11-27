#include "EFadeScreenOff.hpp"
#include "ESndGameState.hpp"
#include "Speed/Indep/Src/EAXSound/SoundPause.h"
#include "Speed/Indep/Src/Frontend/FEJoyInput.hpp"
#include "Speed/Indep/Src/Frontend/FEngHashes/ScriptHashes.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEObjects.hpp"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"

void Main_SkipFrame(int numToSkip);
void SetSoundControlState(bool bON, eSNDCTLSTATE esndstate, const char *Reason);

// PC: EFadeScreenOff 0x621E80, ~EFadeScreenOff 0x62C810,
// EFadeScreenOff_MakeEvent_Callback 0x62C950. The destructor (when the event
// runs) plays the fade's exit script: HIDE drops a loading-bar-only fade, the
// 15/30/45 "IN" scripts fade the screen back in from a full fade.
EFadeScreenOff::EFadeScreenOff(int pDurationScript)
    : Event(0x10), //
      fDurationScript(pDurationScript) {
    Main_SkipFrame(2);
}

EFadeScreenOff::~EFadeScreenOff() {
    if (cFEng::Get()->IsPackagePushed("FadeScreen.fng")) {
        unsigned int script = static_cast<unsigned int>(fDurationScript);
        if (FEngIsScriptSet("FadeScreen.fng", 0x027FF2DC, FEHASH_APPEARINVISIBLE) && script == FEHASH_HIDE) {
            FEngSetScript("FadeScreen.fng", 0x027FF2DC, FEHASH_HIDE, true);
        } else if ((script == FEHASH_15_IN || script == FEHASH_30_IN || script == FEHASH_45_IN) &&
                   FEngIsScriptSet("FadeScreen.fng", 0x027FF2DC, FEHASH_APPEAR)) {
            FEngSetScript("FadeScreen.fng", 0x027FF2DC, script, true);
        }
    }
    if (TheGameFlowManager.GetState() == GAMEFLOW_STATE_IN_FRONTEND) {
        if (!cFEngJoyInput::Get()->IsJoyEnabled(JOYSTICK_PORT_ALL)) {
            cFEngJoyInput::Get()->JoyEnable(JOYSTICK_PORT_ALL, true);
        }
    }
    new ESndGameState(11, false); // GameFlowSndState[11]
    SetSoundControlState(false, SNDSTATE_MINILOAD, "EFadeScreenOff");
}

const char *EFadeScreenOff::GetEventName() {
    return "EFadeScreenOff";
}

void EFadeScreenOff_MakeEvent_Callback(const void *staticData) {
    const EFadeScreenOff::StaticData *data = static_cast<const EFadeScreenOff::StaticData *>(staticData);
    new EFadeScreenOff(data->fDurationScript);
}
