// RegisterEvent::LookupEvent for the native port.
//
// The PC build (LookupEvent 0x633720) searches a generated table of all 161
// events: gEventKeyOrderTable (0x8A5418, event IDs in Eytzinger order) and
// gMakeEventCallbacks (0x8F5F40). Only a few events are reconstructed so far;
// this table lists those. Unknown IDs return null and callers skip them.
#include "Speed/Indep/Src/Generated/Events/ESimulate.hpp"
#include "Speed/Indep/Src/Main/Event.h"

namespace {

struct EventEntry {
    unsigned int id;
    RegisterEvent::MakeEventCallback make;
};

const EventEntry kEvents[] = {
    {0x522D67DB, ESimulate_MakeEvent_Callback},
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
