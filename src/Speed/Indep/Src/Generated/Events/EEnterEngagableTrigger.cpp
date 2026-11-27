#include "EEnterEngagableTrigger.hpp"
#include "Speed/Indep/Src/Interfaces/IFengHud.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"

// PC: EEnterEngagableTrigger 0x621BC0 (vtable 0x8A6B70), ~EEnterEngagableTrigger
// 0x6407D0, EEnterEngagableTrigger_MakeEvent_Callback 0x62C550. The engage
// scripts (world_engage / bin_race_engage "prompt") send it while the player
// is inside an event's trigger: the first player's HUD shows the engage prompt.
EEnterEngagableTrigger::EEnterEngagableTrigger(GRuntimeInstance *pRaceActivity)
    : Event(0x10), //
      fRaceActivity(pRaceActivity) {}

EEnterEngagableTrigger::~EEnterEngagableTrigger() {
    IPlayer *player = IPlayer::First(PLAYER_ALL);
    if (player == nullptr || player->GetHud() == nullptr) {
        return;
    }
    IMenuZoneTrigger *izone;
    if (player->GetHud()->QueryInterface(&izone)) {
        izone->EnterTrigger(fRaceActivity);
    }
}

const char *EEnterEngagableTrigger::GetEventName() {
    return "EEnterEngagableTrigger";
}

void EEnterEngagableTrigger_MakeEvent_Callback(const void *staticData) {
    new EEnterEngagableTrigger(static_cast<const EEnterEngagableTrigger::StaticData *>(staticData)->fRaceActivity);
}
