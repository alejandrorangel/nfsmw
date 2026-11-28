#include <algorithm>
#include "Speed/Indep/Src/Main/EventSequencer.h"
#include "Speed/Indep/Libs/Support/Miscellaneous/CARP.h"
#include "Speed/Indep/Libs/Support/Miscellaneous/PackedBinaryTree.h"
#include "Speed/Indep/Libs/Support/Utility/UCollections.h"
#include "Speed/Indep/Libs/Support/Utility/UCrc.h"
#include "Speed/Indep/Libs/Support/Utility/UGroup.hpp"
#include "Speed/Indep/Libs/Support/Utility/UTypes.h"
#include "Speed/Indep/Src/Main/Event.h"
#include "Speed/Indep/Src/Misc/SpeedChunks.hpp"
#include "Speed/Indep/bWare/Inc/bChunk.hpp"

#include <cstddef>

EventDynamicData gEventDynamicData;

inline void *operator new(std::size_t, void *place, unsigned int) {
    return place;
}

static CARP::ExprValType StimulusFilterLookup(unsigned int name, unsigned int subindex, const void *context, const CARP::ExprValType *value) {
    CARP::ExprValType result;
    const CARP::StimulusFilter *sf = reinterpret_cast<const CARP::StimulusFilter *>(context);
    unsigned int index = SearchPackedBinaryTree<const QueryDesc, unsigned int>(sf->mNumQueries, sf->GetQueries(), name);

    if (index < sf->mNumQueries) {
        const QueryDesc *qDesc = &sf->GetQueries()[index];
        if (subindex < qDesc->mCount) {
            return value[qDesc->mIndex + subindex];
        }

        result.u = 0;
        return result;
    }

    result.u = 0;
    return result;

}

IMPLEMENT_INSTANCABLE(EventSequencer::HENGINE, EventSequencer::IEngine)

// TO BE REMOVED
namespace Query {

extern bool Invoke(
    unsigned int funcName,
    const UTL::COM::IUnknown *acteeContext,
    const UTL::COM::IUnknown *actorContext,
    const void *params,
    unsigned int numResults,
    void *results
);

}

namespace EventSequencer {

typedef std::map<UCrc32, const UData *> NameHashToDataMap;

#if defined(EA_PORT_NATIVE)
// speed.exe 1.3's static initialisers (0x886990..0x886AD0): stringhash32 of
// the names.
static unsigned int gCreateStimulus = stringhash32("create");
static unsigned int gTriggerStimulus = stringhash32("trigger");
static unsigned int gStartAction = stringhash32("start");
static unsigned int gEndAction = stringhash32("end");
static unsigned int gStopAction = stringhash32("stop");
static unsigned int gPauseAction = stringhash32("pause");
static unsigned int gResumeAction = stringhash32("resume");
static unsigned int gUnloadAction = stringhash32("unload");
static unsigned int gStimulusParameter = stringhash32("stimulus");
static unsigned int gStimulusResult = stringhash32("result");
#else
static unsigned int gCreateStimulus;
static unsigned int gTriggerStimulus;
static unsigned int gStartAction;
static unsigned int gEndAction;
static unsigned int gStopAction;
static unsigned int gPauseAction;
static unsigned int gResumeAction;
static unsigned int gUnloadAction;
static unsigned int gStimulusParameter;
static unsigned int gStimulusResult;
#endif

static NameHashToDataMap gEngineData;
static unsigned int gNumActiveSystems = 0;
static float gLastUpdateTime = 0.0f;
static System *gActiveSystemList[1024];

void Init(float externalTime) {
    gLastUpdateTime = externalTime;
    gNumActiveSystems = 0;
}

void Reset(float externalTime) {
    gLastUpdateTime = externalTime;
}

void Update(float externalTime) {
    if (externalTime <= gLastUpdateTime) {
        return;
    }

    for (unsigned int activeIndex = 0; activeIndex < gNumActiveSystems;) {
        if (!gActiveSystemList[activeIndex]->Update(activeIndex, externalTime)) {
            activeIndex++;
        }
    }

    gLastUpdateTime = externalTime;
}

void UpdateDelta(float deltaTime) {
    if (deltaTime <= 0.0f)
        return;

    Update(gLastUpdateTime + deltaTime);
}

static void RegisterEngines(const UGroup *group) {
    for (unsigned int activeIndex = 0; activeIndex < group->DataCountType('eE  '); activeIndex++) {
        const UData *data = group->DataLocate('eE  ', activeIndex);
        const CARP::EventSeqEngine *engine = static_cast<const CARP::EventSeqEngine *>(data->GetDataConst());
        UCrc32 nameHash(engine->mName);

        gEngineData[nameHash] = data;
    }
}

static void UnregisterEngines(const UGroup *group) {
    for (unsigned int activeIndex = 0; activeIndex < group->DataCountType('eE  '); activeIndex++) {
        const UData *data = group->DataLocate('eE  ', activeIndex);
        const CARP::EventSeqEngine *engine = static_cast<const CARP::EventSeqEngine *>(data->GetDataConst());
        UCrc32 nameHash(engine->mName);

        gEngineData.erase(nameHash);
    }
}

static const UData *FindEngineData(UCrc32 name) {
    NameHashToDataMap::iterator iter = gEngineData.find(UCrc32(name));
    if (iter != gEngineData.end()) {
        return (*iter).second;
    }

    return NULL;
}

// unfinished
IEngine *Create(UTL::COM::Object *baseObject, IContext *context, const UData *data, float externalTime, float rate) {
    Engine *e = new Engine(baseObject, context, static_cast<const CARP::EventSeqEngine *>(data->GetDataConst()), externalTime, rate);

    e->ProcessStimulus(gCreateStimulus, externalTime, NULL, QUEUE_ALLOW);

    if (e != NULL) {
        return e;
    }
    return NULL;
}

IEngine *Create(UTL::COM::Object *baseObject, IContext *context, UCrc32 name, float externalTime, float rate) {
    const UData *data = FindEngineData(UCrc32(name));
    if (data == NULL) {
        return NULL;
    }

    return Create(baseObject, context, data, externalTime, rate);
}

#if defined(EA_PORT_NATIVE)
// Engine's IEngine methods (speed.exe 1.3, vtable 0x8A7EC8): each runs over
// the engine's systems.

Engine::~Engine() { // 0x645540
    for (unsigned int i = 0; i < mNumSystems; i++) {
        mSystems[i].~System();
    }
    gFastMem.Free(mSystems, mNumSystems * sizeof(System), "EventSequencerSystems");
    mSystems = NULL;
}

void Engine::Release() { // 0x6452C0
    delete this;
}

const char *Engine::Name() const { // 0x6452D0
    return mEngine != NULL ? static_cast<const char *>(mEngine->mName) : NULL;
}

void Engine::Relocate(unsigned int deltaAddress) {
    // PC 0x645360 moves the data pointers after a defragmentation; native
    // data stays put.
}

void Engine::Unload() { // 0x646730
    if (mEngine == NULL) {
        return;
    }
    mEngine = NULL;
    for (unsigned int i = 0; i < mNumSystems; i++) {
        System &s = mSystems[i];
        if (s.mEngine != NULL) {
            s.TerminateAction(gUnloadAction, s.GetActiveIndex(), s.mActionLast, true, NULL);
            s.mEngine = NULL;
        }
    }
}

IContext *Engine::GetContext() const { // 0x6452E0
    return mContext;
}

void Engine::SetContext(IContext *context) { // 0x6452F0
    mContext = context;
}

unsigned int Engine::NumSystems() const { // 0x645300
    return mNumSystems;
}

unsigned int Engine::GetSystemID(unsigned int index) const { // 0x645310
    if (mEngine != NULL && index < mNumSystems) {
        return mEngine->GetSystemIDs()[index];
    }
    return 0;
}

System *Engine::GetSystemByIndex(unsigned int index) const { // 0x645330
    return index < mNumSystems ? &mSystems[index] : NULL;
}

System *Engine::FindSystem(unsigned int systemID) const { // 0x6454A0 (with 0x6454D0)
    if (mEngine == NULL) {
        return NULL;
    }
    const unsigned int *ids = mEngine->GetSystemIDs();
    const unsigned int *end = ids + mEngine->mNumSystems;
    const unsigned int *it = std::lower_bound(ids, end, systemID);
    if (it < end && *it == systemID) {
        return GetSystemByIndex(static_cast<unsigned int>(it - ids));
    }
    return GetSystemByIndex(static_cast<unsigned int>(-1));
}

bool Engine::AnySystemInAction() const { // 0x6453C0
    for (unsigned int i = 0; i < mNumSystems; i++) {
        if (mSystems[i].IsInAction()) {
            return true;
        }
    }
    return false;
}

void Engine::SetAllSystemsState(float externalTime, unsigned int state) { // 0x6455E0
    for (unsigned int i = 0; i < mNumSystems; i++) {
        System &s = mSystems[i];
        if (s.mSystem == NULL) {
            continue;
        }
        if (s.IsInAction()) {
            s.TerminateAction(gStopAction, s.GetActiveIndex(), externalTime, true, NULL);
        }
        s.mState = const_cast<CARP::EventSeqState *>(s.mSystem->FindState(state));
        if (s.mState == NULL) {
            s.mState = const_cast<CARP::EventSeqState *>(s.mSystem->FindState(s.mSystem->mDefaultState));
            s.mCurrentState = s.mSystem->mDefaultState;
        } else {
            s.mCurrentState = state;
        }
    }
}

bool Engine::ProcessStimulus(unsigned int systemID, unsigned int stimulus, float externalTime, IContext *ifiringcontext,
                             QueueMode mode) { // 0x6467B0
    System *s = FindSystem(systemID);
    return s != NULL && s->ProcessStimulus(stimulus, externalTime, ifiringcontext, mode);
}

bool Engine::ProcessStimulus(unsigned int stimulus, float externalTime, IContext *ifiringcontext, QueueMode mode) { // 0x6467F0
    bool result = false;
    for (unsigned int i = 0; i < mNumSystems; i++) {
        if (mSystems[i].ProcessStimulus(stimulus, externalTime, ifiringcontext, mode)) {
            result = true;
        }
    }
    return result;
}

bool Engine::Trigger(float externalTime, IContext *ifiringcontext, QueueMode mode) { // 0x646850
    return ProcessStimulus(gTriggerStimulus, externalTime, ifiringcontext, mode);
}

bool Engine::FireEventTag(unsigned int tag, IContext *ifiringcontext) { // 0x645730
    bool fired = false;
    for (unsigned int i = 0; i < mNumSystems; i++) {
        if (mSystems[i].FireEventTag(tag, ifiringcontext)) {
            fired = true;
        }
    }
    return fired;
}

void Engine::Flush() { // 0x6453F0
    for (unsigned int i = 0; i < mNumSystems; i++) {
        mSystems[i].Flush();
    }
}

void Engine::Stop(float externalTime, bool flush, IContext *ifiringcontext) { // 0x645440
    for (unsigned int i = 0; i < mNumSystems; i++) {
        System &s = mSystems[i];
        s.TerminateAction(gStopAction, s.GetActiveIndex(), externalTime, flush, ifiringcontext);
    }
}

void Engine::Complete(float externalTime, bool flush, IContext *ifiringcontext) { // 0x6471B0
    for (unsigned int i = 0; i < mNumSystems; i++) {
        System &s = mSystems[i];
        if (s.IsInAction()) {
            s.FireTimedEvents(s.mActionLast, s.mAction->mDuration);
        }
        s.TerminateAction(gEndAction, s.GetActiveIndex(), externalTime, flush, ifiringcontext);
    }
}

void Engine::Pause(float externalTime, IContext *ifiringcontext) { // 0x645800
    for (unsigned int i = 0; i < mNumSystems; i++) {
        mSystems[i].Pause(externalTime, ifiringcontext);
    }
}

void Engine::Resume(float externalTime, IContext *ifiringcontext) { // 0x645860
    for (unsigned int i = 0; i < mNumSystems; i++) {
        mSystems[i].Resume(externalTime, ifiringcontext);
    }
}

// 0x647230: each system back to its initial state and given "create", then
// "create" to all (queued behind the first). PC's queueing branch for a
// system still in action can't be taken after the reset.
void Engine::Reset(float externalTime) {
    for (unsigned int i = 0; i < mNumSystems; i++) {
        System &s = mSystems[i];
        if (s.mSystem == NULL) {
            continue;
        }
        s.InternalReset(externalTime, s.mActionRate);
        if (!s.IsInAction()) {
            s.InvokeStimulus(gCreateStimulus, externalTime, NULL);
        }
    }
    ProcessStimulus(gCreateStimulus, externalTime, NULL, QUEUE_ALLOW);
}

void Engine::SetVerbose(bool verbose) { // 0x645350
    mVerbose = verbose;
}
#endif

const UGroup *PrepareBinary(const void *binData) {
    const UGroup *g = UGroup::Deserialize(binData, true, 0);
    CARP::ResolveTagReferences(g, 0);
    return g;
}

unsigned int StringToID(const char *str) {
    unsigned int hash = stringhash32(str);
    return hash;
}

static bool EraseActiveSystem(unsigned int index) {
    if (index < gNumActiveSystems) {
        for (unsigned int i = index; i < gNumActiveSystems - 1; i++) {
            gActiveSystemList[i] = gActiveSystemList[i + 1];
        }

        gActiveSystemList[--gNumActiveSystems] = NULL;
        return true;
    }

    return false;
}

unsigned int System::ID() const {
    if (this->mSystem != NULL) {
        System *systems = this->mEngine->GetSystems();
        unsigned int index = this - systems; // is this real?

        return this->mEngine->GetSystemID(index);
    }

    return 0;
}

IContext *System::GetContext() const {
    return this->mEngine->Context();
}

void System::SetState(float externalTime, unsigned int state) {
    if (this->mSystem == NULL) return;

    if (this->IsInAction()) {
        this->Stop(externalTime, true, NULL);
    }

    this->mState = const_cast<CARP::EventSeqState *>(this->mSystem->FindState(state));
    if (this->mState == NULL) {
        this->mState = const_cast<CARP::EventSeqState *>(this->mSystem->FindState(this->mSystem->mDefaultState));
        this->mCurrentState = this->mSystem->mDefaultState;
    } else {
        this->mCurrentState = state;
    }

    if (this->mEngine->IsVerbose()) {} // og debug stuff
}

bool System::IsInAction() const {
    return this->mAction != NULL;
}

bool System::IsPaused() const {
    return this->mPausedAt >= 0;
}

float System::GetActionRate() const {
    return this->mActionRate;
}

void System::Flush() {
    for (unsigned int i = 0; i < 4; i++) {
        this->mQueuedStimuli[i] = 0;
    }

    this->mQueueEndState = this->mEndState;
    this->mQueueDuration = this->IsInAction() ? this->mAction->mDuration : 0.0f;

    if (this->mEngine->IsVerbose()) {}
}

void System::Stop(float externalTime, bool flush, IContext *ifiringcontext) {
    this->TerminateAction(gStopAction, this->GetActiveIndex(), externalTime, flush, ifiringcontext);

    if (this->mEngine->IsVerbose()) {}
}

void System::Complete(float externalTime, bool flush, IContext *ifiringcontext) {
    if (this->IsInAction()) {
        this->FireTimedEvents(this->mActionLast, this->mAction->mDuration);
    }

    this->TerminateAction(gStopAction, this->GetActiveIndex(), externalTime, flush, ifiringcontext);

    if (this->mEngine->IsVerbose()) {}
}

void System::Pause(float externalTime, IContext *ifiringcontext) {
    if (this->IsPaused()) return;

    this->mPausedAt = externalTime;
    this->FireEventTag(gPauseAction, ifiringcontext);

        if (this->mEngine->IsVerbose()) {}
}

void System::Resume(float externalTime, IContext *ifiringcontext) {
    if (!this->IsPaused()) return;

    this->mActionLast += externalTime - this->mPausedAt;
    this->mPausedAt = -1.0f;

    this->FireEventTag(gResumeAction, ifiringcontext);
    if (this->mEngine->IsVerbose()) {}
}

void System::Reset(float externalTime, float rate, IContext *ifiringcontext) {
    this->InternalReset(externalTime, rate);
    this->ProcessStimulus(gCreateStimulus, externalTime, ifiringcontext, QUEUE_ALLOW);
}

#if defined(EA_PORT_NATIVE)
// speed.exe 1.3 (0x645A40): an idle system takes the stimulus now; one in
// action queues it (by the mode) as the response of the state its queue ends
// in.
bool System::ProcessStimulus(unsigned int stimulus, float externalTime, IContext *ifiringcontext, EventSequencer::QueueMode mode) {
    if (this->mSystem == NULL) {
        return false;
    }
    if (mode == QUEUE_ABORT) {
        if (this->IsInAction()) {
            this->Stop(externalTime, true, NULL);
        }
    } else if (mode == QUEUE_FLUSH && this->mQueuedStimuli[0] != 0) {
        this->Flush();
    }
    if (!this->IsInAction()) {
        return this->InvokeStimulus(stimulus, externalTime, ifiringcontext);
    }
    unsigned int blocking;
    if (mode == QUEUE_SHALLOW) {
        blocking = this->mQueuedStimuli[0];
    } else if (mode == QUEUE_ALLOW) {
        blocking = this->mQueuedStimuli[3];
    } else {
        return false;
    }
    if (blocking != 0) {
        return false;
    }
    unsigned int entry = 0;
    while (this->mQueuedStimuli[entry] != 0) {
        entry++;
    }
    unsigned int currentEnd = this->mQueuedStimuli[0] != 0 ? this->mQueueEndState : this->mEndState;
    const CARP::EventSeqState *endState = this->mSystem->FindState(currentEnd);
    if (endState == NULL) {
        endState = this->mSystem->FindState(this->mSystem->mDefaultState);
    }
    unsigned int filtered = this->ExecuteFilter(endState, stimulus, ifiringcontext);
    const CARP::EventSeqResponse *response = endState->FindResponse(filtered);
    if (response == NULL) {
        return false;
    }
    this->mQueuedStimuli[entry] = filtered;
    this->mQueueEndState = response->mEndState;
    this->mQueueDuration = response->mActionSeq->mDuration < 0.0f ? UMath::Infinity : this->mQueueDuration + response->mActionSeq->mDuration;
    return true;
}
#else
// unfinished
bool System::ProcessStimulus(unsigned int stimulus, float externalTime, IContext *ifiringcontext, EventSequencer::QueueMode mode) {
    bool result = false;

    if (this->mSystem == NULL) return result;

    if (mode == QUEUE_ABORT && this->IsInAction()) {
        this->Stop(externalTime, true, NULL);
    }
    if (mode == QUEUE_FLUSH && this->mQueuedStimuli[0] != 0) {
        this->Flush();
    }

    if (this->IsInAction()) {
        result = this->InvokeStimulus(stimulus, externalTime, ifiringcontext);
    } else if (mode != QUEUE_DISABLE) {
        bool queueIt = false;
        if (mode == QUEUE_SHALLOW && this->mQueuedStimuli[0] == 0) {
            queueIt = true;
        } else if (mode != QUEUE_ALLOW && this->mQueuedStimuli[3] == 0) {
            queueIt = true;
        } else {
            queueIt = false;
        }

        if (queueIt) {
            unsigned int entry = 0;
            while (this->mQueuedStimuli[entry] != 0) {
                entry++;
            }

            unsigned int currentEnd;
            if (this->IsInAction()) {
                if (this->mQueuedStimuli[0] == 0) {
                    currentEnd = this->mEndState;
                } else {
                    currentEnd = this->mQueueEndState;
                }
            } else {
                currentEnd = this->mCurrentState;
            }

            const CARP::EventSeqState *endState = this->mSystem->FindState(currentEnd);
            if (endState == NULL) {
                endState = this->mSystem->FindState(this->mSystem->mDefaultState);
            }

            currentEnd = this->ExecuteFilter(endState, stimulus, ifiringcontext);
            const CARP::EventSeqResponse *response = this->mState->FindResponse(currentEnd);
            if (response == NULL) {
                return false;
            }
            this->mQueuedStimuli[entry] = currentEnd;
            this->mQueueEndState = response->mEndState;
            this->mQueueDuration = (response->mActionSeq->mDuration >= 0)
                ? this->mQueueDuration + response->mActionSeq->mDuration
                : UMath::Infinity;

            result = true;
        }
    }

    if (this->mEngine->IsVerbose()) {}

    return result;
}

#endif

bool System::FireEventTag(unsigned int tag, IContext *ifiringcontext) const {
    if (this->IsInAction()) {
        unsigned int index = this->mAction->GetTagIndex(tag);

        if (index < this->mAction->mNumTags) {
            this->FireActionEventList(true, index, ifiringcontext);
            return true;
        }
    }
    return false;
}

System::System(Engine *engine, const CARP::EventSeqSystem *system, float externalTime, float rate) {
    this->mEngine = engine;
    this->mSystem = const_cast<CARP::EventSeqSystem *>(system);
    this->mAction = NULL;

    this->InternalReset(externalTime, rate);
}

System::~System() {
    if (this->IsInAction()) {
        EraseActiveSystem(this->GetActiveIndex());
    }
}

bool System::Update(unsigned int index, float externalTime) {
    bool deleted = false;

    if (this->mActionLast < externalTime && !this->IsPaused()) {
        float prevActionTime = this->mActionTime;
        if (this->mActionLast < 0.0f) {
            this->mActionTime = 0.0f;
        } else {
            this->mActionTime += (externalTime - this->mActionLast) * this->mActionRate;
        }
        this->FireTimedEvents(prevActionTime, this->mActionTime);

        if (this->mAction->mDuration >= 0.0f && this->mActionTime >= this->mAction->mDuration) {
            deleted = this->TerminateAction(gEndAction, index, externalTime, false, NULL);
        } else {
            this->mActionLast = externalTime;
        }
    }

    return deleted;
}

bool System::TerminateAction(unsigned int action, unsigned int index, float externalTime, bool flush, IContext *ifiringcontext) {
    bool deleted = false;

    if (flush) {
        this->Flush();
    }
    if (this->IsInAction()) {
        this->FireEventTag(action, ifiringcontext);

        if (this->mAction->mDuration >= 0.0f) {
            this->mQueueDuration -= this->mAction->mDuration;
        } else {
            this->mQueueDuration = 0.0f;
        }

        this->mAction = NULL;
        this->mPausedAt = -1.0f;
        deleted = EraseActiveSystem(index);
        this->SetState(externalTime, this->mEndState);
    }

    if (this->mQueuedStimuli[0] != 0) {
        unsigned int prev = 0;
        unsigned int i = 4;

        while (i-- > 0) {
            unsigned int temp = this->mQueuedStimuli[i];
            this->mQueuedStimuli[i] = prev;
            prev = temp;
        }

        this->InvokeStimulus(prev, externalTime, ifiringcontext);
    } else {
        this->mQueueDuration = 0.0f;
    }

    return deleted;
}

bool System::InvokeStimulus(unsigned int stimulus, float externalTime, IContext *ifiringcontext) {
    stimulus = this->ExecuteFilter(this->mState, stimulus, ifiringcontext);
    const CARP::EventSeqResponse *response = this->mState->FindResponse(stimulus);

    if (response != NULL) {
        this->mEndState = response->mEndState;
        this->mAction = response->mActionSeq;
        this->mActionTime = -1.0f;
        this->mActionLast = -1.0f;

        unsigned int index = gNumActiveSystems++;
        gActiveSystemList[index] = this;
        this->FireEventTag(gStartAction, ifiringcontext);
        this->Update(index, externalTime);
        return true;
    }

    return false;
}

void System::FireActionEventList(bool tagIndex, unsigned int index, struct IContext *ifiringcontext) const {
    gEventDynamicData.Clear();

    gEventDynamicData.fEventSeqSystem = this->ID();
    gEventDynamicData.fEventSeqState = this->mCurrentState;
    gEventDynamicData.fEventSeqEngine = reinterpret_cast<unsigned int>(this->mEngine->GetInstanceHandle());

    IContext *context = this->mEngine->Context();
    if (this->mEngine->IsVerbose()) {}
    if (context != NULL && !context->SetDynamicData(this, &gEventDynamicData)) {
        return;
    }

    if (this->mEngine->IsVerbose()) {}
    if (ifiringcontext != NULL && !ifiringcontext->SetDynamicData(this, &gEventDynamicData)) {
        return;
    }

    EventManager::FireEventList(this->mAction->GetEventList(tagIndex, index), this->mEngine->IsVerbose());
}

void System::FireTimedEvents(float startActionTime, float endActionTime) const {
    unsigned int timeIndex = this->mAction->GetTimeIndex(startActionTime);
    const float *timeList = this->mAction->GetTimeList();

    if (timeIndex < this->mAction->mNumTimes) {
        if (timeList[timeIndex] == startActionTime) timeIndex++;

        while (timeIndex < this->mAction->mNumTimes && timeList[timeIndex] <= endActionTime) {
            this->FireActionEventList(false, timeIndex, NULL);
            timeIndex++;
        }
    }
}

void System::Relocate(unsigned int deltaAddress) {
    if (this->mSystem == NULL) return;

    this->mSystem = reinterpret_cast<CARP::EventSeqSystem *>(reinterpret_cast<char *>(this->mSystem) + deltaAddress);

    if (this->mState != NULL) {
        this->mState = reinterpret_cast<CARP::EventSeqState *>(reinterpret_cast<char *>(this->mState) + deltaAddress);
    }
    if (this->mAction != NULL) {
        this->mAction = reinterpret_cast<CARP::EventSeqAction *>(reinterpret_cast<char *>(this->mAction) + deltaAddress);
    }
}

void System::Unload() {
    if (this->mEngine != NULL) {
        this->TerminateAction(gUnloadAction, this->GetActiveIndex(), this->mActionLast, true, NULL);
        this->mEngine = NULL;
    }
}

void System::InternalReset(float externalTime, float rate) {
    if (this->IsInAction()) {
        EraseActiveSystem(this->GetActiveIndex());
    }

    this->mAction = NULL;
    this->mPausedAt = -1.0f;
    this->mActionTime = 0.0f;
    this->mActionRate = rate;

    this->mEndState = 0;
    this->mQueueEndState = 0;

    this->mActionLast = -1.0f;
    this->mQueueDuration = 0.0f;

    this->mState = const_cast<CARP::EventSeqState *>(this->mSystem->FindState(this->mSystem->mInitialState));
    if (this->mState == NULL) {
        this->mState = const_cast<CARP::EventSeqState *>(this->mSystem->FindState(this->mSystem->mDefaultState));
        this->mCurrentState = this->mSystem->mDefaultState;
    } else {
        this->mCurrentState = this->mSystem->mInitialState;
    }

    for (unsigned int i = 0; i < 4; i++) {
        this->mQueuedStimuli[i] = 0;
    }
}

unsigned int System::GetActiveIndex() const {
    for (unsigned int i = 0; i < gNumActiveSystems; i++) {
        if (gActiveSystemList[i] == this) {
            return i;
        }
    }
    return -1;
}



unsigned int System::ExecuteFilter(const CARP::EventSeqState *state, unsigned int stimulus, IContext *ifiringcontext) const {
    if (state->mFilter != NULL) {
        CARP::ExprValType queryValues[64];

        for (unsigned int q = 0; q < state->mFilter->mNumQueries; q++) {
            const QueryDesc *qDesc = state->mFilter->GetQueries();

            if (qDesc != NULL) {
                const void *staticdata = state->mFilter->GetStaticData();
                Query::Invoke(
                    reinterpret_cast<unsigned int>(qDesc),
                    this->GetContext(),
                    ifiringcontext,
                    staticdata,
                    this->mAction->mName,
                    &queryValues[q]
                );
            } else {
                queryValues[q].u = stimulus;
            }
        }

        return CARP::ExpressionEvaluator(state->mFilter->GetExpression(), &StimulusFilterLookup, state->mFilter, queryValues).u;
    } else {
        return stimulus;
    }

}

} // namespace EventSequencer

int LoaderEventSequence(bChunk *chunk) {
    if (chunk->GetID() == BCHUNK_CARP_EVENT_SEQUENCE) {
        bChunkCarpHeader *chunkHeader = reinterpret_cast<bChunkCarpHeader *>(chunk->GetAlignedData(0x10));
        if (!chunkHeader->IsResolved()) {
            chunkHeader->PlatformEndianSwap();
        }

        const UGroup *group = EventSequencer::PrepareBinary(&chunkHeader[1]);
        EventSequencer::RegisterEngines(group);

        chunkHeader->SetResolved();
        return 1;
    }

    if (chunk->GetID() != BCHUNK_CARP_EVENT_SEQUENCES) {
        return 0;
    }

    bChunk *last_chunk = chunk->GetLastChunk();
    for (chunk = chunk->GetFirstChunk(); chunk < last_chunk; chunk = chunk->GetNext()) {
        bChunkCarpHeader *chunkHeader = reinterpret_cast<bChunkCarpHeader *>(chunk->GetAlignedData(0x10));
        if (!chunkHeader->IsResolved()) {
            chunkHeader->PlatformEndianSwap();
        }

        const UGroup *group = EventSequencer::PrepareBinary(&chunkHeader[1]);
        EventSequencer::RegisterEngines(group);

        chunkHeader->SetResolved();
    }

    return 1;
}

int UnloaderEventSequence(bChunk *chunk) {
    if (chunk->GetID() == BCHUNK_CARP_EVENT_SEQUENCE) {
        bChunkCarpHeader *chunkHeader = reinterpret_cast<bChunkCarpHeader *>(chunk->GetAlignedData(0x10));
        const UGroup *group = UGroup::Deserialize(&chunkHeader[1], false, 0);
        EventSequencer::UnregisterEngines(group);
        return 1;
    }

    if (chunk->GetID() != BCHUNK_CARP_EVENT_SEQUENCES) {
        return 0;
    }

    bChunk *last_chunk = chunk->GetLastChunk();
    for (chunk = chunk->GetFirstChunk(); chunk < last_chunk; chunk = chunk->GetNext()) {
        bChunkCarpHeader *chunkHeader = reinterpret_cast<bChunkCarpHeader *>(chunk->GetAlignedData(0x10));
        const UGroup *group = UGroup::Deserialize(&chunkHeader[1], false, 0);
        EventSequencer::UnregisterEngines(group);
    }

    return 1;
}

// PC: 0x886B00, 0x886B20
bChunkLoader bChunkLoaderEventSequences(BCHUNK_CARP_EVENT_SEQUENCES, LoaderEventSequence, UnloaderEventSequence);
bChunkLoader bChunkLoaderEventSequence(BCHUNK_CARP_EVENT_SEQUENCE, LoaderEventSequence, UnloaderEventSequence);
