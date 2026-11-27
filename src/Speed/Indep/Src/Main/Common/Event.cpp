#include "Speed/Indep/Src/Main/Event.h"
#include <cstring>
#include "Speed/Indep/Libs/Support/Miscellaneous/CARP.h"
#include "Speed/Indep/Src/Misc/Hermes.h"
#include "Speed/Indep/Src/Misc/Profiler.hpp"
#include "Speed/Indep/bWare/Inc/Strings.hpp"
#include "Speed/Indep/bWare/Inc/bMemory.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

static char *gMemoryBuffer = nullptr;
static char *gCreationPoint = nullptr;
static char *gDeletionPoint = nullptr;
static unsigned int gHighWaterMem = 0;
#if defined(EA_PORT_NATIVE)
static const size_t kNativeEventHeader = 0x10;
#endif

Event *EventManager::fgCurrentEvent = nullptr;
bool EventManager::fgHaltCurrentList = false;

void EventManager::Init() {
#if defined(EA_PORT_NATIVE)
    // PC (0x633EC0) uses a 256KB buffer; 64-bit events are also larger.
    gMemoryBuffer = (char *)bMalloc(0x40000, GetVirtualMemoryAllocParams());
#else
    gMemoryBuffer = (char *)bMalloc(0x4000, GetVirtualMemoryAllocParams());
#endif
    EventManager::Reset();
}

void EventManager::Reset() {
    gDeletionPoint = gMemoryBuffer;
    gCreationPoint = gMemoryBuffer;
    gHighWaterMem = 0;
}

void EventManager::RunEvents() {
    ProfileNode profile_node("EventManager::RunEvents", 0);

    while (EventManager::EventsQueued()) {
#if defined(EA_PORT_NATIVE)
        // Step over the event here rather than in operator delete, so an event
        // whose constructor or destructor is still a link stub (never built,
        // or never freed) is skipped instead of stalling the queue.
        char *next = gDeletionPoint + *reinterpret_cast<size_t *>(gDeletionPoint);
        EventManager::fgCurrentEvent = reinterpret_cast<Event *>(gDeletionPoint + kNativeEventHeader);
        if (*reinterpret_cast<void **>(EventManager::fgCurrentEvent) != nullptr) {
            delete EventManager::fgCurrentEvent;
        }
        EventManager::fgCurrentEvent = nullptr;
        gDeletionPoint = next;
#else
        EventManager::fgCurrentEvent = reinterpret_cast<Event *>(gDeletionPoint);
        if (EventManager::fgCurrentEvent != nullptr) {
            delete EventManager::fgCurrentEvent;
        }

        EventManager::fgCurrentEvent = nullptr;
#endif
    }

    gDeletionPoint = gMemoryBuffer;
    gCreationPoint = gMemoryBuffer;
}

void EventManager::FireEventList(const CARP::EventList *eventList, bool verbose) {
    bool haltPrevList = EventManager::fgHaltCurrentList;
    const CARP::EventStaticData *event = eventList->Event();

    EventManager::fgHaltCurrentList = false;

    for (int i = 0; i < eventList->fNumEvents && !EventManager::fgHaltCurrentList; i++, event++) {
        void (*callback)(const void *) = RegisterEvent::LookupEvent(event->fEventID);
        if (callback == nullptr) continue;

        callback(event->StaticData());
    }

    EventManager::fgHaltCurrentList = haltPrevList;
}

void EventManager::FireOneEvent(const CARP::EventList *eventList, unsigned int eventToFire, bool verbose) {
    const CARP::EventStaticData *event = eventList->Event();

    for (int i = 0; i < eventList->fNumEvents; i++, event++) {
        if (i == eventToFire) {
            void (*callback)(const void *) = RegisterEvent::LookupEvent(event->fEventID);
            if (callback == nullptr) break;

            callback(event->StaticData());
            break;
        }
    }
}

bool EventManager::ListHasEvent(const CARP::EventList *eventList, unsigned int eventID, const CARP::EventStaticData **foundEvent) {
    const CARP::EventStaticData *event = eventList->Event();

    for (int i = 0; i < eventList->fNumEvents; i++, event++) {
        if (eventID == event->fEventID) {
            if (foundEvent != nullptr) {
                *foundEvent = event;
            }
            return true;
        }
    }

    return false;
}

bool EventManager::EventsQueued() {
    return gDeletionPoint < gCreationPoint;
}

template<>
unsigned int GetEmbeddedObjectSize<const char>(const char *ptr) {
    return bStrLen(ptr) + 1;
}

template<>
unsigned int GetEmbeddedObjectSize<Hermes::Message>(Hermes::Message *ptr) {
    return ptr->GetSize();
}

#if defined(EA_PORT_NATIVE)
// Each native event is preceded by a header holding its stride: fEventSize is
// the 32-bit size hardcoded by each event's ctor (too small for 64-bit), and
// an event whose ctor is still a stub never sets it. The block is zeroed so a
// never-constructed event reads as a null vtable.
void *Event::operator new(size_t size) {
    size_t stride = kNativeEventHeader + ((size + 0xF) & ~static_cast<size_t>(0xF));
    char *block = gCreationPoint;

    gCreationPoint = &gCreationPoint[stride];
    memset(block, 0, stride);
    *reinterpret_cast<size_t *>(block) = stride;

    return block + kNativeEventHeader;
}

void Event::operator delete(void *ptr, size_t size) {
    // EventManager::RunEvents advances gDeletionPoint.
}

// PC EmbedField(char const *) 0x629A90; the Hermes::Message overload is inlined
// into EDeliverMessage's ctor (0x62BA50). Copies the object into the queue right
// behind the event being constructed and grows that event's stride to cover it.
static void *EmbedBytes(Event *event, const void *src, std::size_t size) {
    char *dst = gCreationPoint;
    std::size_t padded = (size + 0xF) & ~static_cast<std::size_t>(0xF);

    memcpy(dst, src, size);
    event->fEventSize += padded;
    *reinterpret_cast<size_t *>(reinterpret_cast<char *>(event) - kNativeEventHeader) += padded;
    gCreationPoint += padded;
    return dst;
}

const char *EventManager::EmbedField(Event *event, const char *ptr) {
    if (ptr == nullptr) {
        return nullptr;
    }
    return static_cast<const char *>(EmbedBytes(event, ptr, GetEmbeddedObjectSize(ptr)));
}

Hermes::Message *EventManager::EmbedField(Event *event, Hermes::Message *ptr) {
    if (ptr == nullptr) {
        return nullptr;
    }
    return static_cast<Hermes::Message *>(EmbedBytes(event, ptr, GetEmbeddedObjectSize(ptr)));
}
#else
void *Event::operator new(size_t size) {
    void *result = gCreationPoint;

    gCreationPoint = &gCreationPoint[(size + 0xF) & ~0xF];

    return result;
}

void Event::operator delete(void *ptr, size_t size) {
    gDeletionPoint += static_cast<Event *>(ptr)->fEventSize;
}
#endif
