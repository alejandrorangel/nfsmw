#include "EShowRaceCountdown.hpp"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Generated/Messages/MCountdownDone.h"
#include "Speed/Indep/Src/Interfaces/IFengHud.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Lua/source/lua.h"
#include "Speed/Indep/Src/Sim/Simulation.h"

#include <cstdio>

// PC: ~EShowRaceCountdown 0x631100, EShowRaceCountdown_MakeEvent_Callback
// 0x631190, EShowRaceCountdown_MakeEvent_LuaBinding 0x6311C0. Starts the
// 3-2-1-GO on every player's HUD; the HUD posts MCountdownDone at the end.

#if defined(EA_PORT_NATIVE)
namespace {
float gCountdownStartTime = -1.0f;
}

// TODO(port): the HUD only counts down while the player's view has the drive
// camera (FEngHud::DetermineHudFeatures needs a CM_DRIVE_CUBIC camera mover;
// CameraAI isn't ported, so there is none). When the HUD countdown hasn't
// moved after its 3-2-1, finish the countdown for it so the race starts.
// Called from GManager::Update.
void PortUpdateRaceCountdownFallback() {
    if (gCountdownStartTime < 0.0f || Sim::GetTime() - gCountdownStartTime < 4.0f) {
        return;
    }
    gCountdownStartTime = -1.0f;
    const IPlayer::List &players = IPlayer::GetList(PLAYER_ALL);
    for (IPlayer::List::const_iterator iter = players.begin(); iter != players.end(); ++iter) {
        IHud *hud = (*iter)->GetHud();
        ICountdown *countdown;
        if (hud != nullptr && hud->QueryInterface(&countdown) && countdown->GetSecondsBeforeRaceStart() < 3.0f) {
            return; // the HUD is counting
        }
    }
    std::fprintf(stderr, "[lua] TODO(port): HUD countdown didn't run; posting MCountdownDone\n");
    MCountdownDone().Post(UCrc32(UCRC32_Gameplay));
}
#endif

EShowRaceCountdown::EShowRaceCountdown() : Event(0x10) {}

EShowRaceCountdown::~EShowRaceCountdown() {
    const IPlayer::List &players = IPlayer::GetList(PLAYER_ALL);
    for (IPlayer::List::const_iterator iter = players.begin(); iter != players.end(); ++iter) {
        IHud *hud = (*iter)->GetHud();
        ICountdown *countdown;
        if (hud != nullptr && hud->QueryInterface(&countdown)) {
            countdown->BeginCountdown();
        }
    }
#if defined(EA_PORT_NATIVE)
    gCountdownStartTime = Sim::GetTime();
#endif
}

const char *EShowRaceCountdown::GetEventName() {
    return "EShowRaceCountdown";
}

void EShowRaceCountdown_MakeEvent_Callback(const void *staticData) {
    new EShowRaceCountdown();
}

int EShowRaceCountdown_MakeEvent_LuaBinding(lua_State *luaState) {
    if (lua_gettop(luaState) == 0) {
        new EShowRaceCountdown();
    }
    return 0;
}
