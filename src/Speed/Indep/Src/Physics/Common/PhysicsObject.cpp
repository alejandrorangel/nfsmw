#include "Speed/Indep/Src/Physics/PhysicsObject.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Interfaces/SimEntities/IEntity.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/ICause.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRigidBody.h"
#include "Speed/Indep/Src/Physics/Behavior.h"
#include "Speed/Indep/Src/Sim/Simulation.h"
#include "Speed/Indep/Src/World/WWorldPos.h"
#include "Speed/Indep/Src/World/WorldConn.h"

namespace Sim {
namespace Collision {
void AddParticipant(HSIMABLE participant);
void RemoveParticipant(HSIMABLE participant);
}; // namespace Collision
}; // namespace Sim

// GC 0x80215EA8: keeps the list sorted by priority (stable for equal priorities)
void PhysicsObject::Behaviors::Add(Behavior *beh) {
    int pri = beh->GetPriority();
    iterator iter = begin();
    for (; iter != end(); ++iter) {
        if (pri < (*iter)->GetPriority()) {
            break;
        }
    }
    _Base::insert(iter, beh);
}

// speed.exe 1.3: 0x67F510
void PhysicsObject::Behaviors::Remove(Behavior *beh) {
    iterator iter = std::find(begin(), end(), beh);
    if (iter != end()) {
        _Base::erase(iter);
    }
}

// speed.exe 1.3: 0x67EC20
void PhysicsObject::Behaviors::Reset() {
    for (iterator iter = begin(); iter != end(); ++iter) {
        (*iter)->Reset();
    }
}

// speed.exe 1.3: 0x6883E0
PhysicsObject::PhysicsObject(const Attrib::Instance &attribs, SimableType objType, WUID wuid, unsigned int num_interfaces)
    : Sim::Object(num_interfaces + 3),                    //
      ISimable(this),                                     //
      IBody(this),                                        //
      IAttachable(this),                                  //
      mWPos(new WWorldPos(0.025f)), //
      mObjType(objType),                                  //
      mOwner(nullptr),                                    //
      mAttributes(attribs.GetConstCollection(), 0, nullptr), //
      mRigidBody(nullptr),                                //
      mSimulateTask(nullptr),                             //
      mEntity(nullptr),                                   //
      mPlayer(nullptr),                                   //
      mBodyService(nullptr),                              //
      mWorldID(static_cast<WUID>(reinterpret_cast<uintptr_t>(GetInstanceHandle())) | 0x1000000), //
      mAttachments(new Sim::Attachments(this)) {
    if (wuid != 0) {
        mWorldID = wuid;
    }
    mSimulateTask = AddTask(UCrc32("Physics"), 1.0f, 0.0f, Sim::TASK_FRAME_FIXED);
    Sim::ProfileTask(mSimulateTask, "Physics");
    Sim::Collision::AddParticipant(GetInstanceHandle());
}

// speed.exe 1.3: 0x686C20
PhysicsObject::~PhysicsObject() {
    ReleaseBehaviors();
    RemoveTask(mSimulateTask);
    if (mEntity) {
        Detach(mEntity);
        mPlayer = nullptr;
        mEntity = nullptr;
    }
    if (mAttachments) {
        delete mAttachments;
        mAttachments = nullptr;
    }
    if (mBodyService) {
        CloseService(mBodyService);
        mBodyService = nullptr;
    }
    if (mWPos) {
        delete mWPos;
    }
    Sim::Collision::RemoveParticipant(GetInstanceHandle());

#if defined(EA_PORT_NATIVE)
    // Container<Behavior> destructor: anything still owned goes with us
    while (!mElements.empty()) {
        Behavior *b = mElements.front();
        mElements.pop_front();
        delete b;
    }
#endif
}

// speed.exe 1.3: 0x67B4F0
void PhysicsObject::Kill() {
    ReleaseGC();
    if (mBodyService) {
        CloseService(mBodyService);
        mBodyService = nullptr;
    }
}

// speed.exe 1.3: 0x6696E0
bool PhysicsObject::OnService(HSIMSERVICE hCon, Sim::Packet *pkt) {
    if (hCon == mBodyService) {
        WorldConn::Pkt_Body_Service *pbs = static_cast<WorldConn::Pkt_Body_Service *>(pkt);
        UMath::Matrix4 mat;
        mRigidBody->GetMatrix4(mat);
        const UMath::Vector3 &pos = mRigidBody->GetPosition();
        mat.v3.x = pos.x;
        mat.v3.y = pos.y;
        mat.v3.z = pos.z;
        pbs->SetMatrix(mat);
        pbs->SetVelocity(mRigidBody->GetLinearVelocity());
        return true;
    }
    return false;
}

// speed.exe 1.3: 0x67EC20
void PhysicsObject::Reset() {
    mBehaviors.Reset();
}

// speed.exe 1.3: 0x67B520
void PhysicsObject::OnBehaviorChange(const UCrc32 &mechanic) {
    if (mechanic == UCrc32(BEHAVIOR_MECHANIC_RIGIDBODY.GetHash32())) {
        ISimable::QueryInterface(&mRigidBody);
        if (!mBodyService && mRigidBody) {
            UMath::Matrix4 mat;
            mRigidBody->GetMatrix4(mat);
            const UMath::Vector3 &pos = mRigidBody->GetPosition();
            mat.v3.x = pos.x;
            mat.v3.y = pos.y;
            mat.v3.z = pos.z;
            WorldConn::Pkt_Body_Open pkt(mWorldID, mat);
            mBodyService = OpenService(UCrc32(UCRC32_WORLD), &pkt);
        }
    }
    for (Behaviors::iterator iter = mBehaviors.begin(); iter != mBehaviors.end(); ++iter) {
        (*iter)->OnBehaviorChange(mechanic);
    }
}

// speed.exe 1.3: 0x67B4B0
bool PhysicsObject::OnTask(HSIMTASK htask, float dT) {
    if (htask == mSimulateTask) {
        if (!IsDirty()) {
            OnTaskSimulate(dT);
            for (Behaviors::iterator iter = mBehaviors.begin(); iter != mBehaviors.end(); ++iter) {
                Behavior *beh = *iter;
                if (!beh->IsPaused()) {
                    beh->OnTaskSimulate(dT);
                }
            }
        }
        return true;
    }
    return false;
}

// speed.exe 1.3: 0x670820
bool PhysicsObject::Attach(UTL::COM::IUnknown *object) {
    if (!mAttachments) {
        return false;
    }
    if (!UTL::COM::ComparePtr(mEntity, object)) {
        Sim::IEntity *ientity;
        if (object->QueryInterface(&ientity)) {
            if (mEntity) {
                Detach(mEntity);
                mEntity = nullptr;
                mPlayer = nullptr;
            }
            mEntity = ientity;
            ientity->QueryInterface(&mPlayer);
        }
        return mAttachments->Attach(object);
    }
    return false;
}

// speed.exe 1.3: 0x6697C0
bool PhysicsObject::Detach(UTL::COM::IUnknown *object) {
    if (!mAttachments) {
        return false;
    }
    if (UTL::COM::ComparePtr(mEntity, object)) {
        mEntity = nullptr;
        mPlayer = nullptr;
    }
    return mAttachments->Detach(object);
}

// GC 0x80218B30
void PhysicsObject::DetachAll() {
    if (mAttachments) {
        mAttachments->DetachAll();
    }
}

// speed.exe 1.3: 0x686B20
const IAttachable::List *PhysicsObject::GetAttachments() const {
    if (mAttachments) {
        return &mAttachments->GetList();
    }
    return nullptr;
}

// speed.exe 1.3: 0x686B00
bool PhysicsObject::IsAttached(const UTL::COM::IUnknown *pOther) const {
    if (mAttachments) {
        return mAttachments->IsAttached(pOther);
    }
    return false;
}

// speed.exe 1.3: 0x67B450
void PhysicsObject::OnAttached(IAttachable *pOther) {
    for (Behaviors::iterator iter = mBehaviors.begin(); iter != mBehaviors.end(); ++iter) {
        (*iter)->OnOwnerAttached(pOther);
    }
}

// speed.exe 1.3: 0x67B480
void PhysicsObject::OnDetached(IAttachable *pOther) {
    for (Behaviors::iterator iter = mBehaviors.begin(); iter != mBehaviors.end(); ++iter) {
        (*iter)->OnOwnerDetached(pOther);
    }
}

// speed.exe 1.3: 0x669840
void PhysicsObject::AttachEntity(Sim::IEntity *e) {
    if (e) {
        Attach(e);
    } else {
        DetachEntity();
    }
}

// speed.exe 1.3: 0x669820
void PhysicsObject::DetachEntity() {
    if (mEntity) {
        Detach(mEntity);
        mPlayer = nullptr;
        mEntity = nullptr;
    }
}

// speed.exe 1.3: 0x676FD0
bool PhysicsObject::IsOwnedByPlayer() const {
    const ISimable *owner = ISimable::FindInstance(mOwner);
    while (owner) {
        if (owner->IsPlayer()) {
            return true;
        }
        if (!owner->GetOwnerHandle()) {
            break;
        }
        if (owner->GetOwnerHandle() == owner->GetInstanceHandle()) {
            return false;
        }
        const ISimable *next = ISimable::FindInstance(owner->GetOwnerHandle());
        if (owner == next) {
            return false;
        }
        owner = next;
    }
    return false;
}

// speed.exe 1.3: 0x686BF0
ISimable *PhysicsObject::GetOwner() const {
    return ISimable::FindInstance(mOwner);
}

// speed.exe 1.3: 0x677070
bool PhysicsObject::IsOwnedBy(ISimable *queriedOwner) const {
    if (queriedOwner) {
        HSIMABLE handle = queriedOwner->GetInstanceHandle();
        ISimable *owner = ISimable::FindInstance(mOwner);
        while (owner) {
            if (owner->GetInstanceHandle() == handle) {
                return true;
            }
            ISimable *next = ISimable::FindInstance(owner->GetOwnerHandle());
            if (owner == next) {
                break;
            }
            owner = next;
        }
    }
    return false;
}

// speed.exe 1.3: 0x670800
void PhysicsObject::SetOwnerObject(ISimable *pOwner) {
    if (pOwner) {
        mOwner = pOwner->GetInstanceHandle();
    } else {
        mOwner = nullptr;
    }
}

// speed.exe 1.3: 0x669770
void PhysicsObject::DebugObject() {
    GetRigidBody()->Debug();
}

// speed.exe 1.3: 0x686A50
bool PhysicsObject::IsRigidBodySimple() const {
    if (mRigidBody) {
        return mRigidBody->IsSimple();
    }
    return false;
}

// speed.exe 1.3: 0x686A70
bool PhysicsObject::IsRigidBodyComplex() const {
    return !IsRigidBodySimple();
}

// speed.exe 1.3: 0x686AD0
const UMath::Vector3 &PhysicsObject::GetPosition() const {
    if (mRigidBody) {
        return mRigidBody->GetPosition();
    }
    return UMath::Vector3::kZero;
}

// speed.exe 1.3: 0x669520
void PhysicsObject::GetTransform(UMath::Matrix4 &matrix) const {
    if (mRigidBody) {
        mRigidBody->GetMatrix4(matrix);
        const UMath::Vector3 &pos = mRigidBody->GetPosition();
        matrix.v3.x = pos.x;
        matrix.v3.y = pos.y;
        matrix.v3.z = pos.z;
        matrix.v3.w = 1.0f;
    } else {
        matrix = UMath::Matrix4::kIdentity;
    }
}

// speed.exe 1.3: 0x669590
void PhysicsObject::GetLinearVelocity(UMath::Vector3 &velocity) const {
    if (mRigidBody) {
        velocity = mRigidBody->GetLinearVelocity();
    } else {
        velocity = UMath::Vector3::kZero;
    }
}

// speed.exe 1.3: 0x6695E0
void PhysicsObject::GetAngularVelocity(UMath::Vector3 &velocity) const {
    if (mRigidBody) {
        velocity = mRigidBody->GetAngularVelocity();
    } else {
        velocity = UMath::Vector3::kZero;
    }
}

// speed.exe 1.3: 0x669630
void PhysicsObject::GetDimension(UMath::Vector3 &dim) const {
    if (mRigidBody) {
        dim = mRigidBody->GetDimension();
    } else {
        dim = UMath::Vector3::kZero;
    }
}

// speed.exe 1.3: 0x669860
void PhysicsObject::ProcessStimulus(unsigned int stimulus) {
    if (GetEventSequencer()) {
        GetEventSequencer()->ProcessStimulus(stimulus, Sim::GetTime(), nullptr, EventSequencer::QUEUE_ALLOW);
    }
}

// speed.exe 1.3: 0x6696A0
void PhysicsObject::SetCausality(HCAUSE from, float time) {
    IModel *model = GetModel();
    if (model) {
        model->SetCausality(from, time);
    }
}

// speed.exe 1.3: 0x6696C0
HCAUSE PhysicsObject::GetCausality() const {
    const IModel *model = GetModel();
    if (model) {
        return model->GetCausality();
    }
    return nullptr;
}

// speed.exe 1.3: 0x669680
float PhysicsObject::GetCausalityTime() const {
    const IModel *model = GetModel();
    if (model) {
        return model->GetCausalityTime();
    }
    return 0.0f;
}

// speed.exe 1.3: 0x684B60
Behavior *PhysicsObject::FindBehavior(const UCrc32 &mechanic) const {
    Mechanics::iterator iter = mMechanics.find(mechanic.GetValue());
    if (iter != mMechanics.end() && iter->second) {
        return iter->second;
    }
    return nullptr;
}

// GC 0x80217ED4
bool PhysicsObject::IsBehaviorActive(const UCrc32 &mechanic) const {
    Behavior *beh = FindBehavior(mechanic);
    return beh && !beh->IsPaused();
}

// speed.exe 1.3: 0x684A10
void PhysicsObject::PauseBehavior(const UCrc32 &mechanic, bool pause) {
    Behavior *beh = FindBehavior(mechanic);
    if (beh) {
        beh->Pause(pause);
    }
}

// speed.exe 1.3: 0x684A80
bool PhysicsObject::ResetBehavior(const UCrc32 &mechanic) {
    Behavior *beh = FindBehavior(mechanic);
    if (beh) {
        beh->Reset();
        return true;
    }
    return false;
}

// speed.exe 1.3: 0x6809F0
void PhysicsObject::ReleaseBehaviors() {
    for (Mechanics::iterator iter = mMechanics.begin(); iter != mMechanics.end(); ++iter) {
        Behavior *beh = iter->second;
        if (beh) {
            UCrc32 mechanic(iter->first);
            mBehaviors.Remove(beh);
#if defined(EA_PORT_NATIVE)
            mElements.remove(beh);
#endif
            delete beh;
            iter->second = nullptr;
            OnBehaviorChange(mechanic);
        }
    }
}

// speed.exe 1.3: 0x684AE0
void PhysicsObject::ReleaseBehavior(const UCrc32 &mechanic) {
    Mechanics::iterator iter = mMechanics.find(mechanic.GetValue());
    if (iter != mMechanics.end() && iter->second) {
        Behavior *beh = iter->second;
        mBehaviors.Remove(beh);
#if defined(EA_PORT_NATIVE)
        mElements.remove(beh);
#endif
        delete beh;
        iter->second = nullptr;
        OnBehaviorChange(mechanic);
    }
}

// speed.exe 1.3: 0x684BB0
Behavior *PhysicsObject::LoadBehavior(const UCrc32 &mechanic, const UCrc32 &behavior, Sim::Param params) {
    if (IsDirty()) {
        return nullptr;
    }
    Behavior *existing = FindBehavior(mechanic);
    if (existing && existing->GetSignature() == behavior) {
        return existing;
    }
    ReleaseBehavior(mechanic);
    if (behavior == UCrc32::kNull) {
        return nullptr;
    }

    const BehaviorParams bp = {params, this, behavior, mechanic};
    // Container<Behavior>::BuildElement
    Behavior *beh = Behavior::CreateInstance(behavior, bp);
    if (!beh) {
        return nullptr;
    }
#if defined(EA_PORT_NATIVE)
    mElements.push_back(beh);
#endif
    mMechanics[mechanic.GetValue()] = beh;
    mBehaviors.Add(beh);
    OnBehaviorChange(mechanic);
    return beh;
}
