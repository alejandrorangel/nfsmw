#include "EResetProps.hpp"
#include "Speed/Indep/Src/Lua/source/lua.h"

// Defined in World/WWorld (smackable props).
void World_RestoreProps();

// PC: EResetProps 0x624920, ~EResetProps 0x624930,
// EResetProps_MakeEvent_Callback 0x6305B0. Puts the world's knocked-over props
// back.

EResetProps::EResetProps() : Event(0x8) {}

EResetProps::~EResetProps() {
    World_RestoreProps();
}

const char *EResetProps::GetEventName() {
    return "EResetProps";
}

void EResetProps_MakeEvent_Callback(const void *staticData) {
    new EResetProps();
}

// PC: the generated binding (no arguments).
int EResetProps_MakeEvent_LuaBinding(lua_State *luaState) {
    new EResetProps();
    return 0;
}
