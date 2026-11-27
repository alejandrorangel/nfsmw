#include "EAddSMS.hpp"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Lua/source/lua.h"

// PC: EAddSMS (inlined, vtable 0x8A6728), ~EAddSMS 0x620290,
// EAddSMS_MakeEvent_Callback 0x62A6D0. The scripts' way to send the player an
// SMS: GManager queues it (or calls it in, for the cell calls).
EAddSMS::EAddSMS(int pNumber)
    : Event(0x10), //
      fNumber(pNumber) {}

EAddSMS::~EAddSMS() {
    if (GManager::Exists()) {
        GManager::Get().AddSMS(fNumber);
    }
}

const char *EAddSMS::GetEventName() {
    return "EAddSMS";
}

void EAddSMS_MakeEvent_Callback(const void *staticData) {
    new EAddSMS(static_cast<const EAddSMS::StaticData *>(staticData)->fNumber);
}

// The script binding (the PC table gEventLuaBindings): EAddSMS(number).
int EAddSMS_MakeEvent_LuaBinding(lua_State *luaState) {
    if (lua_gettop(luaState) == 1) {
        new EAddSMS(static_cast<int>(lua_tonumber(luaState, 1)));
    }
    return 0;
}
