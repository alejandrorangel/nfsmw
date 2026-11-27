#include "EReloadHud.hpp"
#include "Speed/Indep/Libs/Support/Miscellaneous/StringHash.h"
#include "Speed/Indep/Src/EAXSound/EAXSOund.hpp"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Sim/Simulation.h"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

// PC: ~EReloadHud 0x641840, EReloadHud_MakeEvent_Callback 0x630310. Gives
// every player the HUD for the current race (drag or standard, split screen
// halves), and tells the audio the intro/restart NIS is over.

EReloadHud::EReloadHud() : Event(0x10) {}

EReloadHud::~EReloadHud() {
    const IPlayer::List &players = IPlayer::GetList(PLAYER_ALL);
    for (IPlayer::List::const_iterator iter = players.begin(); iter != players.end(); ++iter) {
        IPlayer *player = *iter;
        GRaceStatus *raceStatus = GRaceStatus::Exists() ? &GRaceStatus::Get() : nullptr;
        GRace::Type raceType = raceStatus != nullptr ? raceStatus->GetRaceType() : GRace::kRaceType_None;
        if (Sim::GetUserMode() == Sim::USER_SPLIT_SCREEN) {
            bool drag = raceType == GRace::kRaceType_Drag;
            bool challenge = raceType == GRace::kRaceType_Challenge;
            if (player == *players.begin()) {
                player->SetHud(drag ? PHT_DRAG_SPLIT1 : PHT_SPLIT1);
            } else {
                player->SetHud(drag ? PHT_DRAG_SPLIT2 : PHT_SPLIT2);
            }
            if (!challenge) {
                if (g_pEAXSound != nullptr) g_pEAXSound->QueueNISButtonThrough(bStringHash("IntroNisDDEnd"), 0);
            }
        } else if (raceStatus == nullptr) {
            player->SetHud(PHT_STANDARD);
        } else {
            player->SetHud(raceType == GRace::kRaceType_Drag ? PHT_DRAG : PHT_STANDARD);
            if (raceType != GRace::kRaceType_Challenge && raceType != GRace::kRaceType_None) {
                if (g_pEAXSound != nullptr) g_pEAXSound->QueueNISButtonThrough(bStringHash("RESTART_FAKE"), -1);
            }
        }
    }
}

const char *EReloadHud::GetEventName() {
    return "EReloadHud";
}

void EReloadHud_MakeEvent_Callback(const void *staticData) {
    new EReloadHud();
}
