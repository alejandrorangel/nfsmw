#ifndef PHYSICS_PVEHICLE_H
#define PHYSICS_PVEHICLE_H

#include "Speed/Indep/Libs/Support/Utility/UCrc.h"
#include "Speed/Indep/Src/Debug/Debugable.h"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Interfaces/IAttributeable.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/IVehicleCache.h"
#include "Speed/Indep/Src/Interfaces/Simables/IExplodeable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"
#include "Speed/Indep/Src/Physics/PhysicsObject.h"
#include "Speed/Indep/Src/Sim/SimTypes.h"
#include "Speed/Indep/bWare/Inc/bList.hpp"

namespace VehicleClass {

extern const UCrc32 CAR;
extern const UCrc32 SUBMARINE;
extern const UCrc32 CHOPPER;
extern const UCrc32 BIKE;
extern const UCrc32 BOAT;
extern const UCrc32 SNOWMOBILE;
extern const UCrc32 HOVER;
extern const UCrc32 PLANE;
extern const UCrc32 TANK;
extern const UCrc32 TRAILER;
extern const UCrc32 TRAIN;
extern const UCrc32 TRANSPORT;
extern const UCrc32 RC;
extern const UCrc32 TRACTOR;

}; // namespace VehicleClass

// total size: 0x30
struct VehicleParams : public Sim::Param {
    VehicleParams(IVehicleCache *cache, DriverClass driver, unsigned int type, const UMath::Vector3 &dir, const UMath::Vector3 &pos,
                  unsigned int flags, const FECustomizationRecord *upgrades, const Physics::Info::Performance *match)
        : Sim::Param(UCrc32(UCRC32_BASE), this), //
          carClass(driver),                      //
          carType(type),                         //
          initialVec(dir),                       //
          initialPos(pos),                       //
          customization(upgrades),               //
          VehicleCache(cache),                   //
          matched(match),                        //
          Flags(flags) {}

    static UCrc32 TypeName() {
        static UCrc32 value = "VehicleParams";
        return value;
    }

    DriverClass carClass;                       // offset 0x10, size 0x4
    unsigned int carType;                       // offset 0x14, size 0x4
    const UMath::Vector3 &initialVec;           // offset 0x18, size 0x4
    const UMath::Vector3 &initialPos;           // offset 0x1C, size 0x4
    const FECustomizationRecord *customization; // offset 0x20, size 0x4
    IVehicleCache *VehicleCache;                // offset 0x24, size 0x4
    const Physics::Info::Performance *matched;  // offset 0x28, size 0x4
    unsigned int Flags;                         // offset 0x2C, size 0x4
};

#if defined(EA_PORT_NATIVE)
DECLARE_CONTAINER_TYPE(ID_PVehicleChangeReq);

namespace CollisionGeometry {
struct Bounds;
};

class IInput;
class ICollisionBody;
class ISuspension;
class IEngine;
class IDamageable;
class ITransmission;
class IVehicleAI;
class IArticulatedVehicle;
class IRenderable;
class IAudible;

// ISimable::GetPosition and IVehicle::GetPosition share a signature but not a
// return type; MSVC gave each its own slot. Route IVehicle's through a
// differently named virtual so one class can implement both.
class PVehicleIVehicle : public IVehicle {
  protected:
    PVehicleIVehicle(UTL::COM::Object *owner) : IVehicle(owner) {}
    ~PVehicleIVehicle() override {}

  public:
    UMath::Vector3 &GetPosition() const final {
        return GetVehiclePosition();
    }
    virtual UMath::Vector3 &GetVehiclePosition() const = 0;
};

// total size: 0x1AC (speed.exe 1.3)
class PVehicle : public PhysicsObject,
                 public bTNode<PVehicle>,
                 public PVehicleIVehicle,
                 public Debugable,
                 public EventSequencer::IContext,
                 public IExplodeable,
                 public IAttributeable {
  public:
    // total size: 0xC
    struct Resource {
        enum {
            VALID = 1 << 0,      // costs car loader memory (cleared on Kill)
            SPOOLED = 1 << 1,
            COMPOSITED = 1 << 2, // skinned: needs the compositing buffer
        };

        Resource() : Type(CARTYPE_NONE), Cost(0), Flags(0) {}
        Resource(const Attrib::Gen::pvehicle &pvehicle, bool spool, bool is_player);

        bool NeedsCompositing() const {
            return (Flags & COMPOSITED) != 0;
        }
        bool IsValid() const {
            return (Flags & VALID) != 0;
        }
        bool IsSpooled() const {
            return (Flags & SPOOLED) != 0;
        }
        void Invalidate() {
            Flags &= ~VALID;
        }

        CarType Type;       // offset 0x0, size 0x4
        unsigned int Cost;  // offset 0x4, size 0x4
        unsigned int Flags; // offset 0x8, size 0x4
    };

    // total size: 0x8
    struct LaunchState {
        LaunchState() : Time(0.0f), Amount(0.0f) {}

        void Clear() {
            Time = 0.0f;
            Amount = 0.0f;
        }
        bool IsSet() const {
            return Time > 0.0f;
        }
        void Set(float amount) {
            Time = 4.5f;
            Amount = amount;
        }
        void Tick(float dT) {
            Time -= dT;
            if (Time <= 0.0f) {
                Clear();
            }
        }

        float Time;   // offset 0x0, size 0x4
        float Amount; // offset 0x4, size 0x4
    };

    typedef UTL::Std::list<Resource, _type_list> ResourceList;
    typedef UTL::Std::map<UCrc32, UCrc32, _type_ID_PVehicleChangeReq> BehaviorOverrides;

    static ISimable *Construct(Sim::Param params);
    static bool MakeRoom(IVehicleCache *whosasking, const ResourceList &resources);
    static void CleanResources();
    static unsigned int CountResources();

    PVehicle(DriverClass carClass, const Attrib::Gen::pvehicle &attributes, const UMath::Vector3 &initialVec, const UMath::Vector3 &initialPos,
             const CollisionGeometry::Bounds *bounds, const FECustomizationRecord *customization, const Resource &resource,
             const Physics::Info::Performance *performance, const char *cache_name);
    virtual ~PVehicle();

    // Sim::Object / PhysicsObject
    void Reset() override;
    void OnTaskSimulate(float dT) override;
    void OnBehaviorChange(const UCrc32 &mechanic) override;
    virtual void OnDebugDraw() {}
    bool OnTask(HSIMTASK htask, float dT) override;

    // ISimable
    void Kill() override;
    void DebugObject() override;
    // ISimable::GetPosition is PhysicsObject's (speed.exe 1.3: 0x688010 is the same code)
    EventSequencer::IEngine *GetEventSequencer() override {
        return mSequencer;
    }
    IModel *GetModel() override;
    const IModel *GetModel() const override;

    // IVehicle
    const ISimable *GetSimable() const override {
        return this;
    }
    ISimable *GetSimable() override {
        return this;
    }
    UMath::Vector3 &GetVehiclePosition() const override {
        return const_cast<UMath::Vector3 &>(PhysicsObject::GetPosition());
    }
    void SetBehaviorOverride(UCrc32 mechanic, UCrc32 behavior) override;
    void RemoveBehaviorOverride(UCrc32 mechanic) override;
    void CommitBehaviorOverrides() override;
    void SetStaging(bool staging) override;
    bool IsStaging() const override {
        return mStaging;
    }
    void Launch() override;
    float GetPerfectLaunch() const override;
    void SetDriverStyle(DriverStyle style) override;
    DriverStyle GetDriverStyle() const override {
        return mDriverStyle;
    }
    void SetPhysicsMode(PhysicsMode mode) override;
    PhysicsMode GetPhysicsMode() const override {
        return mPhysicsMode;
    }
    CarType GetModelType() const override {
        return mResources.Type;
    }
    bool IsSpooled() const override {
        return mResources.IsSpooled();
    }
    const UCrc32 &GetVehicleClass() const override {
        return mClass;
    }
    Attrib::Gen::pvehicle &GetVehicleAttributes() const override {
        return const_cast<Attrib::Gen::pvehicle &>(mAttributes);
    }
    const char *GetVehicleName() const override {
        return mAttributes.CollectionName();
    }
    unsigned int GetVehicleKey() const override {
        return mAttributes.GetCollection();
    }
    void SetDriverClass(DriverClass dc) override;
    DriverClass GetDriverClass() const override {
        return mDriverClass;
    }
    bool IsLoading() const override;
    float GetOffscreenTime() const override {
        return mOffScreenTime;
    }
    float GetOnScreenTime() const override {
        return mOnScreenTime;
    }
    bool SetVehicleOnGround(const UMath::Vector3 &resetPos, const UMath::Vector3 &initialVec) override;
    void ForceStopOn(char forceStopBits) override;
    void ForceStopOff(char forceStopBits) override;
    char GetForceStop() override {
        return mForceStop;
    }
    bool InShock() const override;
    bool IsDestroyed() const override;
    void Activate() override;
    void Deactivate() override;
    bool IsActive() const override {
        return mPhysicsMode != PHYSICS_MODE_INACTIVE;
    }
    float GetSpeedometer() const override {
        return mSpeedometer;
    }
    float GetSpeed() const override {
        return mSpeed;
    }
    void SetSpeed(float speed) override;
    float GetAbsoluteSpeed() const override {
        return mAbsSpeed;
    }
    bool IsGlareOn(VehicleFX::ID glare) const override {
        return (mGlareState & glare) != 0;
    }
    void GlareOn(VehicleFX::ID glare) override {
        mGlareState |= glare;
    }
    void GlareOff(VehicleFX::ID glare) override {
        mGlareState &= ~glare;
    }
    bool IsCollidingWithSoftBarrier() override {
        return false;
    }
    IVehicleAI *GetAIVehiclePtr() const override {
        return mAI;
    }
    float GetSlipAngle() const override {
        return mSlipAngle;
    }
    UMath::Vector3 &GetLocalVelocity() const override {
        return const_cast<UMath::Vector3 &>(mLocalVel);
    }
    void ComputeHeading(UMath::Vector3 *v) override;
    bool IsAnimating() const override {
        return mAnimating;
    }
    void SetAnimating(bool animate) override;
    bool IsOffWorld() const override {
        return mOffWorld;
    }
    const FECustomizationRecord *GetCustomizations() const override {
        return mCustomization;
    }
    const Physics::Tunings *GetTunings() const override;
    void SetTunings(const Physics::Tunings &tunings) override;
    bool GetPerformance(Physics::Info::Performance &performance) const override;
    const char *GetCacheName() const override {
        return mCacheName;
    }

    // IContext
    bool SetDynamicData(const EventSequencer::System *system, EventDynamicData *data) override;

    // IExplodeable
    bool OnExplosion(const UMath::Vector3 &normal, const UMath::Vector3 &position, float dT, IExplosion *explosion) override;

    // IAttributeable
    void OnAttributeChange(const Attrib::Collection *collection, unsigned int attribkey) override {}

  private:
    void OnBeginMode(PhysicsMode mode);
    void OnEndMode(PhysicsMode mode);
    void DoDebug(float dT) {}
    void DoStaging(float dT);
    void CheckOffWorld();
    void LoadBehaviors(const UMath::Vector3 &initialPos, const UMath::Matrix4 &initMat);
    UCrc32 LookupBehaviorSignature(const Attrib::StringKey &mechanic) const;
    void ReloadBehaviors();
    void UpdateLocalVelocities();
    void OnDisableModeling();
    void OnEnableModeling() {}
    void UpdateListing();
    void OnTaskFX(float dT);

    Attrib::Gen::pvehicle mAttributes;           // offset 0xD0, size 0x14
    FECustomizationRecord *mCustomization;       // offset 0xE4, size 0x4
    IInput *mInput;                              // offset 0xE8, size 0x4
    ICollisionBody *mCollisionBody;              // offset 0xEC, size 0x4
    ISuspension *mSuspension;                    // offset 0xF0, size 0x4
    IEngine *mEngine;                            // offset 0xF4, size 0x4
    IDamageable *mDamage;                        // offset 0xF8, size 0x4
    ITransmission *mTranny;                      // offset 0xFC, size 0x4
    IVehicleAI *mAI;                             // offset 0x100, size 0x4
    IArticulatedVehicle *mArticulation;          // offset 0x104, size 0x4
    IRenderable *mRenderable;                    // offset 0x108, size 0x4
    IAudible *mAudible;                          // offset 0x10C, size 0x4
    EventSequencer::IEngine *mSequencer;         // offset 0x110, size 0x4
    HSIMTASK mTaskFX;                            // offset 0x114, size 0x4
    UCrc32 mClass;                               // offset 0x118, size 0x4
    float mSpeed;                                // offset 0x11C, size 0x4
    float mAbsSpeed;                             // offset 0x120, size 0x4
    float mSpeedometer;                          // offset 0x124, size 0x4
    float mTimeInAir;                            // offset 0x128, size 0x4
    float mSlipAngle;                            // offset 0x12C, size 0x4
    unsigned int mWheelsOnGround;                // offset 0x130, size 0x4
    UMath::Vector3 mLocalVel;                    // offset 0x134, size 0xC
    DriverClass mDriverClass;                    // offset 0x140, size 0x4
    DriverStyle mDriverStyle;                    // offset 0x144, size 0x4
    unsigned int mGlareState;                    // offset 0x148, size 0x4
    float mStartingNOS;                          // offset 0x14C, size 0x4
    float mBrakeTime;                            // offset 0x150, size 0x4
    char mForceStop;                             // offset 0x154, size 0x1
    PhysicsMode mPhysicsMode;                    // offset 0x158, size 0x4
    bool mAnimating;                             // offset 0x15C, size 0x1
    bool mStaging;                               // offset 0x15D, size 0x1
    LaunchState mPerfectLaunch;                  // offset 0x160, size 0x8
    BehaviorOverrides mBehaviorOverrides;        // offset 0x168, size 0xC
    bool mOverrideDirty;                         // offset 0x174, size 0x1
    const CollisionGeometry::Bounds *mBounds;    // offset 0x178, size 0x4
    bool mIsModeling;                            // offset 0x17C, size 0x1
    float mOffScreenTime;                        // offset 0x180, size 0x4
    float mOnScreenTime;                         // offset 0x184, size 0x4
    bool mOffWorld;                              // offset 0x188, size 0x1
    bool mHasDyno;                               // offset 0x189, size 0x1
    Resource mResources;                         // offset 0x18C, size 0xC
    Physics::Info::Performance mPerformance;     // offset 0x198, size 0xC
    bool mPerformanceValid;                      // offset 0x1A4, size 0x1
    const char *mCacheName;                      // offset 0x1A8, size 0x4

    static bool mRunDyno;
    static bTList<PVehicle> mInstances;
};
#endif // EA_PORT_NATIVE

#endif
