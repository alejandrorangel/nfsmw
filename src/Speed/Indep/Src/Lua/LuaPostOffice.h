#ifndef LUA_LUAPOSTOFFICE_H
#define LUA_LUAPOSTOFFICE_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include "Speed/Indep/Libs/Support/Utility/UStandard.h"
#include "Speed/Indep/Src/Gameplay/GActivity.h"
#include "Speed/Indep/Src/Misc/Hermes.h"
#include "Speed/Indep/Src/Lua/source/lua.h"

DECLARE_CONTAINER_TYPE(ID_LuaActivityList);
DECLARE_CONTAINER_TYPE(ID_LuaMessageSubscriberMap);

// Pushes a Lua table with the fields of a message (one per message type).
typedef void (*BuildMessageTableFunc)(lua_State *luaState, const Hermes::Message *message);

// One message on its way to the scripts. The message table is built lazily,
// the first time a handler that wants the message is found, and popped again
// when the delivery ends.
// total size: 0x40
struct LuaMessageDeliveryInfo {
    LuaMessageDeliveryInfo(UCrc32 messageKind, const Hermes::Message *message, BuildMessageTableFunc buildTableFunc);
    ~LuaMessageDeliveryInfo();

    void BuildMessageTable();

    unsigned int GetMessageKind() const {
        return mMessageKind.GetValue();
    }

    void SetLuaState(lua_State *luaState) {
        mLuaState = luaState;
    }

    void SetActivityContext(GActivity *activity) {
        mActivityContext = activity;
    }

    void SetHandlerContext(GHandler *handler) {
        mHandlerContext = handler;
    }

    lua_State *GetLuaState() const {
        return mLuaState;
    }

    GActivity *GetActivity() const {
        return mActivityContext;
    }

    GHandler *GetHandler() const {
        return mHandlerContext;
    }

    const Hermes::Message *GetMessage() const {
        return mMessageBase;
    }

  private:
    UCrc32 mMessageKind;                   // offset 0x20, size 0x4
    const Hermes::Message *mMessageBase;   // offset 0x24, size 0x4
    BuildMessageTableFunc mBuildTableFunc; // offset 0x28, size 0x4
    bool mLuaTableBuilt;                   // offset 0x2C, size 0x1
    lua_State *mLuaState;                  // offset 0x30, size 0x4
    GActivity *mActivityContext;           // offset 0x34, size 0x4
    GHandler *mHandlerContext;             // offset 0x38, size 0x4
};

// Routes the gameplay messages the scripts listen for to the activities whose
// current state has a handler for them.
// total size: 0x20
class LuaPostOffice {
  public:
    static void Init();
    static void Shutdown();

    static LuaPostOffice &Get() {
        return *fObj;
    }

    static bool Exists() {
        return fObj != nullptr;
    }

    void RouteMessage(LuaMessageDeliveryInfo *deliveryInfo);
    void RegisterHandler(unsigned int messageID, GActivity *activity);
    void UnregisterHandler(unsigned int messageID, GActivity *activity);

    // Delivers a message of type MessageT (see LuaPostOffice.cpp).
    template <typename MessageT> static void DeliverMessage(const MessageT &message);

  private:
    LuaPostOffice();
    ~LuaPostOffice();

    typedef UTL::Std::vector<GActivity *, _type_ID_LuaActivityList> ActivityList;

    static LuaPostOffice *fObj;

    UTL::Std::map<unsigned int, ActivityList, _type_ID_LuaMessageSubscriberMap> mSubscribers; // offset 0x0, size 0x10
    UTL::Std::vector<Hermes::HHANDLER, Hermes::_type_ID_HermesHandlerVector> mBindingHandlers; // offset 0x10, size 0x10
};

#endif
