#include "Speed/Indep/Src/Physics/SmackableTrigger.h"

#include "Speed/Indep/Libs/Support/Miscellaneous/CARP.h"
#include "Speed/Indep/Libs/Support/Utility/FastMem.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/World/WCollisionAssets.h"
#include "Speed/Indep/Src/World/WTrigger.h"

#include <cstddef>

// A smackable's trigger: a box in the world trigger grid around the prop that
// carries one ESpawnSmackable event. The first rigid body to touch it fires
// the event, which turns the model into a Smackable simable.

namespace {

// The event list the trigger owns (FastMem "SmackTrigger", 0x48 bytes on PC):
// the list header, one event entry and the event's static data.
struct SmackTriggerEvents {
    CARP::EventList fList;
    CARP::EventStaticData fEvent;
    ESpawnSmackable::StaticData fData;
};

// WTrigger flags: enabled (1), fire once (2), hit by complex rigid bodies
// (0x40, RigidBody::GetTriggerFlags), events and trigger not owned by the grid
// (0x100) and 0x40000.
const unsigned int kSmackTriggerFlags = 0x40143;

} // namespace

// speed.exe 1.3: 0x667E70
SmackableTrigger::SmackableTrigger(HMODEL hmodel, bool virgin, const UMath::Matrix4 &objectmatrix, const UMath::Vector3 &dim,
                                   unsigned int extra_flags) {
    SmackTriggerEvents *events = static_cast<SmackTriggerEvents *>(gFastMem.Alloc(sizeof(SmackTriggerEvents), "SmackTrigger"));
    this->mTrigger = new WTrigger(objectmatrix, dim, &events->fList, extra_flags | kSmackTriggerFlags);

    events->fList.fNumEvents = 1;
    events->fEvent.fEventID = ESpawnSmackable::kEventID;
    events->fEvent.fEventSize = sizeof(ESpawnSmackable::StaticData);
    events->fEvent.fDataOffset = offsetof(SmackTriggerEvents, fData) - offsetof(SmackTriggerEvents, fEvent);
    events->fEvent.fPad = 0;
    this->mEventData = &events->fData;
    this->mEventData->fEventID = ESpawnSmackable::kEventID;

    WCollisionAssets::Get().AddTrigger(this->mTrigger);
    if (!virgin) {
        this->mTrigger->UpdateBox(objectmatrix, dim);
    }

    this->mEventData->fScenery = hmodel;
    this->mEventData->fVirginSpawn = virgin;
    UMath::Matrix4ToQuaternion(objectmatrix, this->mEventData->fOrientation);
    this->mEventData->fPosition = UMath::Vector4To3(objectmatrix.v3);
    this->mTrigger->Enable();
}

// speed.exe 1.3: inlined (ESpawnSmackable re-enables through fTrigger)
void SmackableTrigger::Fire() {
    this->mTrigger->FireEvents(nullptr);
}

// speed.exe 1.3: 0x667FB0
void SmackableTrigger::Disable() {
    this->mTrigger->Disable();
}

// speed.exe 1.3: 0x667FC0
void SmackableTrigger::Enable() {
    this->mTrigger->Enable();
}

bool SmackableTrigger::IsEnabled() const {
    return this->mTrigger->IsEnabled();
}

// speed.exe 1.3: 0x667FF0
void SmackableTrigger::GetObjectMatrix(UMath::Matrix4 &matrix) const {
    UMath::QuaternionToMatrix4(this->mEventData->fOrientation, matrix);
    matrix.v3.x = this->mEventData->fPosition.x;
    matrix.v3.y = this->mEventData->fPosition.y;
    matrix.v3.z = this->mEventData->fPosition.z;
    matrix.v3.w = 1.0f;
}

// speed.exe 1.3: 0x668070
void SmackableTrigger::Move(const UMath::Matrix4 &matrix, const UMath::Vector3 &dim, bool virgin) {
    this->mTrigger->UpdateBox(matrix, dim);
    this->mEventData->fVirginSpawn = virgin;
    UMath::Matrix4ToQuaternion(matrix, this->mEventData->fOrientation);
    this->mEventData->fPosition = UMath::Vector4To3(matrix.v3);
}

// speed.exe 1.3: 0x66FB40
SmackableTrigger::~SmackableTrigger() {
    gFastMem.Free(this->mTrigger->fEvents, sizeof(SmackTriggerEvents), "SmackTrigger");
    this->mTrigger->fEvents = nullptr;
    WCollisionAssets::Get().RemoveTrigger(this->mTrigger);
    // the trigger doesn't own itself either (0x100): RemoveTrigger leaves it
    delete this->mTrigger;
    this->mTrigger = nullptr;
    this->mEventData = nullptr;
}
