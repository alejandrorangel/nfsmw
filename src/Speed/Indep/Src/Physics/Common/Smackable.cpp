// Smackable: the simable of a knocked prop (a lamp post, sign, cone...) and its
// RBSmackable rigid body. Reconstructed from speed.exe 1.3.
//
// ESpawnSmackable (or PlaceableScenery::Place) builds one for a prop model
// (SmokeableInfo.cpp). It drops through the world once it settles (DROPOUT),
// is simplified to a simple rigid body when there are too many of them, and
// dies when it stops or leaves the world.

#include "Speed/Indep/Src/Physics/Smackable.h"

#include "Speed/Indep/Libs/Support/Utility/FastMem.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/rigidbodyspecs.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/smackable.h"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Interfaces/IListener.h"
#include "Speed/Indep/Src/Interfaces/SimModels/IPlaceableScenery.h"
#include "Speed/Indep/Src/Interfaces/SimModels/ISceneryModel.h"
#include "Speed/Indep/Src/Interfaces/SimModels/ITriggerableModel.h"
#include "Speed/Indep/Src/Interfaces/Simables/ICause.h"
#include "Speed/Indep/Src/Interfaces/Simables/ICollisionBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/IDisposable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IExplodeable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IExplosion.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRenderable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRigidBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimpleBody.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"
#include "Speed/Indep/Src/Physics/Behavior.h"
#include "Speed/Indep/Src/Physics/Behaviors/RigidBody.h"
#include "Speed/Indep/Src/Physics/Bounds.h"
#include "Speed/Indep/Src/Physics/Dynamics/Articulation.h"
#include "Speed/Indep/Src/Physics/PhysicsObject.h"
#include "Speed/Indep/Src/Physics/VehicleBehaviors.h"
#include "Speed/Indep/Src/Sim/Collision.h"
#include "Speed/Indep/Src/Sim/SimActivity.h"
#include "Speed/Indep/Src/Sim/Simulation.h"
#include "Speed/Indep/Src/World/DamageZones.h"
#include "Speed/Indep/Src/World/WWorldPos.h"

#include <algorithm>

namespace Sim {
bool CanSpawnRigidBody(const UMath::Vector3 &position, bool highPriority);
bool CanSpawnSimpleRigidBody(const UMath::Vector3 &position, bool highPriority);
} // namespace Sim

namespace {

UCrc32 SmackableMechanic(const Attrib::StringKey &mechanic) {
    return UCrc32(mechanic.GetHash32());
}

// speed.exe 1.3: 0x669C00 (GetDropTimer): how long a settled prop takes to
// sink out of the world, if it does.
float SmackableDropTimer(const Attrib::Gen::smackable &attributes) {
    float drop = attributes.DROPOUT(0);
    if (drop > 0.0f && attributes.DROPOUT(1) > 0.0f) {
        return drop;
    }
    return 0.0f;
}

const float kMpsToMph = 0.022369863f; // speed.exe 1.3: 0x8A94B4 (/100)

// speed.exe 1.3: 0x92616C, RBSmackable instances
int TheRBSmackableCount = 0;

} // namespace

// total size: 0x148
class RBSmackable : public RigidBody {
  public:
    static Behavior *Construct(const BehaviorParams &parms);

    RBSmackable(const BehaviorParams &parms, const RBComplexParams &rp);
    ~RBSmackable() override;

    static int Count() {
        return TheRBSmackableCount;
    }

  protected:
    bool ShouldSleep() const override;
    void OnTaskSimulate(float dT) override;
    bool CanCollideWithWorld() const override;
    bool CanCollideWithGround() const override;
    bool CanCollideWith(const RigidBody &other) const override;

  private:
    BehaviorSpecsPtr<Attrib::Gen::rigidbodyspecs> mSpecs; // offset 0x130, size 0x14
    unsigned int mFrame;                                  // offset 0x144, size 0x4
};

BIND_BEHAVIOR_FACTORY(RBSmackable);

// speed.exe 1.3: 0x67F600
Behavior *RBSmackable::Construct(const BehaviorParams &parms) {
    const RBComplexParams rp(parms.fparams.Fetch<RBComplexParams>(UCrc32(UCRC32_BASE)));
    return new RBSmackable(parms, rp);
}

// speed.exe 1.3: 0x67B7D0
RBSmackable::RBSmackable(const BehaviorParams &parms, const RBComplexParams &rp) : RigidBody(parms, rp), mSpecs(this, 0), mFrame(0) {
    TheRBSmackableCount++;
}

RBSmackable::~RBSmackable() {
    TheRBSmackableCount--;
}

// speed.exe 1.3: 0x66A310. Joined props (a sign on its post) stay awake.
bool RBSmackable::ShouldSleep() const {
    if (Dynamics::Articulation::IsJoined(static_cast<const Dynamics::IEntity *>(this))) {
        return false;
    }
    return RigidBody::ShouldSleep();
}

// speed.exe 1.3: 0x66A340
void RBSmackable::OnTaskSimulate(float dT) {
    RigidBody::OnTaskSimulate(dT);
    mFrame++;
}

// speed.exe 1.3: 0x66A360
bool RBSmackable::CanCollideWith(const RigidBody &other) const {
    if (Dynamics::Articulation::IsJoined(static_cast<const Dynamics::IEntity *>(&other), static_cast<const Dynamics::IEntity *>(this))) {
        return false;
    }
    return RigidBody::CanCollideWith(other);
}

// speed.exe 1.3: 0x66A3A0
bool RBSmackable::CanCollideWithGround() const {
    if (IsAttachedToWorld()) {
        return false;
    }
    return RigidBody::CanCollideWithGround();
}

// speed.exe 1.3: 0x66A3C0. Slow props that haven't hit anything only test the
// world every other (under 15 m/s) or every fourth (under 2 m/s) frame.
bool RBSmackable::CanCollideWithWorld() const {
    if (IsAttachedToWorld()) {
        return false;
    }
    if (!HasHadCollision()) {
        float speed2 = UMath::LengthSquare(GetLinearVelocity());
        if (speed2 < 225.0f) {
            unsigned int skip = speed2 < 4.0f ? (mFrame & 3) : (mFrame & 1);
            if (skip != 0) {
                return false;
            }
        }
    }
    return RigidBody::CanCollideWithWorld();
}

// total size: 0x14C
class Smackable : public PhysicsObject,
                  public IDisposable,
                  public IRenderable,
                  public Sim::Collision::IListener,
                  public UTL::Collections::Listable<Smackable, 160>,
                  public IExplodeable,
                  public EventSequencer::IContext {
  public:
    // speed.exe 1.3 (0x4C bytes): sorts the smackables by simplification weight
    // and simplifies the heaviest when there are too many rigid ones.
    class Manager : public Sim::Activity {
      public:
        explicit Manager(float rate);
        ~Manager() override;

        bool OnTask(HSIMTASK htask, float dT) override;

        static Manager *Get() {
            return mInstance;
        }

      private:
        HSIMTASK mTask; // offset 0x48, size 0x4

        static Manager *mInstance;
    };

    static ISimable *Construct(Sim::Param params);
    static bool SimplifySort(Smackable *a, Smackable *b);
    static bool TrySimplify();

    Smackable(const UMath::Matrix4 &matrix, const Attrib::Gen::smackable &attributes, const CollisionGeometry::Bounds *geoms, bool virginspawn,
              IModel *scenery, bool simple_physics, bool is_persistant);
    ~Smackable() override;

    // PhysicsObject
    void OnTaskSimulate(float dT) override;
    void OnBehaviorChange(const UCrc32 &mechanic) override;
    bool OnTask(HSIMTASK htask, float dT) override;

    // ISimable
    void Kill() override;
    IModel *GetModel() override {
        return mModel;
    }
    const IModel *GetModel() const override {
        return mModel;
    }
    EventSequencer::IEngine *GetEventSequencer() override {
        return mModel ? mModel->GetEventSequencer() : nullptr;
    }

    // IAttachable
    void OnDetached(IAttachable *pOther) override;

    // IDisposable
    bool IsRequired() const override {
        return false;
    }

    // IRenderable
    bool InView() const override {
        return mModel ? mModel->InView() : false;
    }
    bool IsRenderable() const override {
        return mModel != nullptr;
    }
    HMODEL GetModelHandle() const override {
        return mModel ? mModel->GetInstanceHandle() : nullptr;
    }
    float DistanceToView() const override {
        return mModel ? mModel->DistanceToView() : 0.0f;
    }

    // IListener
    void OnCollision(const COLLISION_INFO &cinfo) override;

    // IExplodeable
    bool OnExplosion(const UMath::Vector3 &normal, const UMath::Vector3 &position, float dT, IExplosion *explosion) override;

    // IContext
    bool SetDynamicData(const EventSequencer::System *system, EventDynamicData *data) override;

  private:
    bool Simplify();
    bool Dropout();
    bool ProcessDropout(float dT);
    void ProcessDeath(float dT);
    void ProcessOffWorld(float dT);
    void Manage(float dT);
    bool ValidateWorld();
    bool ShouldDie();
    bool CanRetrigger() const;
    void OnImpact(float acceleration, float speed, COLLISION_INFO::CollisionType type, ISimable *iother);
    void DoImpactStimulus(unsigned int systemid, float intensity);
    void CalcSimplificationWeight();

    Attrib::Gen::smackable mAttributes;                    // offset 0xD0, size 0x14
    float mSimplifyWeight;                                 // offset 0xE4, size 0x4
    float mAge;                                            // offset 0xE8, size 0x4
    float mLife;                                           // offset 0xEC, size 0x4
    float mDropTimer;                                      // offset 0xF0, size 0x4
    float mDropOutTimerMax;                                // offset 0xF4, size 0x4
    float mOffWorldTimer;                                  // offset 0xF8, size 0x4
    float mAutoSimplify;                                   // offset 0xFC, size 0x4
    bool mVirgin;                                          // offset 0x100, size 0x1
    IModel *mModel;                                        // offset 0x104, size 0x4
    const CollisionGeometry::Bounds *mGeometry;            // offset 0x108, size 0x4
    HSIMTASK mManageTask;                                  // offset 0x10C, size 0x4
    bool mDroppingOut;                                     // offset 0x110, size 0x1
    bool mPersistant;                                      // offset 0x111, size 0x1
    ICollisionBody *mCollisionBody;                        // offset 0x114, size 0x4
    ISimpleBody *mSimpleBody;                              // offset 0x118, size 0x4
    UMath::Vector3 mLastImpactSpeed;                       // offset 0x11C, size 0xC
    BehaviorSpecsPtr<Attrib::Gen::rigidbodyspecs> mRBSpecs; // offset 0x128, size 0x14
    UMath::Vector4 mLastCollisionPosition;                 // offset 0x13C, size 0x10
};

BIND_PHYSICS_FACTORY(Smackable);

Smackable::Manager *Smackable::Manager::mInstance = nullptr;

// speed.exe 1.3: 0x670B90
Smackable::Manager::Manager(float rate) : Sim::Activity(0) {
    mInstance = this;
    mTask = AddTask(UCrc32("WorldUpdate"), rate, 0.0f, Sim::TASK_FRAME_FIXED);
}

// speed.exe 1.3: 0x670C70
Smackable::Manager::~Manager() {
    RemoveTask(mTask);
    mInstance = nullptr;
}

// speed.exe 1.3: 0x685000
bool Smackable::Manager::OnTask(HSIMTASK htask, float dT) {
    if (htask != mTask) {
        return false;
    }
    UTL::Collections::Listable<Smackable, 160>::Sort(Smackable::SimplifySort);
    if (RBSmackable::Count() > 0x14) {
        Smackable::TrySimplify();
    }
    return true;
}

// speed.exe 1.3: 0x6709E0. Heaviest first: far away, out of view and small.
bool Smackable::SimplifySort(Smackable *a, Smackable *b) {
    if (a->mSimplifyWeight > b->mSimplifyWeight) {
        return true;
    }
    if (a->mSimplifyWeight < b->mSimplifyWeight) {
        return false;
    }
    return reinterpret_cast<uintptr_t>(a->ISimable::GetInstanceHandle()) < reinterpret_cast<uintptr_t>(b->ISimable::GetInstanceHandle());
}

// speed.exe 1.3: 0x684DB0
bool Smackable::TrySimplify() {
    typedef UTL::Collections::Listable<Smackable, 160> SmackableList;
    const SmackableList::List &list = SmackableList::GetList();
    for (SmackableList::List::const_iterator iter = list.begin(); iter != list.end(); ++iter) {
        if ((*iter)->Simplify()) {
            return true;
        }
    }
    return false;
}

// speed.exe 1.3: 0x6895A0. Gameplay props (IPlaceableScenery) are persistent
// and always get a full rigid body; scenery props share a budget of 20.
ISimable *Smackable::Construct(Sim::Param params) {
    const SmackableParams &sp = params.Fetch<SmackableParams>(UCrc32(UCRC32_BASE));
    IModel *scenery = sp.fScenery;
    if (scenery == nullptr) {
        return nullptr;
    }
    Attrib::Gen::smackable attributes(scenery->GetAttributes());
    if (!attributes.IsValid()) {
        return nullptr;
    }

    IPlaceableScenery *placeable;
    bool persistant = scenery->QueryInterface(&placeable);
    bool simple;
    if (!attributes.SimplePhysics() && !sp.fSimplePhysics) {
        simple = false;
        if (!persistant && RBSmackable::Count() > 0x13 && !TrySimplify()) {
            return nullptr;
        }
    } else {
        simple = true;
    }

    const CollisionGeometry::Bounds *geoms = scenery->GetCollisionGeometry();
    if (geoms == nullptr || !(attributes.MASS() > 1e-6f)) {
        return nullptr;
    }
    UMath::Matrix4 matrix = sp.fMatrix;
    const UMath::Vector3 &position = UMath::Vector4To3(matrix.v3);
    if (simple) {
        if (!Sim::CanSpawnSimpleRigidBody(position, false)) {
            return nullptr;
        }
    } else if (!Sim::CanSpawnRigidBody(position, false)) {
        return nullptr;
    }
    if (Manager::Get() == nullptr) {
        new Manager(0.1f);
    }
    return new Smackable(matrix, attributes, geoms, sp.fVirginSpawn, scenery, simple, persistant);
}

// speed.exe 1.3: 0x688660
Smackable::Smackable(const UMath::Matrix4 &matrix, const Attrib::Gen::smackable &attributes, const CollisionGeometry::Bounds *geoms, bool virginspawn,
                     IModel *scenery, bool simple_physics, bool is_persistant)
    : PhysicsObject(attributes, SIMABLE_SMACKABLE, scenery->GetWorldID(), 4), //
      IDisposable(this),                                                     //
      IRenderable(this),                                                     //
      IExplodeable(this),                                                    //
      IContext(this),                                                        //
      mAttributes(attributes),                                               //
      mSimplifyWeight(0.0f),                                                 //
      mAge(0.0f),                                                            //
      mLife(0.125f),                                                         //
      mDropTimer(0.0f),                                                      //
      mDropOutTimerMax(SmackableDropTimer(attributes)),                      //
      mOffWorldTimer(0.0f),                                                  //
      mAutoSimplify(attributes.AUTO_SIMPLIFY()),                             //
      mVirgin(virginspawn),                                                  //
      mModel(scenery),                                                       //
      mGeometry(geoms),                                                      //
      mManageTask(nullptr),                                                  //
      mDroppingOut(false),                                                   //
      mPersistant(is_persistant),                                            //
      mCollisionBody(nullptr),                                               //
      mSimpleBody(nullptr),                                                  //
      mLastImpactSpeed(UMath::Vector3::kZero),                               //
      mRBSpecs(static_cast<ISimable *>(this), 0),                            //
      mLastCollisionPosition(UMath::Vector4::kZero) {
    UMath::Vector3 dim;
    geoms->GetHalfDimensions(dim);
    dim.x = UMath::Max(dim.x, 0.025f);
    dim.y = UMath::Max(dim.y, 0.025f);
    dim.z = UMath::Max(dim.z, 0.025f);
    float radius = UMath::Length(dim);
    float mass = mAttributes.MASS();

    // a solid box, doubled
    float x2 = (dim.x + dim.x) * (dim.x + dim.x);
    float y2 = (dim.y + dim.y) * (dim.y + dim.y);
    float z2 = (dim.z + dim.z) * (dim.z + dim.z);
    UMath::Vector3 moment;
    moment.x = (z2 + y2) * (1.0f / 12.0f) * mass * 2.0f;
    moment.y = (z2 + x2) * (1.0f / 12.0f) * mass * 2.0f;
    moment.z = (y2 + x2) * (1.0f / 12.0f) * mass * 2.0f;
    UMath::Vector3 moment_scale;
    if (mAttributes.MOMENT(moment_scale)) {
        if (moment_scale.x > 0.0f) {
            moment.x *= moment_scale.x;
        }
        if (moment_scale.y > 0.0f) {
            moment.y *= moment_scale.y;
        }
        if (moment_scale.z > 0.0f) {
            moment.z *= moment_scale.z;
        }
    }

    // a prop smacked for the first time starts attached to the world until
    // DETACH_FORCE breaks it free (OnBehaviorChange)
    bool active = !virginspawn || mPersistant;
    const UMath::Vector3 &position = UMath::Vector4To3(matrix.v3);
    if (!simple_physics) {
        RBComplexParams rp(position, UMath::Vector3::kZero, UMath::Vector3::kZero, matrix, mass, moment, dim, active, geoms, 0);
        LoadBehavior(SmackableMechanic(BEHAVIOR_MECHANIC_RIGIDBODY), UCrc32(mPersistant ? "RigidBody" : "RBSmackable"), rp);
        LoadBehavior(SmackableMechanic(BEHAVIOR_MECHANIC_EFFECTS), UCrc32("EffectsSmackable"), Sim::Param());
    } else {
        RBSimpleParams sp(position, UMath::Vector3::kZero, UMath::Vector3::kZero, matrix, radius, mass);
        LoadBehavior(SmackableMechanic(BEHAVIOR_MECHANIC_RIGIDBODY), UCrc32("SimpleRigidBody"), sp);
    }

    for (unsigned int i = 0; i < mAttributes.Num_BEHAVIORS(); i++) {
        const Attrib::StringKey &behavior = mAttributes.BEHAVIORS(i);
        const char *name = behavior.GetString();
        if (name != nullptr && *name != '\0') {
            LoadBehavior(UCrc32(behavior.GetHash32()), UCrc32(behavior.GetHash32()), Sim::Param());
        }
    }

    Attach(mModel);
    mManageTask = AddTask(UCrc32("Physics"), 0.1f, 0.0f, Sim::TASK_FRAME_FIXED);
    CalcSimplificationWeight();

    const char *sequencer = mAttributes.EventSequencer().GetString();
    if (sequencer != nullptr && *sequencer != '\0') {
        Sim::Collision::AddListener(this, ISimable::GetInstanceHandle(), "Smackable");
    }
}

// speed.exe 1.3: 0x686DE0
Smackable::~Smackable() {
    DetachEntity();
    DestroyAttachments();
    if (mManageTask != nullptr) {
        RemoveTask(mManageTask);
        mManageTask = nullptr;
    }
    if (mModel != nullptr) {
        Detach(mModel);
        mModel = nullptr;
    }
    ReleaseBehavior(SmackableMechanic(BEHAVIOR_MECHANIC_EFFECTS));
    ReleaseBehavior(SmackableMechanic(BEHAVIOR_MECHANIC_RIGIDBODY));
    Sim::Collision::RemoveListener(this);
}

// speed.exe 1.3: 0x684DF0
void Smackable::OnBehaviorChange(const UCrc32 &mechanic) {
    PhysicsObject::OnBehaviorChange(mechanic);
    if (mechanic != SmackableMechanic(BEHAVIOR_MECHANIC_RIGIDBODY)) {
        return;
    }
    if (!static_cast<ISimable *>(this)->QueryInterface(&mCollisionBody)) {
        mCollisionBody = nullptr;
    }
    if (mCollisionBody != nullptr) {
        float detach = mAttributes.DETACH_FORCE();
        if (mVirgin && detach != 0.0f) {
            mCollisionBody->AttachedToWorld(true, detach > 0.0f ? detach : 0.0f);
        }
        const CollisionGeometry::Bounds *cg = mGeometry->fCollection->GetChild(mGeometry, UCrc32(0x28b0bb8d));
        if (cg == nullptr) {
            mCollisionBody->DistributeMass();
        } else {
            UMath::Vector3 cog;
            cg->fPosition.Decompress(cog);
            mCollisionBody->SetCenterOfGravity(cog);
        }
    }
    if (!static_cast<ISimable *>(this)->QueryInterface(&mSimpleBody)) {
        mSimpleBody = nullptr;
    }
    if (mSimpleBody != nullptr) {
        mSimpleBody->ModifyFlags(0, 0x20b);
        ReleaseBehavior(SmackableMechanic(BEHAVIOR_MECHANIC_EFFECTS));
    }
}

// speed.exe 1.3: 0x684C60. Swaps the full rigid body for a simple one
// (keeping its motion).
bool Smackable::Simplify() {
    if (mCollisionBody == nullptr || mPersistant) {
        return false;
    }
    if (!Sim::CanSpawnSimpleRigidBody(GetPosition(), true)) {
        return false;
    }
    IRigidBody *irb = GetRigidBody();
    UMath::Vector3 position = irb->GetPosition();
    UMath::Vector3 linvel = irb->GetLinearVelocity();
    UMath::Vector3 angvel = irb->GetAngularVelocity();
    float radius = irb->GetRadius();
    float mass = irb->GetMass();
    UMath::Matrix4 matrix = mCollisionBody->GetMatrix4();
    RBSimpleParams sp(position, linvel, angvel, matrix, radius, mass);
    LoadBehavior(SmackableMechanic(BEHAVIOR_MECHANIC_RIGIDBODY), UCrc32("SimpleRigidBody"), sp);
    return true;
}

// speed.exe 1.3: 0x67B650 (ISimable)
void Smackable::Kill() {
    if (mManageTask != nullptr) {
        RemoveTask(mManageTask);
        mManageTask = nullptr;
    }
    if (mModel != nullptr && !mPersistant) {
        mModel->ReleaseModel();
        mModel = nullptr;
    }
    if (mCollisionBody != nullptr) {
        mCollisionBody->DisableModeling();
        mCollisionBody->DisableTriggering();
    }
    if (mSimpleBody != nullptr) {
        mSimpleBody->ModifyFlags(0x308, 0);
    }
    PhysicsObject::Kill();
}

// speed.exe 1.3: 0x67B760 (IAttachable). Losing the model kills the prop.
void Smackable::OnDetached(IAttachable *pOther) {
    if (UTL::COM::ComparePtr(pOther, mModel)) {
        mModel = nullptr;
        if (!IsDirty()) {
            Kill();
        }
    }
    PhysicsObject::OnDetached(pOther);
}

// speed.exe 1.3: 0x669D40
bool Smackable::SetDynamicData(const EventSequencer::System *system, EventDynamicData *data) {
    data->fPosition = mLastCollisionPosition;
    return true;
}

// speed.exe 1.3: 0x669D70. Up to seven impact levels by intensity (0..1).
void Smackable::DoImpactStimulus(unsigned int systemid, float intensity) {
    float time = Sim::GetTime();
    EventSequencer::IEngine *engine = GetEventSequencer();
    if (engine == nullptr) {
        return;
    }
    EventSequencer::System *system = engine->FindSystem(systemid);
    if (system == nullptr) {
        return;
    }
    if (intensity >= 1.0f) {
        intensity = 1.0f;
    } else if (!(intensity >= 0.0f)) {
        intensity = 0.0f;
    }
    unsigned int levels = static_cast<unsigned int>(static_cast<int>(intensity * 6.0f)) + 1;
    for (unsigned int i = 0; i < levels; i++) {
        system->ProcessStimulus(DamageZone::GetImpactStimulus(i).GetValue(), time, this, EventSequencer::QUEUE_ALLOW);
    }
}

// speed.exe 1.3: 0x669E30
void Smackable::OnImpact(float acceleration, float speed, COLLISION_INFO::CollisionType type, ISimable *iother) {
    if (GetEventSequencer() == nullptr) {
        return;
    }
    if (type == COLLISION_INFO::OBJECT) {
        if (iother != nullptr) {
            float intensity = acceleration * kMpsToMph;
            DoImpactStimulus(0xd59062c8, intensity);
            if (iother->IsPlayer()) {
                DoImpactStimulus(0x2f698829, intensity);
            }
            if (iother->GetSimableType() == SIMABLE_VEHICLE) {
                DoImpactStimulus(0x80b88c1d, intensity);
            }
        }
    } else if (type == COLLISION_INFO::WORLD) {
        DoImpactStimulus(0x7ebe81c0, speed * kMpsToMph);
    } else if (type == COLLISION_INFO::GROUND) {
        DoImpactStimulus(0x2bf74e61, speed * 0.1f);
    }
}

// speed.exe 1.3: 0x6772C0 (IListener)
void Smackable::OnCollision(const COLLISION_INFO &cinfo) {
    if (GetEventSequencer() == nullptr) {
        return;
    }
    float speed = UMath::Length(cinfo.closingVel);
    if (!(speed >= 1.0f)) {
        return;
    }
    mLastCollisionPosition.x = cinfo.position.x;
    mLastCollisionPosition.y = cinfo.position.y;
    mLastCollisionPosition.z = cinfo.position.z;
    mLastCollisionPosition.w = 1.0f;
    COLLISION_INFO::CollisionType type = static_cast<COLLISION_INFO::CollisionType>(cinfo.type);
    if (cinfo.objA == ISimable::GetInstanceHandle()) {
        mLastImpactSpeed = cinfo.objAVel;
        OnImpact(cinfo.impulseA, speed, type, ISimable::FindInstance(cinfo.objB));
    } else if (cinfo.objB == ISimable::GetInstanceHandle()) {
        mLastImpactSpeed = cinfo.objBVel;
        OnImpact(cinfo.impulseB, speed, type, ISimable::FindInstance(cinfo.objA));
    }
}

// speed.exe 1.3: 0x677100 (IExplodeable)
bool Smackable::OnExplosion(const UMath::Vector3 &normal, const UMath::Vector3 &position, float dT, IExplosion *explosion) {
    if ((explosion->GetTargets() & 1) == 0) {
        return false;
    }
    IRigidBody *irb = GetRigidBody();
    float effect = mAttributes.ExplosionEffect();
    if (!(effect > 0.0f)) {
        return false;
    }
    float expansion = explosion->GetExpansionSpeed() * effect;
    UMath::Vector3 velocity;
    irb->GetPointVelocity(position, velocity);
    float along = UMath::Dot(velocity, normal);
    if (along < expansion) {
        float dv = expansion - along;
        float scale = irb->GetMass() / dT;
        UMath::Vector3 force;
        force.x = normal.x * dv * scale;
        force.y = normal.y * dv * scale;
        force.z = normal.z * dv * scale;
        irb->ResolveForce(force, position);
    }
    EventSequencer::IEngine *engine = GetEventSequencer();
    if (engine != nullptr) {
        engine->ProcessStimulus(0xab556d39, Sim::GetTime(), nullptr, EventSequencer::QUEUE_ALLOW);
        if (explosion->HasDamage()) {
            engine->ProcessStimulus(0xffcd8a63, Sim::GetTime(), nullptr, EventSequencer::QUEUE_ALLOW);
        }
    }
    if (GetCausality() == nullptr && explosion->GetCausality() != nullptr) {
        ICause *icause = ICause::FindInstance(explosion->GetCausality());
        if (icause != nullptr) {
            icause->OnCausedExplosion(explosion, this);
        }
    }
    return true;
}

// speed.exe 1.3: 0x669F50. A prop that came to rest drops out of the world
// (DROPOUT) instead of dying on the spot.
bool Smackable::Dropout() {
    if (mCollisionBody != nullptr && mDropOutTimerMax > 0.0f && !mCollisionBody->IsAttachedToWorld()) {
        mCollisionBody->DisableModeling();
        mCollisionBody->DisableTriggering();
        mDropTimer = mDropOutTimerMax;
        return true;
    }
    return false;
}

// speed.exe 1.3: 0x669FB0
bool Smackable::ValidateWorld() {
    const UMath::Vector3 &position = GetPosition();
    WWorldPos &wpos = GetWPos();
    wpos.FindClosestFace(position, true);
    if (wpos.OnValidFace()) {
        float height = wpos.HeightAtPoint(position);
        if (GetRigidBody()->GetRadius() + position.y >= height) {
            return true;
        }
    }
    return false;
}

// speed.exe 1.3: 0x66A020
bool Smackable::ShouldDie() {
    if (!mDroppingOut && mCollisionBody != nullptr) {
        if (mCollisionBody->IsSleeping() && !mCollisionBody->HasHadObjectCollision()) {
            return true;
        }
        if (!mCollisionBody->IsModeling()) {
            return true;
        }
    }
    return false;
}

// speed.exe 1.3: 0x66A070. A scenery prop that settles on the road again
// becomes a resting prop (its model keeps the trigger).
bool Smackable::CanRetrigger() const {
    if (mCollisionBody != nullptr && GetWPos().OnValidFace() && mCollisionBody->IsSleeping()) {
        return true;
    }
    return false;
}

// speed.exe 1.3: 0x66A0B0. Sinks the prop by DROPOUT[1] m/s.
bool Smackable::ProcessDropout(float dT) {
    if (mDropOutTimerMax > 0.0f && mDropTimer > 0.0f) {
        IRigidBody *irb = GetRigidBody();
        mDropTimer -= dT;
        irb->ModifyYPos(-(dT * mAttributes.DROPOUT(1)));
        if (mDropTimer <= 0.0f) {
            Kill();
            mDropTimer = 0.0f;
        }
        return true;
    }
    return false;
}

// speed.exe 1.3: 0x670A30
void Smackable::ProcessDeath(float dT) {
    if (!ShouldDie()) {
        mLife = 0.125f;
        return;
    }
    if (mLife > 0.0f) {
        mLife -= dT;
        return;
    }
    if (Dropout()) {
        return;
    }
    if (mModel != nullptr && CanRetrigger()) {
        ICollisionBody *ibody = mCollisionBody;
        if (mVirgin && ibody != nullptr && ibody->IsAttachedToWorld()) {
            // never broke free: put the scenery back the way it was
            ISceneryModel *iscenery;
            if (mModel->QueryInterface(&iscenery)) {
                iscenery->RestoreScene();
                mModel = nullptr;
            }
        } else {
            ITriggerableModel *itrigger;
            if (mModel->QueryInterface(&itrigger)) {
                UMath::Matrix4 matrix;
                GetTransform(matrix);
                itrigger->PlaceTrigger(matrix, true);
                Detach(mModel);
                mModel = nullptr;
            }
        }
    } else if (mModel != nullptr && !mPersistant) {
        mModel->ReleaseModel();
        mModel = nullptr;
    }
    Kill();
}

// speed.exe 1.3: 0x66A170. Killed after a second below the world.
void Smackable::ProcessOffWorld(float dT) {
    if (mAttributes.ALLOW_OFF_WORLD()) {
        return;
    }
    if (ValidateWorld()) {
        mOffWorldTimer = 0.0f;
        return;
    }
    mOffWorldTimer += dT;
    if (mOffWorldTimer >= 1.0f) {
        if (mModel != nullptr && !mPersistant) {
            mModel->ReleaseModel();
            mModel = nullptr;
        }
        Kill();
    }
}

// speed.exe 1.3: 0x66A200. Bigger for props far from the camera, out of view
// and small.
void Smackable::CalcSimplificationWeight() {
    if (mCollisionBody == nullptr || mPersistant) {
        mSimplifyWeight = -1.0f;
    }
    float distance = Sim::DistanceToCamera(GetPosition());
    float radius = GetRigidBody()->GetRadius();
    float bias = static_cast<float>(mAttributes.CAN_SIMPLIFY());
    if (!InView()) {
        bias += bias;
    }
    mSimplifyWeight = (bias + distance) / radius;
}

// speed.exe 1.3: 0x670B60
void Smackable::Manage(float dT) {
    ProcessDeath(dT);
    ProcessOffWorld(dT);
    CalcSimplificationWeight();
}

// speed.exe 1.3: 0x67B6F0 (ITaskable)
bool Smackable::OnTask(HSIMTASK htask, float dT) {
    if (htask == mManageTask) {
        Manage(dT);
        return true;
    }
    return PhysicsObject::OnTask(htask, dT);
}

// speed.exe 1.3: 0x684F10. Simple bodies get their gravity here.
void Smackable::OnTaskSimulate(float dT) {
    mAge += dT;
    if (mCollisionBody != nullptr && mAutoSimplify > 0.0f && mAutoSimplify < mAge) {
        Simplify();
    }
    mDroppingOut = ProcessDropout(dT);
    IRigidBody *irb = GetRigidBody();
    if (mSimpleBody != nullptr) {
        UMath::Vector3 gravity;
        gravity.x = 0.0f;
        gravity.y = mRBSpecs->GRAVITY() * irb->GetMass();
        gravity.z = 0.0f;
        irb->ResolveForce(gravity);
    }
}
