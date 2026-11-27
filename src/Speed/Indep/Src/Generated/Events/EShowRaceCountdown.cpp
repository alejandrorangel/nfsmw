#include "EShowRaceCountdown.hpp"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Interfaces/IFengHud.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Lua/source/lua.h"
#include "Speed/Indep/Src/Sim/Simulation.h"

// PC: ~EShowRaceCountdown 0x631100, EShowRaceCountdown_MakeEvent_Callback
// 0x631190, EShowRaceCountdown_MakeEvent_LuaBinding 0x6311C0. Starts the
// 3-2-1-GO on every player's HUD; the HUD posts MCountdownDone at the end.

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
