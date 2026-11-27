#include "ELoadingScreenOff.hpp"
#include "EFadeScreenOn.hpp"
#include "Speed/Indep/Src/Frontend/FEPackageData.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Safehouse/career/uiRepSheetRivalFlow.hpp"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"

// PC: ELoadingScreenOff 0x62D450, ~ELoadingScreenOff 0x622600,
// ELoadingScreenOff_MakeEvent_Callback 0x62D520. The constructor pops the
// loading package (fading the screen out first when going into the game); the
// destructor (when the event runs) resumes a rival flow waiting on the load.
ELoadingScreenOff::ELoadingScreenOff() : Event(0x10) {
    if (cFEng::Get()->IsPackageInControl(GetLoadingScreenPackageName())) {
        cFEng::Get()->QueuePackagePop(0);
        if (TheGameFlowManager.GetState() == GAMEFLOW_STATE_RACING) {
            new EFadeScreenOn(false);
        }
    }
}

ELoadingScreenOff::~ELoadingScreenOff() {
    if (uiRepSheetRivalFlow::Get() != nullptr && uiRepSheetRivalFlow::Get()->GetStage() == uiRepSheetRivalFlow::BACK_TO_FREE_ROAM) {
        uiRepSheetRivalFlow::Get()->Next();
    }
}

const char *ELoadingScreenOff::GetEventName() {
    return "ELoadingScreenOff";
}

void ELoadingScreenOff_MakeEvent_Callback(const void *staticData) {
    new ELoadingScreenOff();
}
