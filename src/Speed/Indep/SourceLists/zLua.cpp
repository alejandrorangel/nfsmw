// TODO remove
#ifdef _MSC_VER
#pragma warning(disable : 4716)
#endif

extern "C" {

#include "Speed/Indep/Src/Lua/source/lapi.c"

#include "Speed/Indep/Src/Lua/source/ldebug.c"

#include "Speed/Indep/Src/Lua/source/ldo.c"

#include "Speed/Indep/Src/Lua/source/ldump.c"

#include "Speed/Indep/Src/Lua/source/lfunc.c"

#include "Speed/Indep/Src/Lua/source/lmem.c"

#include "Speed/Indep/Src/Lua/source/lobject.c"

#include "Speed/Indep/Src/Lua/source/lstate.c"

#include "Speed/Indep/Src/Lua/source/lstring.c"

#include "Speed/Indep/Src/Lua/source/ltable.c"

#include "Speed/Indep/Src/Lua/source/ltm.c"

#include "Speed/Indep/Src/Lua/source/lundump.c"

#include "Speed/Indep/Src/Lua/source/lvm.c"

#include "Speed/Indep/Src/Lua/source/lzio.c"

#include "Speed/Indep/Src/Lua/source/lib/lauxlib.c"

#include "Speed/Indep/Src/Lua/source/llex.c"

#include "Speed/Indep/Src/Lua/source/lparser.c"

#include "Speed/Indep/Src/Lua/source/lgc.c"

#if defined(EA_PORT_NATIVE)
// The size of a full userdata (the PC reads Udata::len directly in
// LuaRuntime's table serializer, 0x617A80).
size_t PortLua_UserdataSize(lua_State *L, int index) {
    void *p = lua_touserdata(L, index);
    return p != NULL && !lua_islightuserdata(L, index) ? (reinterpret_cast<Udata *>(p) - 1)->uv.len : 0;
}
#endif
}
