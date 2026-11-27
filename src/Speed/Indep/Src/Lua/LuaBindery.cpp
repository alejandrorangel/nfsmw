#include "Speed/Indep/Src/Lua/LuaBindery.h"

#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Gameplay/GRuntimeInstance.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Lua/LuaGameHooks.h"

#include <new>
#if defined(EA_PORT_NATIVE)
#include <cstdio>
#include <cstdlib>
#endif

LuaBindery *LuaBindery::fObj = nullptr;

// PC: 0x6040C0.
void LuaBindery::Init() {
    fObj = new LuaBindery;
}

// PS2: 0x305FC8.
void LuaBindery::Shutdown() {
    delete fObj;
    fObj = nullptr;
}

// PC: 0x618970 (with 0x615970). Leaves the class's metatable (created on
// first use, registry[className], with __index pointing at itself) on the
// stack.
void LuaBindery::LoadMetatable(lua_State *luaState, const char *className) {
    lua_pushstring(luaState, className);
    lua_rawget(luaState, LUA_REGISTRYINDEX);
    if (!lua_isnil(luaState, -1)) {
        return;
    }
    lua_settop(luaState, -2);
    lua_newtable(luaState);
    lua_pushstring(luaState, className);
    lua_pushvalue(luaState, -2);
    lua_rawset(luaState, LUA_REGISTRYINDEX);
    lua_pushstring(luaState, "__index");
    lua_pushvalue(luaState, -2);
    lua_settable(luaState, -3);
}

// PC: 0x6189D0. Sets the class metatable on the value on top of the stack.
void LuaBindery::AttachMetatable(lua_State *luaState, const char *className) {
    lua_pushstring(luaState, className);
    lua_rawget(luaState, LUA_REGISTRYINDEX);
    lua_setmetatable(luaState, -2);
}

// PC: 0x618A40. Leaves globals[tableName] on the stack, creating it.
void LuaBindery::GetGlobalTable(lua_State *luaState, const char *tableName) {
    lua_pushstring(luaState, tableName);
    lua_gettable(luaState, LUA_GLOBALSINDEX);
    if (lua_isnil(luaState, -1)) {
        lua_settop(luaState, -2);
        lua_newtable(luaState);
        lua_pushstring(luaState, tableName);
        lua_pushvalue(luaState, -2);
        lua_settable(luaState, LUA_GLOBALSINDEX);
    }
}

// PC: 0x618780.
void LuaBindery::PushRuntimeInstance(lua_State *luaState, GRuntimeInstance *instance) {
    if (instance == nullptr) {
        lua_pushnil(luaState);
        return;
    }
    GRuntimeInstance **userdata = static_cast<GRuntimeInstance **>(lua_newuserdata(luaState, sizeof(GRuntimeInstance *)));
    *userdata = instance;
    AttachMetatable(luaState, "GRuntimeInstance");
}

// PC: 0x618850. The handle, not the pointer, so a dead simable reads as nil.
void LuaBindery::PushSimable(lua_State *luaState, ISimable *simable) {
    if (simable == nullptr) {
        lua_pushnil(luaState);
        return;
    }
    HSIMABLE *userdata = static_cast<HSIMABLE *>(lua_newuserdata(luaState, sizeof(HSIMABLE)));
    *userdata = simable->GetInstanceHandle();
    AttachMetatable(luaState, "ISimable");
}

// PC: inlined in every thunk (e.g. 0x60CFD0).
GRuntimeInstance *LuaBindery::ToRuntimeInstance(lua_State *luaState, int index) {
    if (index > lua_gettop(luaState)) {
        return nullptr;
    }
    GRuntimeInstance **userdata = static_cast<GRuntimeInstance **>(lua_touserdata(luaState, index));
    return userdata != nullptr ? *userdata : nullptr;
}

// PC: inlined in every thunk (e.g. 0x6102D0).
ISimable *LuaBindery::ToSimable(lua_State *luaState, int index) {
    if (index > lua_gettop(luaState)) {
        return nullptr;
    }
    HSIMABLE *userdata = static_cast<HSIMABLE *>(lua_touserdata(luaState, index));
    if (userdata == nullptr || *userdata == 0) {
        return nullptr;
    }
    return ISimable::FindInstance(*userdata);
}

namespace {

// Argument and result marshalling for the thunks (the PC has one binder per
// signature, 0x61A270..0x61D1D0).
template <typename T> struct LuaArg;

template <> struct LuaArg<int> {
    static int Get(lua_State *L, int i) {
        return static_cast<int>(lua_tonumber(L, i));
    }
    static void Push(lua_State *L, int v) {
        lua_pushnumber(L, static_cast<lua_Number>(v));
    }
};

template <> struct LuaArg<float> {
    static float Get(lua_State *L, int i) {
        return static_cast<float>(lua_tonumber(L, i));
    }
    static void Push(lua_State *L, float v) {
        lua_pushnumber(L, static_cast<lua_Number>(v));
    }
};

template <> struct LuaArg<bool> {
    static bool Get(lua_State *L, int i) {
        return lua_toboolean(L, i) != 0;
    }
    static void Push(lua_State *L, bool v) {
        lua_pushboolean(L, v);
    }
};

template <> struct LuaArg<const char *> {
    static const char *Get(lua_State *L, int i) {
        return lua_tostring(L, i);
    }
};

template <> struct LuaArg<GRuntimeInstance *> {
    static GRuntimeInstance *Get(lua_State *L, int i) {
        return LuaBindery::ToRuntimeInstance(L, i);
    }
    static void Push(lua_State *L, GRuntimeInstance *v) {
        LuaBindery::PushRuntimeInstance(L, v);
    }
};

template <> struct LuaArg<ISimable *> {
    static ISimable *Get(lua_State *L, int i) {
        return LuaBindery::ToSimable(L, i);
    }
    static void Push(lua_State *L, ISimable *v) {
        LuaBindery::PushSimable(L, v);
    }
};

template <typename T> struct Decay {
    typedef T Type;
};

// Calls fn with the Lua arguments from 'first' on; returns the Lua results.
template <typename R, typename... Args> struct Caller {
    template <typename F, int... I> struct Seq {};

    template <typename F, std::size_t... I> static int Call(lua_State *L, F fn, int first, std::index_sequence<I...>) {
        R result = fn(LuaArg<Args>::Get(L, first + static_cast<int>(I))...);
        LuaArg<R>::Push(L, result);
        return 1;
    }
};

template <typename... Args> struct Caller<void, Args...> {
    template <typename F, std::size_t... I> static int Call(lua_State *L, F fn, int first, std::index_sequence<I...>) {
        fn(LuaArg<Args>::Get(L, first + static_cast<int>(I))...);
        return 0;
    }
};

// Free function: the function pointer is the closure's light userdata upvalue.
#if defined(EA_PORT_NATIVE)
// NFSMW_TRACE_LUA=2 logs every game function a script calls (the closure's
// second upvalue is its "Table.Name") with its first result.
bool TraceHookCalls() {
    static int sTrace = -1;
    if (sTrace < 0) {
        const char *env = std::getenv("NFSMW_TRACE_LUA");
        sTrace = env != nullptr && std::atoi(env) >= 2 ? 1 : 0;
    }
    return sTrace != 0;
}

void TraceHookCall(lua_State *L, int results) {
    const char *name = lua_tostring(L, lua_upvalueindex(2));
    char args[128] = "";
    int argCount = lua_gettop(L) - results;
    for (int i = 1, used = 0; i <= argCount && used < static_cast<int>(sizeof(args)) - 16; i++) {
        if (lua_type(L, i) == LUA_TNUMBER) {
            used += std::snprintf(args + used, sizeof(args) - used, "%s%g", i > 1 ? ", " : "", lua_tonumber(L, i));
        } else if (lua_type(L, i) == LUA_TSTRING) {
            used += std::snprintf(args + used, sizeof(args) - used, "%s\"%.24s\"", i > 1 ? ", " : "", lua_tostring(L, i));
        } else {
            used += std::snprintf(args + used, sizeof(args) - used, "%s%s", i > 1 ? ", " : "", lua_typename(L, lua_type(L, i)));
        }
    }
    if (results > 0 && lua_isnumber(L, -1)) {
        std::fprintf(stderr, "[lua] call %s(%s) -> %g\n", name, args, lua_tonumber(L, -1));
    } else if (results > 0 && lua_isboolean(L, -1)) {
        std::fprintf(stderr, "[lua] call %s(%s) -> %s\n", name, args, lua_toboolean(L, -1) ? "true" : "false");
    } else {
        std::fprintf(stderr, "[lua] call %s(%s)\n", name, args);
    }
}
#endif

template <typename R, typename... Args> int FunctionThunk(lua_State *L) {
    typedef R (*Fn)(Args...);
    Fn fn = reinterpret_cast<Fn>(lua_touserdata(L, lua_upvalueindex(1)));
#if defined(EA_PORT_NATIVE)
    int results = Caller<R, Args...>::Call(L, fn, 1, std::index_sequence_for<Args...>());
    if (TraceHookCalls()) {
        TraceHookCall(L, results);
    }
    return results;
#else
    return Caller<R, Args...>::Call(L, fn, 1, std::index_sequence_for<Args...>());
#endif
}

// Method: the member function pointer is in the closure's userdata upvalue and
// the object is the first argument (obj:Method(...)).
template <typename C, typename R, typename... Args> struct MethodHolder {
    R (C::*mMethod)(Args...);
};

template <typename C, typename R, typename... Args> int MethodThunk(lua_State *L) {
    MethodHolder<C, R, Args...> *holder = static_cast<MethodHolder<C, R, Args...> *>(lua_touserdata(L, lua_upvalueindex(1)));
    C **self = static_cast<C **>(lua_touserdata(L, 1));
    if (self == nullptr || *self == nullptr) {
        return 0;
    }
    C *obj = *self;
    auto fn = [holder, obj](Args... args) -> R { return (obj->*(holder->mMethod))(args...); };
    return Caller<R, Args...>::Call(L, fn, 2, std::index_sequence_for<Args...>());
}

// ISimable methods take the handle userdata as self.
template <typename R> int SimableMethodThunk(lua_State *L) {
    typedef R (ISimable::*Method)() const;
    Method *method = static_cast<Method *>(lua_touserdata(L, lua_upvalueindex(1)));
    ISimable *isim = LuaBindery::ToSimable(L, 1);
    if (isim == nullptr) {
        lua_pushnil(L);
        return 1;
    }
    LuaArg<R>::Push(L, (isim->**method)());
    return 1;
}

// PC: 0x61A270 and the other BindFunction/BindVoidFunction binders.
template <typename R, typename... Args> void BindFunction(lua_State *L, const char *tableName, const char *name, R (*fn)(Args...)) {
    LuaBindery::GetGlobalTable(L, tableName);
    lua_pushstring(L, name);
    lua_pushlightuserdata(L, reinterpret_cast<void *>(fn));
#if defined(EA_PORT_NATIVE)
    lua_pushstring(L, tableName);
    lua_pushstring(L, ".");
    lua_pushstring(L, name);
    lua_concat(L, 3);
    lua_pushcclosure(L, FunctionThunk<R, Args...>, 2);
#else
    lua_pushcclosure(L, FunctionThunk<R, Args...>, 1);
#endif
    lua_settable(L, -3);
    lua_settop(L, -2);
}

// PC: 0x61BA60.
void BindRawFunction(lua_State *L, const char *tableName, const char *name, lua_CFunction fn) {
    LuaBindery::GetGlobalTable(L, tableName);
    lua_pushstring(L, name);
    lua_pushcclosure(L, fn, 0);
    lua_settable(L, -3);
    lua_settop(L, -2);
}

// PC: 0x61AE80 etc. The method goes into the class metatable.
template <typename C, typename R, typename... Args> void BindMethod(lua_State *L, const char *className, const char *name, R (C::*method)(Args...)) {
    LuaBindery::LoadMetatable(L, className);
    lua_pushstring(L, name);
    MethodHolder<C, R, Args...> *holder = static_cast<MethodHolder<C, R, Args...> *>(lua_newuserdata(L, sizeof(MethodHolder<C, R, Args...>)));
    holder->mMethod = method;
    lua_pushcclosure(L, MethodThunk<C, R, Args...>, 1);
    lua_settable(L, -3);
    lua_settop(L, -2);
}

template <typename R> void BindSimableMethod(lua_State *L, const char *name, R (ISimable::*method)() const) {
    typedef R (ISimable::*Method)() const;
    LuaBindery::LoadMetatable(L, "ISimable");
    lua_pushstring(L, name);
    Method *holder = static_cast<Method *>(lua_newuserdata(L, sizeof(Method)));
    *holder = method;
    lua_pushcclosure(L, SimableMethodThunk<R>, 1);
    lua_settable(L, -3);
    lua_settop(L, -2);
}

// PC: 0x61ADF0. globals[name] = the object, with its class metatable.
template <typename C> void BindObject(lua_State *L, const char *name, const char *className, C *object) {
    lua_pushstring(L, name);
    C **userdata = static_cast<C **>(lua_newuserdata(L, sizeof(C *)));
    *userdata = object;
    LuaBindery::LoadMetatable(L, className);
    lua_setmetatable(L, -2);
    lua_settable(L, LUA_GLOBALSINDEX);
}

} // namespace

// PC: 0x61E750.
void LuaBindery::BindToGameCode(lua_State *L) {
    BindRawFunction(L, "Table", "Randomize", Table_Randomize);
    BindFunction(L, "Activity", "Run", Activity_Run);
    BindFunction(L, "Activity", "Suspend", Activity_Suspend);
    BindFunction(L, "Audio", "SetFlag", Audio_SetFlag);
    BindFunction(L, "Audio", "IsCopSpeechPlaying", Audio_IsCopSpeechPlaying);
    BindFunction(L, "MiniMap", "AddEngagedRace", MiniMap_AddEngagedRace);
    BindFunction(L, "Bin", "GetNumChallengesPassed", Bin_GetNumChallengesPassed);
    BindFunction(L, "Bin", "GetNumRacesWon", Bin_GetNumRacesWon);
    BindFunction(L, "Math", "RandomInt", Math_RandomInt);
    BindFunction(L, "Camera", "SetGenericCamera", Camera_SetGenericCamera);
    BindFunction(L, "NIS", "Play", NIS_Play);
    BindFunction(L, "Movie", "PlayHackE3FMV", Movie_PlayHackE3FMV);
    BindFunction(L, "Debug", "Print", Debug_Print);
    BindFunction(L, "Debug", "PrintInstance", Debug_PrintInstance);
    BindFunction(L, "Debug", "ShowScreenMessage", Debug_ShowScreenMessage);
    BindFunction(L, "Demo", "SetRaceCompleteForFE", Demo_SetRaceCompleteForFE);
    BindFunction(L, "Demo", "StorePursuitRepForFE", Demo_StorePursuitRepForFE);
    BindFunction(L, "HUD", "ShowMessage", HUD_ShowMessage);
    BindFunction(L, "HUD", "ShowTimeExtension", HUD_ShowTimeExtension);
    BindFunction(L, "Game", "SabotageEngine", Game_SabotageEngine);
    BindFunction(L, "Game", "BlowEngine", Game_BlowEngine);
    BindFunction(L, "Game", "DetachCameraFromRacer", Game_DetachCameraFromRacer);
    BindFunction(L, "Game", "ChallengeComplete", Game_ChallengeComplete);
    BindFunction(L, "Game", "ClearAIControl", Game_ClearAIControl);
    BindFunction(L, "Game", "ForceAIControl", Game_ForceAIControl);
    BindFunction(L, "Game", "InitRacers", Game_InitRacers);
    BindFunction(L, "Game", "WarpPlayerToTrigger", Game_WarpPlayerToTrigger);
    BindFunction(L, "Game", "ResetTrigger", Game_ResetTrigger);
    BindFunction(L, "Game", "SetPlayerStartPosition", Game_SetPlayerStartPosition);
    BindFunction(L, "Game", "SetRacerGoal", Game_SetRacerGoal);
    BindFunction(L, "Game", "SetRacerLapsLeft", Game_SetRacerLapsLeft);
    BindFunction(L, "Game", "KnockoutRacer", Game_KnockoutRacer);
    BindFunction(L, "Game", "TotalRacer", Game_KnockoutRacer);
    BindFunction(L, "Game", "NotifyRacePlacement", Game_NotifyRacePlacement);
    BindFunction(L, "Game", "NotifySpeedTrapTriggered", Game_NotifySpeedTrapTriggered);
    BindFunction(L, "Game", "NotifyCheckpointReached", Game_NotifyCheckpointReached);
    BindFunction(L, "Game", "NotifyLapFinished", Game_NotifyLapFinished);
    BindFunction(L, "Game", "NotifyRaceFinished", Game_NotifyRaceFinished);
    BindFunction(L, "Game", "SetRaceActivity", Game_SetRaceActivity);
    BindFunction(L, "Game", "StartRace", Game_StartRace);
    BindFunction(L, "Game", "StartRaceTimers", Game_StartRaceTimers);
    BindFunction(L, "Game", "SaveStartPositions", Game_SaveStartPositions);
    BindFunction(L, "Game", "RestoreStartPositions", Game_RestoreStartPositions);
    BindFunction(L, "Game", "EnterPostRaceFlow", Game_EnterPostRaceFlow);
    BindFunction(L, "Game", "ShowOnlinePostRaceScreen", FE_ShowOnlinePostRaceScreen);
    BindFunction(L, "Game", "SetCopsEnabled", Game_SetCopsEnabled);
    BindFunction(L, "Game", "NoNewPursuitsOrCops", Game_NoNewPursuitsOrCops);
    BindFunction(L, "Game", "ForcePursuitStart", Game_ForcePursuitStart);
    BindFunction(L, "Game", "EnterEngagableTrigger", Game_EnterEngagableTrigger);
    BindFunction(L, "Game", "ExitEngagableTrigger", Game_ExitEngagableTrigger);
    BindFunction(L, "Game", "EnterGateZone", Game_EnterGateZone);
    BindFunction(L, "Game", "ExitGateZone", Game_ExitGateZone);
    BindFunction(L, "Game", "ShowRaceOverSummary", Game_ShowRaceOverSummary);
    BindFunction(L, "Game", "HideRaceOverSummary", Game_HideRaceOverSummary);
    BindFunction(L, "Game", "SetTrafficSpeed", Game_SetTrafficSpeed);
    BindFunction(L, "Game", "SpawnCop", Game_SpawnCop);
    BindFunction(L, "Game", "SpawnCharacter", Game_SpawnCharacter);
    BindFunction(L, "Game", "UnspawnCharacter", Game_UnspawnCharacter);
    BindFunction(L, "Game", "SendCharacterStimulus", Game_SendCharacterStimulus);
    BindFunction(L, "Game", "JackKnife", Game_JackKnife);
    BindFunction(L, "Game", "SetAllStaging", Game_SetAllStaging);
    BindFunction(L, "Game", "ShowPauseMenu", Game_ShowPauseMenu);
    BindFunction(L, "Game", "AwardCash", Game_AwardCash);
    BindFunction(L, "Game", "AwardPoints", Game_AwardPoints);
    BindFunction(L, "Game", "ChallengeCompleted", Game_ChallengeCompleted);
    BindFunction(L, "Game", "UnlockRace", Game_UnlockRace);
    BindFunction(L, "Game", "NotifyCountdownDone", Game_NotifyCountdownDone);
    BindFunction(L, "Game", "ResetCopsForRestart", Game_ResetCopsForRestart);
    BindFunction(L, "Game", "ShowTriggerIcon", Game_ShowTriggerIcon);
    BindFunction(L, "Game", "HideTriggerIcon", Game_HideTriggerIcon);
    BindFunction(L, "Game", "WarpToMarkerWhenRoaming", Game_WarpToMarkerWhenRoaming);
    BindFunction(L, "Game", "DoSpecialFinalization", Game_DoSpecialFinalization);
    BindFunction(L, "Game", "IsActiveSpeedTrap", Game_IsActiveSpeedTrap);
    BindFunction(L, "Game", "IsActiveMenuGate", Game_IsActiveMenuGate);
    BindFunction(L, "Game", "GetSimTime", Game_GetSimTime);
    BindFunction(L, "Game", "GetRacerIndex", Game_GetRacerIndex);
    BindFunction(L, "Game", "GetRacerElement", Game_GetRacerElement);
    BindFunction(L, "Game", "GetPlayerElement", Game_GetPlayerElement);
    BindFunction(L, "Game", "GetRacerCharacter", Game_GetRacerCharacter);
    BindFunction(L, "Game", "GetNumRacers", Game_GetNumRacers);
    BindFunction(L, "Game", "GetSimableSpeedKmh", Game_GetSimableSpeedKmh);
    BindFunction(L, "Game", "RacerIsHuman", Game_RacerIsHuman);
    BindFunction(L, "Game", "PlayerIsLocal", Game_PlayerIsLocal);
    BindFunction(L, "Game", "IsRaceCompleted", Game_IsRaceCompleted);
    BindFunction(L, "Game", "IsRaceUnlocked", Game_IsRaceUnlocked);
    BindFunction(L, "Game", "IsOnlineGame", Game_IsOnlineGame);
    BindFunction(L, "Game", "IsLANGame", Game_IsLANGame);
    BindFunction(L, "Game", "AllRacersDone", Game_AllRacersDone);
    BindFunction(L, "Game", "AllHumanPlayersDone", Game_AllHumanPlayersDone);
    BindFunction(L, "Game", "SimableDistance", Game_SimableDistance);
    BindFunction(L, "Game", "SimableAngle", Game_SimableAngle);
    BindFunction(L, "Game", "AllowEngageEvents", Game_AllowEngageEvents);
    BindFunction(L, "Game", "AllowMenuGates", Game_AllowMenuGates);
    BindFunction(L, "Game", "AllowEngageSafehouse", Game_AllowEngageSafehouse);
    BindFunction(L, "Game", "DoSpecialSetup", Game_DoSpecialSetup);
    BindFunction(L, "Platform", "IsNextGen", Platform_IsNextGen);

    BindObject(L, "RaceStatus", "GRaceStatus", &GRaceStatus::Get());
    BindMethod(L, "GRaceStatus", "ClearRacers", &GRaceStatus::ClearRacers);
    BindMethod(L, "GRaceStatus", "AddRacer", &GRaceStatus::AddRacer);
    BindMethod(L, "GRaceStatus", "AddAvailableEventToMap", &GRaceStatus::AddAvailableEventToMap);
    BindMethod(L, "GRaceStatus", "AddSpeedTrapToMap", &GRaceStatus::AddSpeedTrapToMap);
    BindMethod(L, "GRaceStatus", "ClearCheckpoints", &GRaceStatus::ClearCheckpoints);
    BindMethod(L, "GRaceStatus", "AddCheckpoint", &GRaceStatus::AddCheckpoint);
    BindMethod(L, "GRaceStatus", "SetNextCheckpointPos", &GRaceStatus::SetNextCheckpointPos);
    BindMethod(L, "GRaceStatus", "SetRoaming", &GRaceStatus::SetRoaming);
    BindMethod(L, "GRaceStatus", "SetIsLoading", &GRaceStatus::SetIsLoading);
    BindMethod(L, "GRaceStatus", "EnterSuddenDeath", &GRaceStatus::EnterSuddenDeath);
    BindMethod(L, "GRaceStatus", "AwardBonusTime", &GRaceStatus::AwardBonusTime);
    BindMethod(L, "GRaceStatus", "SetActivelyRacing", &GRaceStatus::SetActivelyRacing);
    BindMethod(L, "GRaceStatus", "SetTaskTime", &GRaceStatus::SetTaskTime);
    BindMethod(L, "GRaceStatus", "DisableBarriers", &GRaceStatus::DisableBarriers);
    BindMethod(L, "GRaceStatus", "EnableBinBarriers", &GRaceStatus::EnableBinBarriers);
    BindMethod(L, "GRaceStatus", "NotifyScriptWhenLoaded", &GRaceStatus::NotifyScriptWhenLoaded);
    BindMethod(L, "GRaceStatus", "SetHasBeenWon", &GRaceStatus::SetHasBeenWon);

    BindFunction(L, "Game", "GetPlayerBounty", Game_GetPlayerBounty);
    BindFunction(L, "Game", "AwardPlayerBounty", Game_AwardPlayerBounty);
    BindFunction(L, "Game", "JumpToCarLot", Game_JumpToCarLot);
    BindFunction(L, "Game", "AbandonRace", Game_AbandonRace);
    BindFunction(L, "Game", "JumpToSafeHouse", Game_JumpToSafeHouse);
    BindSimableMethod(L, "IsPlayer", &ISimable::IsPlayer);
    BindFunction(L, "Game", "SetTimer", Game_SetTimer);
    BindFunction(L, "Game", "KillTimer", Game_KillTimer);
    BindFunction(L, "Game", "ShowGPS", Game_ShowGPS);
    BindFunction(L, "Game", "NavigatePlayerTo", Game_NavigatePlayerTo);
    BindFunction(L, "Debug", "Assert", Debug_Assert);
    BindFunction(L, "Game", "NotifyActivityFinsihed", Game_NotifyFinished);
    BindFunction(L, "Game", "SkipCareerIntro", Game_SkipCareerIntro);
    BindFunction(L, "Game", "SetChanceOfRain", Game_SetChanceOfRain);
    BindFunction(L, "Game", "Fade", Game_DoFade);
    BindFunction(L, "Game", "IntroduceRival", Game_IntroduceRival);
    BindFunction(L, "Game", "PlayTutorial", Game_PlayTutorial);
    BindFunction(L, "Game", "DoSafeHouseIntro", Game_DoSafeHouseIntro);
    BindFunction(L, "Game", "DoZoneMenuAction", Game_DoZoneMenuAction);
    BindFunction(L, "Game", "SetCurrentTimeOfDay", Game_SetCurrentTimeOfDay);
    BindFunction(L, "Game", "ReloadWorld", Game_ReloadWorld);
    BindFunction(L, "Game", "IsCareerMode", Game_IsCareerMode);
    BindFunction(L, "Game", "IsSplitScreen", Game_IsSplitScreen);
    BindFunction(L, "Game", "SetHasRapSheet", Game_SetHasRapSheet);
    BindFunction(L, "Game", "SetWorldHeat", Game_SetWorldHeat);
    BindFunction(L, "Game", "ShowWinningPostRaceScreen", FE_ShowWinningPostRaceScreen);
    BindFunction(L, "Game", "ShowLosingPostRaceScreen", FE_ShowLosingPostRaceScreen);
    BindFunction(L, "Game", "PreventPlayerBeingBusted", Game_PreventPlayerBeingBusted);
    BindFunction(L, "Game", "CalculateRanking", Game_CalculateRanking);
}
