#include "EShowRaceOverMessage.hpp"
#include "Speed/Indep/Src/Camera/CameraAI.hpp"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Interfaces/IFengHud.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Sim/Simulation.h"

// PC: EShowRaceOverMessage 0x625160, ~EShowRaceOverMessage 0x631200,
// EShowRaceOverMessage_MakeEvent_Callback 0x6312A0. Shows the "race over"
// HUD message for the player (a totalled player gets the totalled camera).
EShowRaceOverMessage::EShowRaceOverMessage(IPlayer *pPlayer)
    : Event(0x10), //
      fPlayer(pPlayer) {}

EShowRaceOverMessage::~EShowRaceOverMessage() {
    if (!Sim::Exists() || fPlayer == nullptr) {
        return;
    }
    GRacerInfo *info = GRaceStatus::Get().GetRacerInfo(fPlayer->GetSimable());
    if (info != nullptr && info->GetIsTotalled()) {
        CameraAI::MaybeDoTotaledCam(fPlayer);
    }
    IHud *hud = fPlayer->GetHud();
    IRaceOverMessage *message;
    if (hud != nullptr && hud->QueryInterface(&message)) {
        message->RequestRaceOverMessage(fPlayer);
    }
}

const char *EShowRaceOverMessage::GetEventName() {
    return "EShowRaceOverMessage";
}

void EShowRaceOverMessage_MakeEvent_Callback(const void *staticData) {
    // TODO(port): fPlayer is a 32-bit pointer in the event data.
    new EShowRaceOverMessage(nullptr);
}
