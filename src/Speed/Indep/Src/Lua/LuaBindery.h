#ifndef LUA_LUABINDERY_H
#define LUA_LUABINDERY_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include "Speed/Indep/Src/Lua/source/lua.h"

class GRuntimeInstance;
class ISimable;

// Binds the game functions the scripts call (Game.*, RaceStatus:*, ...).
// Game objects are passed to Lua as userdata: GRuntimeInstances hold the
// pointer (metatable "GRuntimeInstance"), simables hold their instance handle
// (metatable "ISimable").
// total size: 0x1
class LuaBindery {
  public:
    static void Init();
    static void Shutdown();

    void BindToGameCode(lua_State *luaState);

    static LuaBindery &Get() {
        return *fObj;
    }

    static bool Exists() {
        return fObj != nullptr;
    }

    static void LoadMetatable(lua_State *luaState, const char *className);
    static void AttachMetatable(lua_State *luaState, const char *className);
    static void GetGlobalTable(lua_State *luaState, const char *tableName);

    static void PushRuntimeInstance(lua_State *luaState, GRuntimeInstance *instance);
    static void PushSimable(lua_State *luaState, ISimable *simable);
    static GRuntimeInstance *ToRuntimeInstance(lua_State *luaState, int index);
    static ISimable *ToSimable(lua_State *luaState, int index);

  private:
    static LuaBindery *fObj;
};

#endif
