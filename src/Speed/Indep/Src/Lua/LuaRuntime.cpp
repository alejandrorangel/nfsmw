#include "Speed/Indep/Src/Lua/LuaRuntime.h"

#include "Speed/Indep/Libs/Support/Miscellaneous/StringHash.h"
#include "Speed/Indep/Src/Main/Event.h"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include <cstdio>
#include <cstdlib>

// lmem.h (kept out of here: the VM's internal headers define macros that
// clash with game code). The allocation type is an enum (int) there.
typedef int LuaAllocType;
extern "C" void luaM_setallocator(void *(*reallocFunc)(lua_State *, void *, unsigned int, unsigned int, LuaAllocType),
                                  void (*freeFunc)(lua_State *, void *, unsigned int, LuaAllocType));

LuaRuntime *LuaRuntime::mObj = nullptr;

namespace {

#if defined(EA_PORT_NATIVE)
// PC: 0x61E520 / 0x61E550 (LuaRuntime::Realloc / Free) carve the VM's memory
// out of LuaRuntime's pool (small block chains, a large free block map and
// emergency allocations when it runs out). Natively the system heap does it.
// TODO(port): the pool (and with it the heap snapshot/reset) isn't ported.
void *LuaRealloc(lua_State *L, void *block, unsigned int oldsize, unsigned int size, LuaAllocType type) {
    return std::realloc(block, size);
}

void LuaFree(lua_State *L, void *block, unsigned int oldsize, LuaAllocType type) {
    std::free(block);
}
#endif

#if defined(EA_PORT_NATIVE)
int UnportedEventBinding(lua_State *luaState) {
    return 0;
}
#endif

// PC: 0x61B960.
int LuaPanic(lua_State *luaState) {
    LuaRuntime::DumpStack(luaState);
#if defined(EA_PORT_NATIVE)
    std::fprintf(stderr, "[lua] panic: %s\n", lua_tostring(luaState, -1) != nullptr ? lua_tostring(luaState, -1) : "?");
#endif
    return 0;
}

} // namespace

// PC: 0x61F3B0.
LuaRuntime::LuaRuntime(unsigned int memPoolSize)
    : mState(nullptr),          //
      mMemPoolSize(memPoolSize), //
      mMemPoolUsed(0),          //
      mMemPoolBlock(nullptr),   //
      mHeapSnapshot(nullptr),   //
      mHeapSnapshotSize(0),     //
      mHeapSnapshotCompressed(false),
      mNeedToResetHeap(false), //
      mMessagesBeingDelivered(0) {
    for (int i = 0; i < 16; i++) {
        mSmallFreeBlockChain[i] = nullptr;
    }
#if defined(EA_PORT_NATIVE)
    luaM_setallocator(LuaRealloc, LuaFree);
#endif
}

// PS2: 0x3039D8.
LuaRuntime::~LuaRuntime() {
    if (mState != nullptr) {
        lua_close(mState);
        mState = nullptr;
    }
    if (mHeapSnapshot != nullptr) {
        bFree(mHeapSnapshot);
        mHeapSnapshot = nullptr;
    }
}

// PC: 0x61F490.
void LuaRuntime::Init(std::size_t memPoolSize) {
    mObj = new LuaRuntime(static_cast<unsigned int>(memPoolSize));
    mObj->CreateState();
}

// PS2: 0x303B10.
void LuaRuntime::Shutdown() {
    delete mObj;
    mObj = nullptr;
}

// PC: 0x61D250. Undefined globals go through HandleGlobalIndex, which turns
// E* names into event constructors.
void LuaRuntime::CreateState() {
    mState = lua_open();
    lua_atpanic(mState, LuaPanic);
    lua_newtable(mState);
    lua_pushstring(mState, "__index");
    lua_pushcclosure(mState, HandleGlobalIndex, 0);
    lua_settable(mState, -3);
    lua_setmetatable(mState, LUA_GLOBALSINDEX);
    // The PC also raises the GC threshold to 0x40000000 here; the collector
    // is compiled out (lgc.c) anyway.
}

// PC: 0x609F20. Copies (compressing if there's room) the VM's pool so the
// heap can be put back to its post-binding state later.
// TODO(port): no pool natively (see LuaRealloc), so there's nothing to copy.
void LuaRuntime::TakeResetSnapshot() {}

// PS2: 0x304690.
void LuaRuntime::BeginDelivery() {
    mMessagesBeingDelivered++;
}

// PS2: 0x3046A0. A heap reset requested while scripts ran waits until the
// last delivery ends.
void LuaRuntime::EndDelivery() {
    mMessagesBeingDelivered--;
    if (mMessagesBeingDelivered == 0 && mNeedToResetHeap) {
        // TODO(port): ResetHeap (PS2 0x304768) restores the snapshot.
        mNeedToResetHeap = false;
    }
}

// PC: 0x6182F0. Formats every stack slot (the PC's output is compiled out).
void LuaRuntime::DumpStack(lua_State *luaState) {
#if defined(EA_PORT_NATIVE)
    int top = lua_gettop(luaState);
    for (int i = 1; i <= top; i++) {
        int type = lua_type(luaState, i);
        switch (type) {
            case LUA_TSTRING:
                std::fprintf(stderr, "[lua]   %d/-%d: '%s'\n", i, top - i + 1, lua_tostring(luaState, i));
                break;
            case LUA_TNUMBER:
                std::fprintf(stderr, "[lua]   %d/-%d: %.2f\n", i, top - i + 1, static_cast<double>(lua_tonumber(luaState, i)));
                break;
            default:
                std::fprintf(stderr, "[lua]   %d/-%d: %s\n", i, top - i + 1, lua_typename(luaState, type));
                break;
        }
    }
#endif
}

// PC: 0x61B970. __index of the globals table: an E<name> global that isn't
// defined yet is the event's Lua constructor, cached in the globals.
int LuaRuntime::HandleGlobalIndex(lua_State *luaState) {
    const char *name = lua_isstring(luaState, -1) ? lua_tostring(luaState, -1) : nullptr;
    if (name != nullptr && name[0] == 'E') {
        RegisterEvent::LuaBinding binding = RegisterEvent::GetLuaBinding(stringhash32(name));
        if (binding != nullptr) {
            lua_pushcclosure(luaState, binding, 0);
            lua_pushvalue(luaState, -2);
            lua_pushvalue(luaState, -2);
            lua_rawset(luaState, LUA_GLOBALSINDEX);
            return 1;
        }
#if defined(EA_PORT_NATIVE)
        // The event isn't reconstructed: hand the script a constructor that
        // does nothing (on PC every event has one), so the handler carries on.
        std::fprintf(stderr, "[lua] TODO(port): no Lua binding for event %s\n", name);
        lua_pushcclosure(luaState, UnportedEventBinding, 0);
        lua_pushvalue(luaState, -2);
        lua_pushvalue(luaState, -2);
        lua_rawset(luaState, LUA_GLOBALSINDEX);
        return 1;
#endif
    }
    lua_pushnil(luaState);
    return 1;
}
