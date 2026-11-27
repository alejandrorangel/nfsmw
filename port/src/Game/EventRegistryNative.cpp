// RegisterEvent::LookupEvent for the native port.
//
// The PC build (LookupEvent 0x633720) searches a generated table of all 161
// events: gEventKeyOrderTable (0x8A5418, event IDs in Eytzinger order) and
// gMakeEventCallbacks (0x8F5F40). Only a few events are reconstructed so far;
// this table lists those. Unknown IDs return null and callers skip them.
#include "Speed/Indep/Src/Generated/Events/EEnterBin.hpp"
#include "Speed/Indep/Src/Generated/Events/EFadeScreenOff.hpp"
#include "Speed/Indep/Src/Generated/Events/EFadeScreenOn.hpp"
#include "Speed/Indep/Src/Generated/Events/EGTriggerInternal.hpp"
#include "Speed/Indep/Src/Generated/Events/ESndGameState.hpp"
#include "Speed/Indep/Src/Generated/Events/ESimulate.hpp"
#include "Speed/Indep/Src/Main/Event.h"
#include "Speed/Indep/Libs/Support/Miscellaneous/StringHash.h"

namespace {

struct EventEntry {
    unsigned int id;
    RegisterEvent::MakeEventCallback make;
};

const EventEntry kEvents[] = {
    {0x522D67DB, ESimulate_MakeEvent_Callback},
    {0xC34649C0, EGTriggerInternal_MakeEvent_Callback},
    {0x5F8C3748, EFadeScreenOn_MakeEvent_Callback},
    {0x77803D76, EFadeScreenOff_MakeEvent_Callback},
    {0x75B0D22B, ESndGameState_MakeEvent_Callback},
    {0x2AF7332B, EEnterBin_MakeEvent_Callback},
    // TODO(port): ECinematicMoment (0x7D2B68BD) needs its name pointers
    // resolved from the event data first.
};

} // namespace

void (*RegisterEvent::LookupEvent(unsigned int tag))(const void *) {
    for (const EventEntry &e : kEvents) {
        if (e.id == tag) {
            return e.make;
        }
    }
    return nullptr;
}

// ResolveEvent (PC 0x633760) uses the same key order table to pick from
// gResolveEventCallbacks (0x8F61C8): 115 of the 161 events have one, which
// turns tag references in the event's static data into pointers. None are
// reconstructed yet (their StaticData would also need PORT_PTR32 fields), so
// event lists in CARP data keep their tags.
// TODO(port): resolvers for the events that get reconstructed.
void (*RegisterEvent::ResolveEvent(unsigned int tag))(void *, const struct UGroup *) {
    return nullptr;
}

// PC: RegisterEvent::GetLuaBinding (0x6337A0) looks the event up in the same
// key order table and returns its gEventLuaBindings entry: the function a
// script calls as E<name>(...) to queue the event. Keys are stringhash32 of the
// event's name (LuaRuntime::HandleGlobalIndex).
// TODO(port): only the bindings of reconstructed events are listed.
struct lua_State;
int EShowRaceCountdown_MakeEvent_LuaBinding(lua_State *luaState);
int EResetProps_MakeEvent_LuaBinding(lua_State *luaState);
int ETerminateMusic_MakeEvent_LuaBinding(lua_State *luaState);
int EEnterBin_MakeEvent_LuaBinding(lua_State *luaState);

namespace {

struct LuaBindingEntry {
    const char *name;
    RegisterEvent::LuaBinding binding;
};

const LuaBindingEntry kLuaBindings[] = {
    {"EShowRaceCountdown", EShowRaceCountdown_MakeEvent_LuaBinding},
    {"EResetProps", EResetProps_MakeEvent_LuaBinding},
    {"ETerminateMusic", ETerminateMusic_MakeEvent_LuaBinding},
    {"EEnterBin", EEnterBin_MakeEvent_LuaBinding},
};

} // namespace

int (*RegisterEvent::GetLuaBinding(unsigned int tag))(lua_State *) {
    for (const LuaBindingEntry &e : kLuaBindings) {
        if (stringhash32(e.name) == tag) {
            return e.binding;
        }
    }
    return nullptr;
}
