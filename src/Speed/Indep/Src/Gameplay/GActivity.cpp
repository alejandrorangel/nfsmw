#include "Speed/Indep/Src/Gameplay/GActivity.h"

#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GTrigger.h"
#include "Speed/Indep/Src/Generated/Events/EChangeState.hpp"
#include "Speed/Indep/Src/Generated/Messages/MStateEnter.h"
#include "Speed/Indep/Src/Generated/Messages/MStateExit.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifySimTick.h"
#include "Speed/Indep/Src/Lua/LuaBindery.h"
#include "Speed/Indep/Src/Lua/LuaPostOffice.h"
#include "Speed/Indep/Src/Lua/LuaRuntime.h"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#if defined(EA_PORT_NATIVE)
#include <cstdio>
#include <cstdlib>

static bool TraceActivities() {
    static int sTrace = -1;
    if (sTrace < 0) {
        const char *env = std::getenv("NFSMW_TRACE_LUA");
        sTrace = env != nullptr && env[0] != '\0' && env[0] != '0';
    }
    return sTrace != 0;
}
#endif

// PC: 0x5F9BA0.
GActivity::GActivity(const Attrib::Key &activityKey)
    : GRuntimeInstance(activityKey, kGameplayObjType_Activity), //
      mCurrentState(nullptr),                                    //
      mRegisteredHandlersState(nullptr),                         //
      mRunning(false),                                           //
      mVarsInLuaVM(false) {
    DeserializeVars();
}

// PS2: 0x2E18A8.
GActivity::~GActivity() {
    if (mRunning) {
        Suspend();
    }
    UnregisterMessageHandlers();
}

GameplayObjType GActivity::GetType() const {
    return kGameplayObjType_Activity;
}

// PS2: inlined.
const char *GActivity::GetActivityName() const {
    return CollectionName();
}

void GActivity::ClearStateHandlers() {
    mStateHandlers.clear();
}

// PC: 0x5F9270. Finds this activity's states, then each state's handlers.
void GActivity::GatherStatesAndHandlers() {
    ClearStateHandlers();
    for (GObjectIterator<GState> iter(0xffffffff); iter.IsValid(); iter.Advance()) {
        GState *state = iter.GetInstance();
        if (CollectionIsStateForActivity(state)) {
            GHandlerVector &handlerVec = mStateHandlers[state];
            GHandlerVector().swap(handlerVec);
        }
    }
    for (StateToHandlers::iterator iterState = mStateHandlers.begin(); iterState != mStateHandlers.end(); ++iterState) {
        GState *state = iterState->first;
        GHandlerVector &handlerVec = iterState->second;
        handlerVec.reserve(StoreHandlers(state, nullptr));
        StoreHandlers(state, &handlerVec);
    }
}

// PC: 0x5F5B90.
unsigned int GActivity::StoreHandlers(GState *state, GHandlerVector *handlerVec) {
    unsigned int count = 0;
    for (GObjectIterator<GHandler> iter(0xffffffff); iter.IsValid(); iter.Advance()) {
        GHandler *handler = iter.GetInstance();
        if (CollectionIsHandlerForState(state, handler)) {
            if (handlerVec != nullptr) {
                handlerVec->push_back(handler);
            }
            count++;
        }
    }
    return count;
}

// PC: 0x5E2D70. A state belongs to the activity template it refers to.
bool GActivity::CollectionIsStateForActivity(GState *state) {
    Attrib::Key parent = GetParent();
    while (parent != 0) {
        Attrib::Gen::gameplay parentObj(parent, 0, nullptr);
        if (state->templateref().mCollectionKey == parentObj.GetCollection()) {
            return true;
        }
        parent = parentObj.GetParent();
    }
    return false;
}

// PC: 0x5E2E70. A handler belongs to its state and to the activity that owns
// it (or one of the activity's templates, or an activity of the same name).
bool GActivity::CollectionIsHandlerForState(GState *state, GHandler *handler) {
    if (handler->stateref().mCollectionKey != state->GetCollection()) {
        return false;
    }
    Attrib::Key ownerKey = handler->handler_owner().mCollectionKey;
    if (ownerKey == GetCollection()) {
        return true;
    }
    Attrib::Gen::gameplay owner(ownerKey, 0, nullptr);
    if (bStrCmp(owner.CollectionName(), CollectionName()) == 0) {
        return true;
    }
    Attrib::Key key = GetCollection();
    while (key != 0) {
        if (key == ownerKey) {
            return true;
        }
        Attrib::Gen::gameplay obj(key, 0, nullptr);
        key = obj.GetParent();
    }
    return false;
}

// PC: 0x5EE870.
void GActivity::RegisterMessageHandlers(GState *state) {
    if (mRegisteredHandlersState == state) {
        return;
    }
    if (mRegisteredHandlersState != nullptr) {
        UnregisterMessageHandlers();
    }
    StateToHandlers::iterator iterState = mStateHandlers.find(state);
    if (iterState != mStateHandlers.end()) {
        GHandlerVector &handlerVec = iterState->second;
        for (GHandlerVector::iterator iterHandler = handlerVec.begin(); iterHandler != handlerVec.end(); ++iterHandler) {
            LuaPostOffice::Get().RegisterHandler((*iterHandler)->GetMessageID(), this);
        }
    }
    mRegisteredHandlersState = state;
}

// PC: 0x5ED6F0.
void GActivity::UnregisterMessageHandlers() {
    if (mRegisteredHandlersState == nullptr) {
        return;
    }
    StateToHandlers::iterator iterState = mStateHandlers.find(mRegisteredHandlersState);
    if (iterState != mStateHandlers.end() && LuaPostOffice::Exists()) {
        GHandlerVector &handlerVec = iterState->second;
        for (GHandlerVector::iterator iterHandler = handlerVec.begin(); iterHandler != handlerVec.end(); ++iterHandler) {
            LuaPostOffice::Get().UnregisterHandler((*iterHandler)->GetMessageID(), this);
        }
    }
    mRegisteredHandlersState = nullptr;
}

// PC: 0x5E87F0. Enables (or releases) every trigger this activity and its
// children refer to.
void GActivity::ActivateReferencedTriggers(bool activate, GRuntimeInstance *instance) {
    for (unsigned int onConnection = 0; onConnection < instance->GetConnectionCount(); onConnection++) {
        Attrib::Key key = instance->GetConnectionAt(onConnection)->GetCollection();
        GRuntimeInstance *head = GRuntimeInstance::GetRingListHead(kGameplayObjType_Trigger);
        GRuntimeInstance *trigger = head;
        while (trigger != nullptr && trigger->GetCollection() != key) {
            trigger = trigger->GetNextInRing();
            if (trigger == head) {
                trigger = nullptr;
            }
        }
        if (trigger != nullptr) {
            if (activate) {
                static_cast<GTrigger *>(trigger)->AddActivationReference();
            } else {
                static_cast<GTrigger *>(trigger)->RemoveActivationReference();
            }
        }
    }
    for (unsigned int onChild = 0; onChild < instance->Num_Children(); onChild++) {
        GRuntimeInstance *child = GManager::Get().FindInstance(instance->Children(onChild).mCollectionKey);
        if (child != nullptr) {
            ActivateReferencedTriggers(activate, child);
        }
    }
}

// PC: 0x5F93D0. Starts in the state it was in, or queues the change into
// "initial".
void GActivity::Run() {
    if (mRunning) {
        return;
    }
    if (mStateHandlers.empty()) {
        GatherStatesAndHandlers();
    }
    mRunning = true;
#if defined(EA_PORT_NATIVE)
    if (TraceActivities()) {
        std::fprintf(stderr, "[lua] Run %s (%u states)\n", GetActivityName(), static_cast<unsigned int>(mStateHandlers.size()));
    }
#endif
    ActivateReferencedTriggers(true, this);
    if (mCurrentState != nullptr) {
        RegisterMessageHandlers(mCurrentState);
        return;
    }
    GState *initialState = GetStateByName("initial");
    if (initialState != nullptr) {
        new EChangeState(GetCollection(), initialState->GetCollection());
    }
}

// PC: 0x5F4730.
void GActivity::Suspend() {
    if (!mRunning) {
        return;
    }
    mRunning = false;
    ActivateReferencedTriggers(false, this);
    UnregisterMessageHandlers();
    ClearStateHandlers();
}

// PC: 0x5F4780. Stops it and forgets its state and script variables.
void GActivity::Reset() {
    if (mRunning) {
        mRunning = false;
        ActivateReferencedTriggers(false, this);
        UnregisterMessageHandlers();
        ClearStateHandlers();
    }
    if (LuaRuntime::Exists()) {
        ClearActivityVars(LuaRuntime::Get().GetState());
    }
    mVarsInLuaVM = false;
    GManager::Get().ClearObjectStateBlock(GetCollection());
    mCurrentState = nullptr;
}

// PC: 0x5ED750.
GState *GActivity::GetStateByName(const char *stateName) {
    for (StateToHandlers::iterator iterState = mStateHandlers.begin(); iterState != mStateHandlers.end(); ++iterState) {
        GState *state = iterState->first;
        if (bStrCmp(state->Name(), stateName) == 0) {
            return state;
        }
    }
    return nullptr;
}

// PS2: 0x2E2560 (inlined on PC).
void GActivity::EnterStateByName(const char *stateName) {
    EnterState(GetStateByName(stateName));
}

// PC: 0x5FA3C0. Runs the old state's exit handlers and the new one's enter
// handlers.
void GActivity::EnterState(GState *newState) {
    if (!mVarsInLuaVM) {
        DeserializeVars();
    }
    if (newState == mCurrentState) {
        return;
    }
#if defined(EA_PORT_NATIVE)
    if (TraceActivities()) {
        std::fprintf(stderr, "[lua] %s: %s -> %s\n", GetActivityName(), mCurrentState != nullptr ? static_cast<const char *>(mCurrentState->Name()) : "-",
                     newState != nullptr ? static_cast<const char *>(newState->Name()) : "-");
    }
#endif
    if (mCurrentState != nullptr) {
        HandleLocalMessage(MStateExit::_GetKind());
    }
    UnregisterMessageHandlers();
    if (!mVarsInLuaVM) {
        DeserializeVars();
    }
    mCurrentState = newState;
    if (newState != nullptr) {
        RegisterMessageHandlers(newState);
        HandleLocalMessage(MStateEnter::_GetKind());
    }
}

// PC: 0x5ED7D0. ChangeState("name") from a handler; the change is queued.
int GActivity::ChangeStateFromScript(lua_State *luaState) {
    GActivity *activity = static_cast<GActivity *>(lua_touserdata(luaState, lua_upvalueindex(1)));
    const char *stateName = lua_tostring(luaState, 1);
    GState *destState = activity->GetStateByName(stateName != nullptr ? stateName : "");
    if (destState != nullptr) {
        new EChangeState(activity->GetCollection(), destState->GetCollection());
    }
#if defined(EA_PORT_NATIVE)
    else {
        std::fprintf(stderr, "[lua] %s: ChangeState to unknown state '%s'\n", activity->GetActivityName(), stateName != nullptr ? stateName : "");
    }
#endif
    return 0;
}

// PC: 0x5FA1F0. Delivers a message (MStateEnter/MStateExit) only to this
// activity; there's no message table.
void GActivity::HandleLocalMessage(UCrc32 messageType) {
    LuaMessageDeliveryInfo deliveryInfo(messageType, nullptr, nullptr);
    deliveryInfo.SetLuaState(LuaRuntime::Get().GetState());
    HandleMessage(&deliveryInfo);
}

// PC: 0x5F9CC0. The vars live in the registry under the activity's name.
void GActivity::PushActivityVars(lua_State *luaState) {
    if (!mVarsInLuaVM) {
        DeserializeVars();
    }
    const char *activityName = GetActivityName();
    lua_pushstring(luaState, activityName);
    lua_gettable(luaState, LUA_REGISTRYINDEX);
    if (!lua_istable(luaState, -1)) {
        lua_settop(luaState, -2);
        lua_newtable(luaState);
        lua_pushstring(luaState, activityName);
        lua_pushvalue(luaState, -2);
        lua_settable(luaState, LUA_REGISTRYINDEX);
    }
    mVarsInLuaVM = true;
}

// PS2: 0x2E28C8.
void GActivity::ClearActivityVars(lua_State *luaState) {
    lua_pushstring(luaState, GetActivityName());
    lua_pushnil(luaState);
    lua_settable(luaState, LUA_REGISTRYINDEX);
}

// PC: 0x5F9D30. Pushes the activity and its vars and points the ChangeState
// global at this activity; returns the stack top to restore.
int GActivity::BuildActivityTables(lua_State *luaState) {
    int previousTop = lua_gettop(luaState);
    GRuntimeInstance **userdata = static_cast<GRuntimeInstance **>(lua_newuserdata(luaState, sizeof(GRuntimeInstance *)));
    *userdata = this;
    LuaBindery::AttachMetatable(luaState, "GRuntimeInstance");
    PushActivityVars(luaState);
    lua_pushstring(luaState, "ChangeState");
    lua_pushlightuserdata(luaState, this);
    lua_pushcclosure(luaState, ChangeStateFromScript, 1);
    lua_settable(luaState, LUA_GLOBALSINDEX);
    return previousTop;
}

// PC: 0x5F9DA0. Runs the current state's handlers for the message.
void GActivity::HandleMessage(LuaMessageDeliveryInfo *deliveryInfo) {
    lua_State *luaState = deliveryInfo->GetLuaState();
    int luaStackPreviousTop = 0;
    bool activityTablesBuilt = false;
    StateToHandlers::iterator iterState = mStateHandlers.find(mCurrentState);
    if (iterState == mStateHandlers.end()) {
        return;
    }
    GHandlerVector &handlerVec = iterState->second;
    for (GHandlerVector::iterator iterHandler = handlerVec.begin(); iterHandler != handlerVec.end(); ++iterHandler) {
        GHandler *handler = *iterHandler;
        if (handler->GetMessageID() != deliveryInfo->GetMessageKind()) {
            continue;
        }
        deliveryInfo->SetActivityContext(this);
        deliveryInfo->SetHandlerContext(handler);
        if (!handler->MessagePassesFilters(deliveryInfo)) {
            continue;
        }
        deliveryInfo->BuildMessageTable();
        if (!activityTablesBuilt) {
            luaStackPreviousTop = BuildActivityTables(luaState);
            activityTablesBuilt = true;
        }
#if defined(EA_PORT_NATIVE)
        if (TraceActivities() && deliveryInfo->GetMessageKind() != MNotifySimTick::_GetKind().GetValue()) {
            std::fprintf(stderr, "[lua]   %s\n", static_cast<const char *>(handler->CollectionName()));
        }
#endif
        handler->HandleMessage(deliveryInfo);
    }
    if (activityTablesBuilt) {
        lua_settop(luaState, luaStackPreviousTop);
    }
}

// PC: 0x5F9640 (SerializeVars) / 0x5F9480 (DeserializeVars). The current
// state and the script vars are kept in GManager's object state blocks so they
// survive unloading the vault (and saving the game).
// TODO(port): the object state blocks (GManager::AllocObjectStateBlock /
// GetObjectStateBlock) and LuaRuntime::SerializeTable/DeserializeTable aren't
// ported; activities start from scratch every time.
void GActivity::SerializeVars(bool abandonLuaTable) {}

void GActivity::DeserializeVars() {}
