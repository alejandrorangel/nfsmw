#include "EWorldMapOn.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Lua/source/lua.h"

// PC: EWorldMapOn 0x625AD0, EWorldMapOn_MakeEvent_Callback 0x632370,
// EWorldMapOn_MakeEvent_LuaBinding 0x6323C0. The constructor does the work:
// the world map (the HUD's d-pad left) when the game can pause.
EWorldMapOn::EWorldMapOn()
    : Event(0x10) {
    if (FEManager::IsOkayToRequestPauseSimulation(0, true, false)) {
        u32 controlMask = FEngMapJoyportToJoyParam(FEDatabase->GetPlayersJoystickPort(0));
        cFEng::Get()->QueuePackageSwitch("WorldMapMain.fng", 0, controlMask, false);
    }
}

EWorldMapOn::~EWorldMapOn() {}

const char *EWorldMapOn::GetEventName() {
    return "EWorldMapOn";
}

void EWorldMapOn_MakeEvent_Callback(const void *staticData) {
    new EWorldMapOn();
}

int EWorldMapOn_MakeEvent_LuaBinding(lua_State *luaState) {
    if (lua_gettop(luaState) == 0) {
        new EWorldMapOn();
    }
    return 0;
}
