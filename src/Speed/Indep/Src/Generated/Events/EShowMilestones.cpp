#include "EShowMilestones.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/FEPkg_PostRace.hpp"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/IAI.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"

void GPS_Disengage();

// PC: EShowMilestones 0x641FE0, ~EShowMilestones 0x625110 (trivial),
// EShowMilestones_MakeEvent_Callback 0x6420F0. Fills in the pursuit results
// and pushes the milestone rewards screen.
EShowMilestones::EShowMilestones(int pEnterSafehouseOnExit)
    : Event(0x10), //
      fEnterSafehouseOnExit(pEnterSafehouseOnExit) {
    GPS_Disengage();
    IPlayer *player = IPlayer::First(PLAYER_ALL);
    ISimable *isim = player != nullptr ? player->GetSimable() : nullptr;
    IVehicle *ivehicle;
    if (isim != nullptr && isim->QueryInterface(&ivehicle)) {
        IPerpetrator *iperp = nullptr;
        ivehicle->QueryInterface(&iperp);
        IVehicleAI *ai = ivehicle->GetAIVehiclePtr();
        if (ai != nullptr) {
            PostRacePursuitScreen::GetPursuitData().PopulateData(ai->GetPursuit(), iperp, fEnterSafehouseOnExit);
        }
    }
    cFEng::Get()->QueuePackagePush("PostRace_MilestoneRewards.fng", 0, 0, false);
}

EShowMilestones::~EShowMilestones() {}

const char *EShowMilestones::GetEventName() {
    return "EShowMilestones";
}

void EShowMilestones_MakeEvent_Callback(const void *staticData) {
    new EShowMilestones(static_cast<const EShowMilestones::StaticData *>(staticData)->fEnterSafehouseOnExit);
}
