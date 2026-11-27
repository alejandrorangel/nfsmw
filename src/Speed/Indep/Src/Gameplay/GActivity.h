#ifndef GAMEPLAY_GACTIVITY_H
#define GAMEPLAY_GACTIVITY_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include "GHandler.h"
#include "GState.h"
#include "Speed/Indep/Libs/Support/Utility/UCrc.h"
#include "Speed/Indep/Libs/Support/Utility/UStandard.h"

struct lua_State;
struct LuaMessageDeliveryInfo;

DECLARE_CONTAINER_TYPE(ID_GHandlerVector);
DECLARE_CONTAINER_TYPE(ID_StateToVectors);

typedef UTL::Std::vector<GHandler *, _type_ID_GHandlerVector> GHandlerVector;
typedef UTL::Std::map<GState *, GHandlerVector, _type_ID_StateToVectors> StateToHandlers;

// A scripted state machine (a race, a bin, an engage trigger...). While it runs
// it listens, through LuaPostOffice, for the messages its current state's
// handlers want and runs them in the Lua VM. Its script variables live in a
// table in the Lua registry named after the activity.
// total size: 0x48
class GActivity : public GRuntimeInstance {
  public:
    enum { kType = kGameplayObjType_Activity };

    // total size: 0x8
    struct SerializedHeader {
        unsigned int mStateNameHash; // offset 0x0, size 0x4
        unsigned short mFlags;       // offset 0x4, size 0x2
        unsigned short mTableBytes;  // offset 0x6, size 0x2
    };

    GActivity(const Attrib::Key &activityKey);
    ~GActivity() override;

    GameplayObjType GetType() const override;

    const char *GetActivityName() const;
    void HandleMessage(LuaMessageDeliveryInfo *deliveryInfo);
    void PushActivityVars(lua_State *luaState);
    void ClearActivityVars(lua_State *luaState);
    GState *GetStateByName(const char *stateName);
    void EnterState(GState *newState);
    void Run();
    void Suspend();
    void Reset();
    void SerializeVars(bool abandonLuaTable);

    bool GetIsRunning() const {
        return mRunning;
    }

    GState *GetCurrentState() const {
        return mCurrentState;
    }

    static int ChangeStateFromScript(lua_State *luaState);

  private:
    void GatherStatesAndHandlers();
    unsigned int StoreHandlers(GState *state, GHandlerVector *handlerVec);
    void RegisterMessageHandlers(GState *state);
    void UnregisterMessageHandlers();
    bool CollectionIsStateForActivity(GState *state);
    bool CollectionIsHandlerForState(GState *state, GHandler *handler);
    void EnterStateByName(const char *stateName);
    void HandleLocalMessage(UCrc32 messageType);
    void ActivateReferencedTriggers(bool activate, GRuntimeInstance *instance);
    int BuildActivityTables(lua_State *luaState);
    void DeserializeVars();
    void ClearStateHandlers();

    GState *mCurrentState;            // offset 0x28, size 0x4
    GState *mRegisteredHandlersState; // offset 0x2C, size 0x4
    StateToHandlers mStateHandlers;   // offset 0x30, size 0x10
    bool mRunning;                    // offset 0x40, size 0x1
    bool mVarsInLuaVM;                // offset 0x44, size 0x1
};

#endif
