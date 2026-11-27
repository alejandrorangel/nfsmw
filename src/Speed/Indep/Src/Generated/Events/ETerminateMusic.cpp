#include "ETerminateMusic.hpp"
#include "Speed/Indep/Libs/Support/Miscellaneous/StringHash.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyMusicFlow.h"
#include "Speed/Indep/Src/Lua/source/lua.h"

// PC: ~ETerminateMusic 0x625660, ETerminateMusic_MakeEvent_Callback 0x631EB0.
// Tells the music flow to stop (MNotifyMusicFlow on the "Terminate" port).
ETerminateMusic::ETerminateMusic() : Event(0x10) {}

ETerminateMusic::~ETerminateMusic() {
    MNotifyMusicFlow(0).Send(UCrc32(stringhash32("Terminate")));
}

const char *ETerminateMusic::GetEventName() {
    return "ETerminateMusic";
}

void ETerminateMusic_MakeEvent_Callback(const void *staticData) {
    new ETerminateMusic();
}

// The generated binding (no arguments).
int ETerminateMusic_MakeEvent_LuaBinding(lua_State *luaState) {
    new ETerminateMusic();
    return 0;
}
