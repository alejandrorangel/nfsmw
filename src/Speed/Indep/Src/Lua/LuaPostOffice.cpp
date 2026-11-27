#include "Speed/Indep/Src/Lua/LuaPostOffice.h"

#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Generated/Messages/MAcceptEnterCareerEvent.h"
#include "Speed/Indep/Src/Generated/Messages/MCountdownDone.h"
#include "Speed/Indep/Src/Generated/Messages/MDeclineEnterCareerEvent.h"
#include "Speed/Indep/Src/Generated/Messages/MEnterFreeRoam.h"
#include "Speed/Indep/Src/Generated/Messages/MEnterSafeHouse.h"
#include "Speed/Indep/Src/Generated/Messages/MEnteringGameplay.h"
#include "Speed/Indep/Src/Generated/Messages/MFlowReadyForOutro.h"
#include "Speed/Indep/Src/Generated/Messages/MICECameraFinished.h"
#include "Speed/Indep/Src/Generated/Messages/MLoadingComplete.h"
#include "Speed/Indep/Src/Generated/Messages/MNISComplete.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyCellCallComplete.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyCellCallStarted.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyChallengePassed.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyEngineBlown.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyFinished.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyKnockedOut.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyMessageDone.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyMilestoneProgress.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyMilestoneReached.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyMovieFinished.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyOnlineRaceOver.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyPlayerRep.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyRaceAbandoned.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyRacePlacement.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyRaceTime.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyRaceTimeExpired.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyRaceTimeSecTick.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifySimTick.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifySpeedTrap.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyTimer.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyVehicleDestroyed.h"
#include "Speed/Indep/Src/Generated/Messages/MPerpBusted.h"
#include "Speed/Indep/Src/Generated/Messages/MPerpEscaped.h"
#include "Speed/Indep/Src/Generated/Messages/MRestartRace.h"
#include "Speed/Indep/Src/Generated/Messages/MTriggerEnter.h"
#include "Speed/Indep/Src/Generated/Messages/MTriggerExit.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Lua/LuaBindery.h"
#include "Speed/Indep/Src/Lua/LuaRuntime.h"

#include <algorithm>

LuaPostOffice *LuaPostOffice::fObj = nullptr;

// ---------------------------------------------------------------------------
// LuaMessageDeliveryInfo
// ---------------------------------------------------------------------------

// PC: 0x5DAE20.
LuaMessageDeliveryInfo::LuaMessageDeliveryInfo(UCrc32 messageKind, const Hermes::Message *message, BuildMessageTableFunc buildTableFunc)
    : mMessageKind(messageKind),      //
      mMessageBase(message),          //
      mBuildTableFunc(buildTableFunc), //
      mLuaTableBuilt(false),          //
      mLuaState(nullptr),             //
      mActivityContext(nullptr),      //
      mHandlerContext(nullptr) {
    LuaRuntime::Get().BeginDelivery();
}

// PC: 0x5DAEC0.
LuaMessageDeliveryInfo::~LuaMessageDeliveryInfo() {
    if (mLuaTableBuilt) {
        lua_settop(mLuaState, -2);
    }
    LuaRuntime::Get().EndDelivery();
}

// PC: 0x609FE0. A message without fields gets nil.
void LuaMessageDeliveryInfo::BuildMessageTable() {
    if (mLuaTableBuilt) {
        return;
    }
    if (mBuildTableFunc != nullptr) {
        mBuildTableFunc(mLuaState, mMessageBase);
    } else {
        lua_pushnil(mLuaState);
    }
    mLuaTableBuilt = true;
}

// ---------------------------------------------------------------------------
// Message tables (PC: one BuildMessageTable per message, e.g. 0x625260)
// ---------------------------------------------------------------------------

namespace {

void SetNumber(lua_State *L, const char *name, float value) {
    lua_pushstring(L, name);
    lua_pushnumber(L, static_cast<lua_Number>(value));
    lua_settable(L, -3);
}

void SetBool(lua_State *L, const char *name, bool value) {
    lua_pushstring(L, name);
    lua_pushboolean(L, value);
    lua_settable(L, -3);
}

void SetString(lua_State *L, const char *name, const char *value) {
    lua_pushstring(L, name);
    lua_pushstring(L, value != nullptr ? value : "");
    lua_settable(L, -3);
}

void SetInstance(lua_State *L, const char *name, GCollectionKey key) {
    lua_pushstring(L, name);
    LuaBindery::PushRuntimeInstance(L, GManager::Get().FindInstance(key.mCollectionKey));
    lua_settable(L, -3);
}

void SetSimable(lua_State *L, const char *name, HSIMABLE handle) {
    lua_pushstring(L, name);
    LuaBindery::PushSimable(L, handle != 0 ? ISimable::FindInstance(handle) : nullptr);
    lua_settable(L, -3);
}

// The fields of each message type the scripts get; a message without fields
// gets an empty table.
template <typename MessageT> void FillMessageTable(lua_State *L, const MessageT &msg) {}

template <> void FillMessageTable<MEnterSafeHouse>(lua_State *L, const MEnterSafeHouse &msg) {
    SetString(L, "ZoneType", msg.GetZoneType());
}
template <> void FillMessageTable<MNISComplete>(lua_State *L, const MNISComplete &msg) {
    SetString(L, "NISName", msg.GetNISName());
}
template <> void FillMessageTable<MNotifyChallengePassed>(lua_State *L, const MNotifyChallengePassed &msg) {
    SetString(L, "ChallengeName", msg.GetChallengeName());
}
template <> void FillMessageTable<MNotifyEngineBlown>(lua_State *L, const MNotifyEngineBlown &msg) {
    SetSimable(L, "Racer", msg.GetRacer());
}
template <> void FillMessageTable<MNotifyFinished>(lua_State *L, const MNotifyFinished &msg) {
    SetInstance(L, "Sender", msg.GetSender());
}
template <> void FillMessageTable<MNotifyKnockedOut>(lua_State *L, const MNotifyKnockedOut &msg) {
    SetSimable(L, "Racer", msg.GetRacer());
}
template <> void FillMessageTable<MNotifyMilestoneProgress>(lua_State *L, const MNotifyMilestoneProgress &msg) {
    SetString(L, "MilestoneName", msg.GetMilestoneName());
    SetNumber(L, "ValueReached", msg.GetValueReached());
}
template <> void FillMessageTable<MNotifyMilestoneReached>(lua_State *L, const MNotifyMilestoneReached &msg) {
    SetString(L, "MilestoneName", msg.GetMilestoneName());
    SetNumber(L, "ValueReached", msg.GetValueReached());
}
template <> void FillMessageTable<MNotifyOnlineRaceOver>(lua_State *L, const MNotifyOnlineRaceOver &msg) {
    SetBool(L, "ShowPostRace", msg.GetShowPostRace());
}
template <> void FillMessageTable<MNotifyPlayerRep>(lua_State *L, const MNotifyPlayerRep &msg) {
    SetSimable(L, "Racer", msg.GetRacer());
    SetNumber(L, "RepPoints", static_cast<float>(msg.GetRepPoints()));
}
template <> void FillMessageTable<MNotifyRacePlacement>(lua_State *L, const MNotifyRacePlacement &msg) {
    SetInstance(L, "Activity", msg.GetActivity());
    SetSimable(L, "Racer", msg.GetRacer());
    SetNumber(L, "Placing", static_cast<float>(msg.GetPlacing()));
}
template <> void FillMessageTable<MNotifyRaceTime>(lua_State *L, const MNotifyRaceTime &msg) {
    SetNumber(L, "TimeElapsed", msg.GetTimeElapsed());
    SetBool(L, "TimeIsLimited", msg.GetTimeIsLimited());
    SetNumber(L, "TimeRemaining", msg.GetTimeRemaining());
}
template <> void FillMessageTable<MNotifyRaceTimeSecTick>(lua_State *L, const MNotifyRaceTimeSecTick &msg) {
    SetNumber(L, "TimeElapsed", msg.GetTimeElapsed());
}
template <> void FillMessageTable<MNotifySimTick>(lua_State *L, const MNotifySimTick &msg) {
    SetNumber(L, "SimTime", msg.GetSimTime());
    SetNumber(L, "TimeStep", msg.GetTimeStep());
}
template <> void FillMessageTable<MNotifySpeedTrap>(lua_State *L, const MNotifySpeedTrap &msg) {
    SetInstance(L, "TrapActivity", msg.GetTrapActivity());
    SetSimable(L, "Racer", msg.GetRacer());
    SetNumber(L, "SpeedKmh", msg.GetSpeedKmh());
}
template <> void FillMessageTable<MNotifyTimer>(lua_State *L, const MNotifyTimer &msg) {
    SetString(L, "Name", msg.GetName());
}
template <> void FillMessageTable<MNotifyVehicleDestroyed>(lua_State *L, const MNotifyVehicleDestroyed &msg) {
    SetSimable(L, "Racer", msg.GetRacer());
}
template <> void FillMessageTable<MPerpBusted>(lua_State *L, const MPerpBusted &msg) {
    SetSimable(L, "Perpetrator", msg.GetPerpetrator());
}
template <> void FillMessageTable<MPerpEscaped>(lua_State *L, const MPerpEscaped &msg) {
    SetSimable(L, "Perpetrator", msg.GetPerpetrator());
}
template <> void FillMessageTable<MTriggerEnter>(lua_State *L, const MTriggerEnter &msg) {
    SetInstance(L, "Sender", msg.GetSender());
    SetSimable(L, "Element", msg.GetElement());
}
template <> void FillMessageTable<MTriggerExit>(lua_State *L, const MTriggerExit &msg) {
    SetInstance(L, "Sender", msg.GetSender());
    SetSimable(L, "Element", msg.GetElement());
}

template <typename MessageT> void BuildMessageTable(lua_State *L, const Hermes::Message *message) {
    lua_newtable(L);
    FillMessageTable<MessageT>(L, *static_cast<const MessageT *>(message));
}

// Receives the bound messages on the Gameplay port.
struct LuaMessageRouter {
    template <typename MessageT> void Route(const MessageT &message) {
        LuaPostOffice::DeliverMessage(message);
    }
};

LuaMessageRouter gLuaMessageRouter;

template <typename MessageT> Hermes::HHANDLER BindMessage() {
    return Hermes::Handler::Create<MessageT, LuaMessageRouter, LuaMessageRouter>(&gLuaMessageRouter, &LuaMessageRouter::Route<MessageT>,
                                                                              UCrc32(UCRC32_Gameplay), 0);
}

} // namespace

template <typename MessageT> void LuaPostOffice::DeliverMessage(const MessageT &message) {
    if (fObj == nullptr || !LuaRuntime::Exists()) {
        return;
    }
    LuaMessageDeliveryInfo deliveryInfo(MessageT::_GetKind(), &message, BuildMessageTable<MessageT>);
    fObj->RouteMessage(&deliveryInfo);
}

// ---------------------------------------------------------------------------
// LuaPostOffice
// ---------------------------------------------------------------------------

// PC: 0x61F8A0. EventManager::BindMessagesToLua (0x648590) makes a Gameplay
// port handler (unfiltered by ID) for each of the 65 message types the scripts
// can handle.
// TODO(port): only the messages the vault's handlers listen for are bound.
LuaPostOffice::LuaPostOffice() {
    mBindingHandlers.push_back(BindMessage<MAcceptEnterCareerEvent>());
    mBindingHandlers.push_back(BindMessage<MCountdownDone>());
    mBindingHandlers.push_back(BindMessage<MDeclineEnterCareerEvent>());
    mBindingHandlers.push_back(BindMessage<MEnterFreeRoam>());
    mBindingHandlers.push_back(BindMessage<MEnteringGameplay>());
    mBindingHandlers.push_back(BindMessage<MEnterSafeHouse>());
    mBindingHandlers.push_back(BindMessage<MFlowReadyForOutro>());
    mBindingHandlers.push_back(BindMessage<MICECameraFinished>());
    mBindingHandlers.push_back(BindMessage<MLoadingComplete>());
    mBindingHandlers.push_back(BindMessage<MNISComplete>());
    mBindingHandlers.push_back(BindMessage<MNotifyCellCallComplete>());
    mBindingHandlers.push_back(BindMessage<MNotifyCellCallStarted>());
    mBindingHandlers.push_back(BindMessage<MNotifyChallengePassed>());
    mBindingHandlers.push_back(BindMessage<MNotifyEngineBlown>());
    mBindingHandlers.push_back(BindMessage<MNotifyFinished>());
    mBindingHandlers.push_back(BindMessage<MNotifyKnockedOut>());
    mBindingHandlers.push_back(BindMessage<MNotifyMessageDone>());
    mBindingHandlers.push_back(BindMessage<MNotifyMilestoneProgress>());
    mBindingHandlers.push_back(BindMessage<MNotifyMilestoneReached>());
    mBindingHandlers.push_back(BindMessage<MNotifyMovieFinished>());
    mBindingHandlers.push_back(BindMessage<MNotifyOnlineRaceOver>());
    mBindingHandlers.push_back(BindMessage<MNotifyPlayerRep>());
    mBindingHandlers.push_back(BindMessage<MNotifyRaceAbandoned>());
    mBindingHandlers.push_back(BindMessage<MNotifyRacePlacement>());
    mBindingHandlers.push_back(BindMessage<MNotifyRaceTime>());
    mBindingHandlers.push_back(BindMessage<MNotifyRaceTimeExpired>());
    mBindingHandlers.push_back(BindMessage<MNotifyRaceTimeSecTick>());
    mBindingHandlers.push_back(BindMessage<MNotifySimTick>());
    mBindingHandlers.push_back(BindMessage<MNotifySpeedTrap>());
    mBindingHandlers.push_back(BindMessage<MNotifyTimer>());
    mBindingHandlers.push_back(BindMessage<MNotifyVehicleDestroyed>());
    mBindingHandlers.push_back(BindMessage<MPerpBusted>());
    mBindingHandlers.push_back(BindMessage<MPerpEscaped>());
    mBindingHandlers.push_back(BindMessage<MRestartRace>());
    mBindingHandlers.push_back(BindMessage<MTriggerEnter>());
    mBindingHandlers.push_back(BindMessage<MTriggerExit>());
    for (unsigned int i = 0; i < mBindingHandlers.size(); i++) {
        Hermes::Handler::SetIDFilter(mBindingHandlers[i], false);
    }
}

// PS2: 0x305790.
LuaPostOffice::~LuaPostOffice() {
    for (unsigned int i = 0; i < mBindingHandlers.size(); i++) {
        Hermes::Handler::Destroy(mBindingHandlers[i]);
    }
    mBindingHandlers.clear();
}

// PC: 0x61F940.
void LuaPostOffice::Init() {
    fObj = new LuaPostOffice;
}

// PS2: 0x305908.
void LuaPostOffice::Shutdown() {
    delete fObj;
    fObj = nullptr;
}

// PC: 0x61FC10. Hands the message to a copy of the subscriber list (handlers
// may change state, and so the subscriptions, as they run).
void LuaPostOffice::RouteMessage(LuaMessageDeliveryInfo *deliveryInfo) {
    UTL::Std::map<unsigned int, ActivityList, _type_ID_LuaMessageSubscriberMap>::iterator iter = mSubscribers.find(deliveryInfo->GetMessageKind());
    if (iter == mSubscribers.end() || iter->second.empty()) {
        return;
    }
    ActivityList activities(iter->second);
    deliveryInfo->SetLuaState(LuaRuntime::Get().GetState());
    for (ActivityList::iterator iterActivity = activities.begin(); iterActivity != activities.end(); ++iterActivity) {
        (*iterActivity)->HandleMessage(deliveryInfo);
    }
}

// PC: 0x61FD00.
void LuaPostOffice::RegisterHandler(unsigned int messageID, GActivity *activity) {
    ActivityList &activities = mSubscribers[messageID];
    if (std::find(activities.begin(), activities.end(), activity) == activities.end()) {
        activities.push_back(activity);
    }
}

// PC: 0x61FD40.
void LuaPostOffice::UnregisterHandler(unsigned int messageID, GActivity *activity) {
    ActivityList &activities = mSubscribers[messageID];
    activities.erase(std::remove(activities.begin(), activities.end(), activity), activities.end());
}
