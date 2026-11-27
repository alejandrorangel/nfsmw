#include "ERaceSheetOn.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/FEPkg_PostRace.hpp"

extern int iCurrentViewBin;

// PC: ERaceSheetOn 0x624360 (vtable 0x8A7234), ERaceSheetOn_MakeEvent_Callback
// 0x62FFE0. The constructor does the work: the in-game rap sheet (the HUD's
// d-pad down, 0) or the rival challenge (a boss race's trigger 3, the
// blacklist rival flow 2: the latter even when the game can't pause), not
// over the pursuit results.
ERaceSheetOn::ERaceSheetOn(int pRivalFlow)
    : Event(0x10), //
      fRivalFlow(pRivalFlow) {
    if (pRivalFlow != 2) {
        if (!FEManager::IsOkayToRequestPauseSimulation(0, true, false) || PostRacePursuitScreen::GetPursuitData().mPursuitIsActive) {
            return;
        }
    }
    iCurrentViewBin = FEDatabase->GetCareerSettings()->GetCurrentBin();
    u32 controlMask = FEngMapJoyportToJoyParam(FEDatabase->GetPlayersJoystickPort(0));
    if (fRivalFlow < 1) {
        cFEng::Get()->QueuePackagePush("InGameReputationOverview.fng", 1, controlMask, false);
    } else {
        cFEng::Get()->QueuePackagePush("InGameRivalChallenge.fng", fRivalFlow, controlMask, false);
    }
}

ERaceSheetOn::~ERaceSheetOn() {}

const char *ERaceSheetOn::GetEventName() {
    return "ERaceSheetOn";
}

void ERaceSheetOn_MakeEvent_Callback(const void *staticData) {
    new ERaceSheetOn(static_cast<const ERaceSheetOn::StaticData *>(staticData)->fRivalFlow);
}
