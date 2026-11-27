#ifndef SUPPORT_MISC_CARP_H
#define SUPPORT_MISC_CARP_H

#include "Speed/Indep/Libs/Support/Utility/UTypes.h"
#include "Speed/Indep/Libs/Support/Utility/UGroup.hpp"
#include "Speed/Indep/Tools/EventSys/Runtime/Common/eventsysdata.h"
#include "types.h"

#include <algorithm>
#include <cmath>

struct WCollisionArticle;

namespace CARP {

struct Map {};

struct EventList;
struct EventSeqAction;
struct EventSeqState;
struct EventSeqSystem;
struct StimulusFilter;

// Slots that ResolveTagReferences turns from a tag into a pointer (see
// TagReference). They are 4 bytes in the loaded data, so natively they hold a
// PortPtr32.
typedef PORT_PTR32(const EventList) EventListRef;
typedef PORT_PTR32(const EventSeqState) EventSeqStateRef;
typedef PORT_PTR32(const EventSeqSystem) EventSeqSystemRef;

// TODO fix the order of these

// total size: 0x10
struct EventStaticData {
    // char *StaticData() {}

    // Decl: 253
    const char *StaticData() const {
        return reinterpret_cast<const char *>(this) + fDataOffset;
    }

    // EventStaticData *NextEvent() {}

    // const EventStaticData *NextEvent() const {}

    unsigned int fEventID;    // offset 0x0, size 0x4
    unsigned int fEventSize;  // offset 0x4, size 0x4
    unsigned int fDataOffset; // offset 0x8, size 0x4
    unsigned int fPad;        // offset 0xC, size 0x4
};

// total size: 0x10
struct EventList {
    // Decl: 97
    EventStaticData *Event() {
        return reinterpret_cast<EventStaticData *>(&this[1]);
    }

    const EventStaticData *Event() const {
        return reinterpret_cast<const EventStaticData *>(&this[1]);
    }

    // const EventStaticData *Event() const {}

    unsigned int fNumEvents; // offset 0x0, size 0x4
    unsigned int fPad[3];    // offset 0x4, size 0xC
};

// total size: 0x10
struct EventSeqAction {
    uint32 *GetTagList() {
        return reinterpret_cast<uint32 *>(&this[1]);
    }

    float *GetTimeList() {
        return reinterpret_cast<float *>(&this->GetTagList()[this->mNumTags]);
    }

    EventListRef *GetEventListArray() {
        return reinterpret_cast<EventListRef *>(&this->GetTimeList()[this->mNumTimes]);
    }

    const uint32 *GetTagList() const {
        return reinterpret_cast<const uint32 *>(&this[1]);
    }

    const float *GetTimeList() const {
        return reinterpret_cast<const float *>(&this->GetTagList()[this->mNumTags]);
    }

    const EventListRef *GetEventListArray() const {
        return reinterpret_cast<const EventListRef *>(&this->GetTimeList()[this->mNumTimes]);
    }

    const EventList *GetEventList(bool tagIndex, unsigned int index) const {
        unsigned int actualIndex = index;
        const EventListRef *eventLists = this->GetEventListArray();

        if (!tagIndex) {
            actualIndex += this->mNumTags;
        }
        return eventLists[actualIndex];
    }

    unsigned int GetTimeIndex(float t) const {
        const float *timeEntry = this->GetTimeList();
        const float *timeList = std::lower_bound(timeEntry, &timeEntry[this->mNumTimes], t);

        if (timeList < &timeEntry[this->mNumTimes]) {
            return timeList - timeEntry;
        } else {
            return -1;
        }
    }

    // Decl: 420
    unsigned int GetTagIndex(unsigned int tag) const {
        const unsigned int *tagEntry = this->GetTagList();
        const unsigned int *tagList = std::lower_bound(tagEntry, &tagEntry[this->mNumTags], tag);

        if (tagList < &tagEntry[this->mNumTags] && *tagList == tag) {
            return tagList - tagEntry;
        } else {
            return -1;
        }
    }

    uint32 mName;     // offset 0x0, size 0x4
    uint32 mNumTags;  // offset 0x4, size 0x4
    uint32 mNumTimes; // offset 0x8, size 0x4
    float mDuration;  // offset 0xC, size 0x4
};

// total size: 0x8
struct EventSeqResponse {
    uint32 mEndState;           // offset 0x0, size 0x4
    PORT_PTR32(EventSeqAction) mActionSeq; // offset 0x4, size 0x4
};

struct Expression {
    // Functions
    unsigned int *ParamNames() {}

    const unsigned int *ParamNames() const {}

    unsigned char *OpCodes() {}

    const unsigned char *OpCodes() const {}

    unsigned int Size() const {}

    // Members
    unsigned int mNumOpCodes; // offset 0x0, size 0x4
    unsigned int mNumParams;  // offset 0x4, size 0x4
};

// total size: 0x10
struct StimulusFilter {
    QueryDesc *GetQueries() {
        return reinterpret_cast<QueryDesc *>(&this[1]);
    }

    const QueryDesc *GetQueries() const {
        return reinterpret_cast<const QueryDesc *>(&this[1]);
    }

    Expression *GetExpression() {
        return reinterpret_cast<Expression *>(&this->GetQueries()[this->mNumQueries]);
    }

    const Expression *GetExpression() const {
        return reinterpret_cast<const Expression *>(&this->GetQueries()[this->mNumQueries]);
    }

    char *GetStaticData() {
        return reinterpret_cast<char *>(&this->GetExpression()[this->mExpressionSize] + (unsigned int)&this[1]);
    }

    // Decl: 547
    const char *GetStaticData() const {
        return reinterpret_cast<const char *>(&this->GetExpression()[this->mExpressionSize]);
    }

    unsigned int mNumQueries;     // offset 0x0, size 0x4
    unsigned int mNumInputs;      // offset 0x4, size 0x4
    unsigned int mExpressionSize; // offset 0x8, size 0x4
    unsigned int mPad;            // offset 0xC, size 0x4
};

// total size: 0x8
struct EventSeqState {
    unsigned int *GetStimuli() {
        return reinterpret_cast<uint32 *>(&this[1]);
    }

    EventSeqResponse *GetResponses() {
        return reinterpret_cast<EventSeqResponse *>(&this->GetStimuli()[this->mNumStimuli]);
    }

    const uint32 *GetStimuli() const {
        return reinterpret_cast<const uint32 *>(&this[1]);
    }

    // Decl: 378
    const EventSeqResponse *GetResponses() const {
        return reinterpret_cast<const EventSeqResponse *>(&this->GetStimuli()[this->mNumStimuli]);
    }

    const EventSeqResponse *FindResponse(unsigned int stimulus) const {
        const unsigned int *stimEntry = this->GetStimuli();
        const unsigned int *stimList = std::lower_bound(stimEntry, &stimEntry[this->mNumStimuli], stimulus);

        if (stimList < &stimEntry[this->mNumStimuli] && *stimList == stimulus) {
            return &this->GetResponses()[stimList - stimEntry];
        } else {
            return nullptr;
        }
    }

    uint32 mNumStimuli;      // offset 0x0, size 0x4
    PORT_PTR32(StimulusFilter) mFilter; // offset 0x4, size 0x4
};

// total size: 0xC
struct EventSeqSystem {
    unsigned int *GetStateIDs() {
        return reinterpret_cast<uint32 *>(&this[1]);
    }

    EventSeqStateRef *GetStates() {
        return reinterpret_cast<EventSeqStateRef *>(&this->GetStateIDs()[this->mNumStates]);
    }

    const uint32 *GetStateIDs() const {
        return reinterpret_cast<const uint32 *>(&this[1]);
    }

    const EventSeqStateRef *GetStates() const {
        return reinterpret_cast<const EventSeqStateRef *>(&this->GetStateIDs()[this->mNumStates]);
    }

    // Decl: 353
    const EventSeqState *FindState(unsigned int state) const {
        const unsigned int *stateEntry = this->GetStateIDs();
        const unsigned int *stateList = std::lower_bound(stateEntry, &stateEntry[this->mNumStates], state);

        if (stateList < &stateEntry[this->mNumStates] && *stateList == state) {
            return this->GetStates()[stateList - stateEntry];
        } else {
            return nullptr;
        }
    }

    uint32 mInitialState; // offset 0x0, size 0x4
    uint32 mDefaultState; // offset 0x4, size 0x4
    uint32 mNumStates;    // offset 0x8, size 0x4
};

struct EventSeqEngine {
    unsigned int *GetSystemIDs() {
        return &this->mNumSystems + 1;
    }

    EventSeqSystemRef *GetSystems() {
        return reinterpret_cast<EventSeqSystemRef *>(&this->GetSystemIDs()[this->mNumSystems]);
    }

    const unsigned int FindSystemIndex(unsigned int ident) const {}

    const EventSeqSystem *FindSystem(unsigned int ident) const {}

    const unsigned int *GetSystemIDs() const {
        return &this->mNumSystems + 1;
    }

    const EventSeqSystemRef *GetSystems() const {
        return reinterpret_cast<const EventSeqSystemRef *>(&this->GetSystemIDs()[this->mNumSystems]);
    }

    PORT_PTR32(const char) mName; // offset 0x0, size 0x4
    uint32 mNumSystems; // offset 0x4, size 0x4
};

// A 4-byte slot in loaded CARP data that holds a UData tag until
// ResolveTagReferences constructs a TagReference over it, which replaces the
// tag with a pointer to that UData's data (-1: the containing group, unknown:
// null). Natively the pointer is stored as PortPtr32 bits, so the fields
// these slots alias are PORT_PTR32.
// total size: 0x4
struct TagReference {
    unsigned int fData; // offset 0x0, size 0x4

    TagReference() {}
    TagReference(unsigned int tag) : fData(tag) {}

    TagReference(const UGroup *context);
};

// total size: 0x40
struct CollisionInstance {
    bVector4 fInvMatRow0Width;                          // offset 0x0, size 0x10
    unsigned short fIterStamp;                          // offset 0x10, size 0x2
    mutable unsigned short fFlags;                      // offset 0x12, size 0x2
    float fHeight;                                      // offset 0x14, size 0x4
    unsigned short fGroupNumber;                        // offset 0x18, size 0x2
    unsigned short fRenderInstanceInd;                  // offset 0x1A, size 0x2
    mutable PORT_PTR32(const WCollisionArticle) fCollisionArticle; // offset 0x1C, size 0x4
    bVector4 fInvMatRow2Length;                         // offset 0x20, size 0x10
    bVector4 fInvPosRadius;                             // offset 0x30, size 0x10
};

struct CollisionSurface {
    unsigned char fSurface;
    unsigned char fFlags;
};

// total size: 0x70
struct CollisionObject {
    UMath::Vector4 fPosRadius;         // offset 0x0, size 0x10
    UMath::Vector4 fDimensions;        // offset 0x10, size 0x10
    unsigned char fType;               // offset 0x20, size 0x1
    unsigned char fShape;              // offset 0x21, size 0x1
    unsigned short fFlags;             // offset 0x22, size 0x2
    unsigned short fRenderInstanceInd; // offset 0x24, size 0x2
    CollisionSurface fSurface;         // offset 0x26, size 0x2
    float fPAD[2];                     // offset 0x28, size 0x8
    UMath::Matrix4 fMat;               // offset 0x30, size 0x40
};

// total size: 0x40
struct EA_PACKED Trigger {
    bool ValidateMatrix() const;
    void MakeMatrix(UMath::Matrix4 &m, bool addXLate) const;

    UMath::Vector4 fMatRow0Width;  // offset 0x0, size 0x10
    unsigned int fType : 4;        // offset 0x10, size 0x4
    unsigned int fShape : 4;       // offset 0x10, size 0x4
    unsigned int fFlags : 24;      // offset 0x10, size 0x4
    float fHeight;                 // offset 0x14, size 0x4
    CARP::EventList *fEvents;      // offset 0x18, size 0x4
    unsigned short fIterStamp;     // offset 0x1C, size 0x2
    unsigned short fFingerprint;   // offset 0x1E, size 0x2
    UMath::Vector4 fMatRow2Length; // offset 0x20, size 0x10
    UMath::Vector4 fPosRadius;     // offset 0x30, size 0x10
};

union ExprValType { // 0x4
    /* 0x0 */ float f;
    /* 0x0 */ unsigned int u;
    /* 0x0 */ int i;
    /* 0x0 */ bool b;
};

typedef void (*UDataTagResolverFunc)(const UData *d, const UGroup *context);

void InitResolvers();
void AddUDataTagResolver(unsigned int dataTag, UDataTagResolverFunc dtrFunc);
unsigned int ResolveTagReferences(const UGroup *g, unsigned int deltaAddress);
ExprValType ExpressionEvaluator(const Expression *expr, ExprValType (*lookup)(unsigned int, unsigned int, const void *, const ExprValType *),
                                const void *context, const ExprValType *values);

}; // namespace CARP

#if defined(EA_PORT_NATIVE)
static_assert(sizeof(CARP::EventSeqEngine) == 0x8, "EventSeqEngine header is loaded in place");
static_assert(sizeof(CARP::EventSeqResponse) == 0x8, "EventSeqResponse is loaded in place");
static_assert(sizeof(CARP::EventSeqState) == 0x8, "EventSeqState header is loaded in place");
static_assert(sizeof(CARP::CollisionInstance) == 0x40, "CollisionInstance is loaded in place");
#endif

#endif
