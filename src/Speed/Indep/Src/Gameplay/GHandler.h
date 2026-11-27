#ifndef GAMEPLAY_GHANDLER_H
#define GAMEPLAY_GHANDLER_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include "GRuntimeInstance.h"

struct lua_State;
struct LuaMessageDeliveryInfo;

// A message handler of a state. Scripted handlers are a compiled Lua chunk (the
// bytecode attribute) defining a global function named after the handler's
// collection; it is called as f(activity, message, activityVars).
// total size: 0x2C
class GHandler : public GRuntimeInstance {
  public:
    enum { kType = kGameplayObjType_Handler };

    GHandler(const Attrib::Key &handlerKey);
    ~GHandler() override;

    GameplayObjType GetType() const override;

    void Attach(lua_State *luaState);
    void Detach(lua_State *luaState);
    void NotifyBytecodeFlushed();
    bool IsScripted() const;
    bool MessagePassesFilters(LuaMessageDeliveryInfo *deliveryInfo);
    void HandleMessage(LuaMessageDeliveryInfo *deliveryInfo);

    unsigned int GetMessageID() const;

  private:
    bool BytecodeInVM() const {
        return mAttached;
    }

    void ExecuteScriptedHandler(LuaMessageDeliveryInfo *deliveryInfo);

    bool mAttached; // offset 0x28, size 0x1
};

#endif
