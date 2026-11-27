#include "Speed/Indep/Src/Gameplay/GHandler.h"

#include "Speed/Indep/Src/Lua/LuaPostOffice.h"
#include "Speed/Indep/Src/Lua/LuaRuntime.h"
#include "Speed/Indep/Src/Lua/source/lauxlib.h"
#include "Speed/Indep/Src/Misc/LZCompress.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include <cstdio>
#include <cstdlib>

// PS2: 0x2EEC48.
GHandler::GHandler(const Attrib::Key &handlerKey) : GRuntimeInstance(handlerKey, kGameplayObjType_Handler), mAttached(false) {}

// PS2: 0x2EEC88.
GHandler::~GHandler() {}

GameplayObjType GHandler::GetType() const {
    return kGameplayObjType_Handler;
}

// The message kind this handler is for.
unsigned int GHandler::GetMessageID() const {
    return message_id();
}

// PS2: inlined.
bool GHandler::IsScripted() const {
    return GetAttributePointer(0x9a4a020a, 0) != nullptr; // bytecode
}

// PC: 0x5DD2E0. Runs the handler's chunk once, which defines its function.
// The bytecode is usually compressed.
void GHandler::Attach(lua_State *luaState) {
    if (mAttached) {
        return;
    }
    const Attrib::Blob *bytecode = static_cast<const Attrib::Blob *>(GetAttributePointer(0x9a4a020a, 0)); // bytecode
    if (bytecode == nullptr || bytecode->GetSize() == 0) {
        return;
    }
    uint8 *data = static_cast<uint8 *>(const_cast<void *>(bytecode->GetData()));
    unsigned int size = bytecode->GetSize();
    uint8 *decompressed = nullptr;
    if (LZValidHeader(reinterpret_cast<LZHeader *>(data))) {
        size = reinterpret_cast<LZHeader *>(data)->UncompressedSize;
        decompressed = static_cast<uint8 *>(bMalloc(static_cast<int>(size + 0x10), 0));
        LZDecompress(data, decompressed);
        data = decompressed;
    }
    int result = luaL_loadbuffer(luaState, reinterpret_cast<const char *>(data), size, "Handler");
    if (result == 0) {
        result = lua_pcall(luaState, 0, 0, 0);
    }
    if (result != 0) {
#if defined(EA_PORT_NATIVE)
        std::fprintf(stderr, "[lua] %s: %s\n", static_cast<const char *>(CollectionName()), lua_tostring(luaState, -1));
#endif
        lua_settop(luaState, -2);
    }
    if (decompressed != nullptr) {
        bFree(decompressed);
    }
    if (result == 0) {
        mAttached = true;
    }
}

// PC: 0x5DD3C0 (guess). Drops the handler's function from the VM.
void GHandler::Detach(lua_State *luaState) {
    if (!mAttached) {
        return;
    }
    lua_pushstring(luaState, CollectionName());
    lua_pushnil(luaState);
    lua_settable(luaState, LUA_GLOBALSINDEX);
    mAttached = false;
}

// PC: 0x5DD400. The Lua heap was reset; the function has to be loaded again.
void GHandler::NotifyBytecodeFlushed() {
    mAttached = false;
}

// PC: 0x5DD410. Each FilterBlocks entry is a compiled query run against the
// message; FilterModePassAll says whether every filter or any one must pass.
bool GHandler::MessagePassesFilters(LuaMessageDeliveryInfo *deliveryInfo) {
    unsigned int numFilters = Num_FilterBlocks();
    if (numFilters == 0) {
        return true;
    }
    // TODO(port): the filter blocks hold Query functions (Query::LookupQueryFunc,
    // PC 0x633840) evaluated on the message fields; none are reconstructed, so
    // every filter passes.
#if defined(EA_PORT_NATIVE)
    static bool sWarned = false;
    if (!sWarned) {
        sWarned = true;
        std::fprintf(stderr, "[lua] TODO(port): message filters not evaluated (%s)\n", static_cast<const char *>(CollectionName()));
    }
#endif
    return true;
}

// PC: 0x5DD520. Called with the message table, the activity and its vars on
// the stack (pushed by GActivity::HandleMessage).
void GHandler::HandleMessage(LuaMessageDeliveryInfo *deliveryInfo) {
    lua_State *luaState = deliveryInfo->GetLuaState();
    Attach(luaState);
    ExecuteScriptedHandler(deliveryInfo);
}

// PC: inlined in HandleMessage. f(activity, message, vars).
void GHandler::ExecuteScriptedHandler(LuaMessageDeliveryInfo *deliveryInfo) {
    lua_State *luaState = deliveryInfo->GetLuaState();
    lua_pushstring(luaState, CollectionName());
    lua_gettable(luaState, LUA_GLOBALSINDEX);
    lua_pushvalue(luaState, -3);
    lua_pushvalue(luaState, -5);
    lua_pushvalue(luaState, -4);
#if defined(EA_PORT_NATIVE)
    // The PC calls unprotected (an error ends in LuaPanic); report the error
    // and carry on instead.
    if (lua_pcall(luaState, 3, 0, 0) != 0) {
        std::fprintf(stderr, "[lua] error in %s: %s\n", static_cast<const char *>(CollectionName()), lua_tostring(luaState, -1));
        lua_settop(luaState, -2);
    }
#else
    lua_call(luaState, 3, 0);
#endif
}
