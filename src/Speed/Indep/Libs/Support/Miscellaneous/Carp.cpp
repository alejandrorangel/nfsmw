#include "Speed/Indep/Libs/Support/Miscellaneous/CARP.h"
#include "Speed/Indep/Libs/Support/Utility/UGroup.hpp"
#include "Speed/Indep/Src/Main/Event.h"

#include <algorithm>
#include <new>
#include <vector>

namespace CARP {

// total size: 0x8
struct TagResolverNode {
    TagResolverNode() {}
    TagResolverNode(unsigned int tag, UDataTagResolverFunc func) : fTag(tag), fFunc(func) {}

    bool operator<(const TagResolverNode &rhs) const {
        return this->fTag < rhs.fTag;
    }

    unsigned int fTag;          // offset 0x0, size 0x4
    UDataTagResolverFunc fFunc; // offset 0x4, size 0x4
};

static bool gHaveInitialized = false;            // PC: 0x91DDBC
static bool gResolversSorted = false;            // PC: 0x91DDBD
static std::vector<TagResolverNode> gResolvers;  // PC: 0x91DDC0
static unsigned int gResolverDeltaAddress = 0;   // PC: 0x91DD94
static bool gResolvingTags = false;              // PC: 0x91DD90

// Natively a resolved slot holds PortPtr32 bits (see TagReference in CARP.h).
static unsigned int EncodeTagPointer(const void *p) {
#if defined(EA_PORT_NATIVE)
    return PortPtr32Encode(p);
#else
    return reinterpret_cast<unsigned int>(p);
#endif
}

// PC: 0x5D0DF0. The PC build also calls an optional debug hook (0x91DD8C)
// that can veto a reference; nothing ever installs it, so it's left out.
TagReference::TagReference(const UGroup *context) {
    unsigned int tag = this->fData;
    if (tag == 0) {
        return;
    }
    if (gResolverDeltaAddress != 0) {
        // already resolved once: the data moved by the delta
#if defined(EA_PORT_NATIVE)
        this->fData = PortPtr32Encode(static_cast<char *>(PortPtr32Decode(tag)) + static_cast<int>(gResolverDeltaAddress));
#else
        this->fData = gResolverDeltaAddress + tag;
#endif
        return;
    }
    if (tag == 0xFFFFFFFF) {
        // the containing group
        this->fData = EncodeTagPointer(context);
        return;
    }
    const UData *d = context->DataLocateTag(tag);
    if (d == context->DataEnd()) {
        this->fData = 0;
        return;
    }
    this->fData = EncodeTagPointer(d->GetDataConst());
}

// Every event in the list resolves its own static data.
// PC: 0x5CBBF0
static void EventListResolver(const UData *d, const UGroup *context) {
    const EventList *list = static_cast<const EventList *>(d->GetDataConst());
    const EventStaticData *event = list->Event();
    for (unsigned int i = list->fNumEvents; i != 0; i--) {
        RegisterEvent::ResolveEventCallback resolve = RegisterEvent::ResolveEvent(event->fEventID);
        if (resolve != nullptr) {
            resolve(const_cast<char *>(event->StaticData()), context);
        }
        event = reinterpret_cast<const EventStaticData *>(reinterpret_cast<const char *>(event) + sizeof(EventStaticData));
    }
}

// Query::Resolve (PC 0x633880) looks the query up by the first word of its
// static data in a one-entry table (0xBF50744E, QSimTime_Resolver at
// 0x626970), and that resolver does nothing.
static void ResolveQuery(void *queryData, const UGroup *context) {}

// PC: 0x5CBC40
static void StimulusFilterResolver(const UData *d, const UGroup *context) {
    const StimulusFilter *filter = static_cast<const StimulusFilter *>(d->GetDataConst());
    const QueryDesc *queries = filter->GetQueries();
    const char *staticData = reinterpret_cast<const char *>(&queries[filter->mNumQueries]) + filter->mExpressionSize;
    for (unsigned int i = 0; i < filter->mNumQueries; i++) {
        if (queries[i].mQueryName != 0) {
            ResolveQuery(const_cast<char *>(staticData + queries[i].mDataOffset), context);
        }
    }
}

// PC: 0x5D2F40
static void EventSeqEngineResolver(const UData *d, const UGroup *context) {
    const EventSeqEngine *e = static_cast<const EventSeqEngine *>(d->GetDataConst());
    new (const_cast<EventSeqEngine *>(e)) TagReference(context); // mName

    const EventSeqSystemRef *systems = e->GetSystems();
    for (unsigned int i = 0; i < e->mNumSystems; i++) {
        new (const_cast<EventSeqSystemRef *>(&systems[i])) TagReference(context);
    }
}

// PC: 0x5D2FF0
static void EventSeqSystemResolver(const UData *d, const UGroup *context) {
    const EventSeqSystem *s = static_cast<const EventSeqSystem *>(d->GetDataConst());
    const EventSeqStateRef *states = s->GetStates();
    for (unsigned int i = 0; i < s->mNumStates; i++) {
        new (const_cast<EventSeqStateRef *>(&states[i])) TagReference(context);
    }
}

// PC: 0x5D3080
static void EventSeqStateResolver(const UData *d, const UGroup *context) {
    const EventSeqState *s = static_cast<const EventSeqState *>(d->GetDataConst());
    new (const_cast<PORT_PTR32(StimulusFilter) *>(&s->mFilter)) TagReference(context);

    const EventSeqResponse *responses = s->GetResponses();
    for (unsigned int i = 0; i < s->mNumStimuli; i++) {
        new (const_cast<PORT_PTR32(EventSeqAction) *>(&responses[i].mActionSeq)) TagReference(context);
    }
}

// PC: 0x5D3130
static void EventSeqActionResolver(const UData *d, const UGroup *context) {
    const EventSeqAction *a = static_cast<const EventSeqAction *>(d->GetDataConst());
    const EventListRef *lists = a->GetEventListArray();
    unsigned int numLists = a->mNumTags + a->mNumTimes;
    for (unsigned int i = 0; i < numLists; i++) {
        new (const_cast<EventListRef *>(&lists[i])) TagReference(context);
    }
}

// PC: 0x5D31C0
static void CollisionInstanceResolver(const UData *d, const UGroup *context) {
    const CollisionInstance *inst = static_cast<const CollisionInstance *>(d->GetDataConst());
    for (unsigned int i = 0; i < d->DataCount(); i++) {
        new (&inst[i].fCollisionArticle) TagReference(context);
    }
}

// PC: 0x5D7980
void AddUDataTagResolver(unsigned int dataTag, UDataTagResolverFunc dtrFunc) {
    gResolvers.push_back(TagResolverNode(dataTag, dtrFunc));
}

// PC: 0x5D79B0
void InitResolvers() {
    if (gHaveInitialized) {
        return;
    }
    gResolvers.reserve(16);
    gResolvers.push_back(TagResolverNode('ci  ', CollisionInstanceResolver));
    gResolvers.push_back(TagResolverNode('eE  ', EventSeqEngineResolver));
    gResolvers.push_back(TagResolverNode('eS  ', EventSeqSystemResolver));
    gResolvers.push_back(TagResolverNode('eT  ', EventSeqStateResolver));
    gResolvers.push_back(TagResolverNode('eA  ', EventSeqActionResolver));
    gResolvers.push_back(TagResolverNode('sf  ', StimulusFilterResolver));
    gResolvers.push_back(TagResolverNode('el  ', EventListResolver));
    std::sort(gResolvers.begin(), gResolvers.end());
    gHaveInitialized = true;
    gResolversSorted = true;
}

// PC: 0x5D5AA0
static void ResolveData(const UGroup *g, const UData *d) {
    TagResolverNode t(d->Type(), nullptr);
    std::vector<TagResolverNode>::iterator it = std::lower_bound(gResolvers.begin(), gResolvers.end(), t);
    if (it != gResolvers.end() && it->fTag == t.fTag) {
        it->fFunc(d, g);
    }
}

// total size: 0xC
class CarpResolver : public UGroup::Processor {
  public:
    CarpResolver(unsigned int deltaAddress) : fDeltaAddress(deltaAddress) {}

    // PC: 0x5D5B10
    bool StartGroup(const UGroup *g) {
        this->ProcessTagRefs(g);
        this->SkipData();
        return true;
    }

    // PC: 0x5C75C0
    bool ProcessData(const UGroup *g, const UData *d) {
        return true;
    }

    // PC: 0x5C75D0
    void EndGroup(const UGroup *g) {}

  private:
    // PC: 0x5D5B30
    bool ProcessTagRefs(const UGroup *g) {
        gResolverDeltaAddress = this->fDeltaAddress;
        gResolvingTags = true;
        for (const UData *d = g->DataBegin(); d != g->DataEnd(); ++d) {
            ResolveData(g, d);
        }
        gResolvingTags = false;
        return true;
    }

    unsigned int fDeltaAddress; // offset 0x8, size 0x4
};

// Runs the registered resolver of every UData in the tree. Returns a value
// from the tree's 'Sect' data (section sizes) if it has one.
// PC: 0x5D7AD0
unsigned int ResolveTagReferences(const UGroup *g, unsigned int deltaAddress) {
    if (!gHaveInitialized) {
        InitResolvers();
    }

    CarpResolver sr(deltaAddress);
    g->ProcessBreadthFirst(sr);

    const UData *sectSizes = g->DataLocateTag('Sect');
    if (sectSizes != g->DataEnd()) {
        return static_cast<const unsigned int *>(sectSizes->GetDataConst())[0x7C / 4];
    }
    return 0;
}

} // namespace CARP
