#include "EShowResults.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/FEPkg_PostRace.hpp"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/IAI.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Sim/Simulation.h"

void GPS_Disengage();

// PC: EShowResults 0x642140, ~EShowResults 0x625280 (trivial),
// EShowResults_MakeEvent_Callback 0x642300. The constructor pushes the post
// race results screen (PostRace_Results.fng, or PostRace_Pursuit.fng with the
// player's pursuit filled in).
EShowResults::EShowResults(FERESULTTYPE pResultType, bool pCalledWhileDriving)
    : Event(0x10), //
      fResultType(pResultType), //
      fCalledWhileDriving(pCalledWhileDriving) {
    if (!FEManager::IsOkayToRequestPauseSimulation(0, true, false) && fCalledWhileDriving) {
        return;
    }
    GPS_Disengage();
    const char *packageName;
    if (fResultType == FERESULTTYPE_RACE) {
        packageName = "PostRace_Results.fng";
    } else if (fResultType == FERESULTTYPE_PURSUIT) {
        packageName = "PostRace_Pursuit.fng";
        IPlayer *player = IPlayer::First(PLAYER_ALL);
        ISimable *isim = player != nullptr ? player->GetSimable() : nullptr;
        IVehicle *ivehicle;
        if (isim != nullptr && isim->QueryInterface(&ivehicle)) {
            IPerpetrator *iperp = nullptr;
            ivehicle->QueryInterface(&iperp);
            IVehicleAI *ai = ivehicle->GetAIVehiclePtr();
            if (ai != nullptr) {
                PostRacePursuitScreen::GetPursuitData().PopulateData(ai->GetPursuit(), iperp, -1);
            }
        }
    } else {
        return;
    }
    u32 mask = FEngMapJoyportToJoyParam(FEDatabase->GetPlayersJoystickPort(0));
    if (Sim::GetUserMode() == Sim::USER_SPLIT_SCREEN || FEDatabase->IsSplitScreenMode()) {
        mask |= FEngMapJoyportToJoyParam(FEDatabase->GetPlayersJoystickPort(1));
    }
    if (cFEng::Get()->IsPackageInControl("ControllerUnplugged.fng")) {
        cFEng::Get()->PopErrorPackage();
    }
    cFEng::Get()->QueuePackagePush(packageName, 0, mask, false);
}

EShowResults::~EShowResults() {}

const char *EShowResults::GetEventName() {
    return "EShowResults";
}

void EShowResults_MakeEvent_Callback(const void *staticData) {
    const EShowResults::StaticData *data = static_cast<const EShowResults::StaticData *>(staticData);
    new EShowResults(data->fResultType, data->fCalledWhileDriving);
}
