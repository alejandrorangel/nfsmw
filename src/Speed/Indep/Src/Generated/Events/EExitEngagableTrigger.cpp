#include "EExitEngagableTrigger.hpp"
#include "Speed/Indep/Src/Interfaces/IFengHud.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"

// PC: EExitEngagableTrigger 0x621C40 (vtable 0x8A6B90), ~EExitEngagableTrigger
// 0x640870, EExitEngagableTrigger_MakeEvent_Callback 0x62C5D0. Hides the first
// player's engage prompt.
EExitEngagableTrigger::EExitEngagableTrigger(GRuntimeInstance *pRaceActivity)
    : Event(0x10), //
      fRaceActivity(pRaceActivity) {}

EExitEngagableTrigger::~EExitEngagableTrigger() {
    IPlayer *player = IPlayer::First(PLAYER_ALL);
    if (player == nullptr || player->GetHud() == nullptr) {
        return;
    }
    IMenuZoneTrigger *izone;
    if (player->GetHud()->QueryInterface(&izone)) {
        izone->ExitTrigger();
    }
}

const char *EExitEngagableTrigger::GetEventName() {
    return "EExitEngagableTrigger";
}

void EExitEngagableTrigger_MakeEvent_Callback(const void *staticData) {
    new EExitEngagableTrigger(static_cast<const EExitEngagableTrigger::StaticData *>(staticData)->fRaceActivity);
}
