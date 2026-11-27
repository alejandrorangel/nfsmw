#ifndef __PHYSICSOBJECT_H__
#define __PHYSICSOBJECT_H__

#include "Speed/Indep/Src/Interfaces/IBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Sim/SimAttachable.h"
#include "Speed/Indep/Src/Sim/SimObject.h"

class Behavior;
struct IPlayer;

DECLARE_CONTAINER_TYPE(ID_POMechanics);
DECLARE_CONTAINER_TYPE(ID_POBehaviors);

// total size: 0xAC
class PhysicsObject : public Sim::Object,
                      public ISimable,
                      public IBody,
                      public IAttachable,
                      public UTL::Collections::GarbageNode<PhysicsObject, 160>,
                      protected UTL::Collections::Container<Behavior, _type_UContainer> {
  public:
    typedef UTL::Std::map<unsigned int, Behavior *, _type_ID_POMechanics> Mechanics;

    // Behaviors in update order (by Behavior::GetPriority)
    struct Behaviors : protected UTL::Std::list<Behavior *, _type_ID_POBehaviors> {
        // total size: 0x8
        typedef UTL::Std::list<Behavior *, _type_ID_POBehaviors> _Base;
        typedef _Base::iterator iterator;

        void Add(Behavior *beh);
        void Remove(Behavior *beh);
        void Reset();

        iterator begin() {
            return _Base::begin();
        }
        iterator end() {
            return _Base::end();
        }
    };

    PhysicsObject(const Attrib::Instance &attribs, SimableType objType, WUID wuid, unsigned int num_interfaces);
    PhysicsObject(const char *attributeClass, const char *attribName, SimableType objType, HSIMABLE owner, WUID wuid);

    // Overrides
    virtual ~PhysicsObject();

    // Sim::Object
    bool OnService(HSIMSERVICE hCon, Sim::Packet *pkt) override;
    virtual void Reset();
    virtual void OnTaskSimulate(float dT) = 0;
    virtual void OnBehaviorChange(const UCrc32 &mechanic);

    // ITaskable
    bool OnTask(HSIMTASK htask, float dT) override;

    // ISimable
    SimableType GetSimableType() const override {
        return mObjType;
    }
    void Kill() override;
    bool Attach(UTL::COM::IUnknown *object) override;
    bool Detach(UTL::COM::IUnknown *object) override;
    const IAttachable::List *GetAttachments() const override;
    void AttachEntity(Sim::IEntity *e) override;
    void DetachEntity() override;
    IPlayer *GetPlayer() const override {
        return mPlayer;
    }
    bool IsPlayer() const override {
        return mPlayer != nullptr;
    }
    bool IsOwnedByPlayer() const override;
    Sim::IEntity *GetEntity() const override {
        return mEntity;
    }
    void DebugObject() override;
    HSIMABLE GetOwnerHandle() const override {
        return mOwner;
    }
    ISimable *GetOwner() const override;
    bool IsOwnedBy(ISimable *queriedOwner) const override;
    void SetOwnerObject(ISimable *pOwner) override;
    const Attrib::Instance &GetAttributes() const override {
        return mAttributes;
    }
    WWorldPos &GetWPos() override {
        return *mWPos;
    }
    const WWorldPos &GetWPos() const override {
        return *mWPos;
    }
    IRigidBody *GetRigidBody() override {
        return mRigidBody;
    }
    const IRigidBody *GetRigidBody() const override {
        return mRigidBody;
    }
    bool IsRigidBodySimple() const override;
    bool IsRigidBodyComplex() const override;
    const UMath::Vector3 &GetPosition() const override;
    void GetTransform(UMath::Matrix4 &matrix) const override;
    void GetLinearVelocity(UMath::Vector3 &velocity) const override;
    void GetAngularVelocity(UMath::Vector3 &velocity) const override;
    unsigned int GetWorldID() const override {
        return mWorldID;
    }
    EventSequencer::IEngine *GetEventSequencer() override {
        return nullptr;
    }
    void ProcessStimulus(unsigned int stimulus) override;
    IModel *GetModel() override = 0;
    const IModel *GetModel() const override = 0;
    void SetCausality(HCAUSE from, float time) override;
    HCAUSE GetCausality() const override;
    float GetCausalityTime() const override;

    // IBody
    void GetDimension(UMath::Vector3 &dim) const override;

    // IAttachable
    bool IsAttached(const UTL::COM::IUnknown *pOther) const override;
    void OnAttached(IAttachable *pOther) override;
    void OnDetached(IAttachable *pOther) override;

  protected:
    void DetachAll();
    void DestroyAttachments() {
        if (mAttachments) {
            delete mAttachments;
            mAttachments = nullptr;
        }
    }
    Behavior *LoadBehavior(const UCrc32 &mechanic, const UCrc32 &behavior, Sim::Param params);
    bool HasBehavior(const UCrc32 &mechanic) const {
        return FindBehavior(mechanic) != nullptr;
    }
    void ReleaseBehavior(const UCrc32 &mechanic);
    void ReleaseBehaviors();
    Behavior *FindBehavior(const UCrc32 &mechanic) const;
    bool ResetBehavior(const UCrc32 &mechanic);
    void PauseBehavior(const UCrc32 &mechanic, bool pause);
    bool IsBehaviorActive(const UCrc32 &mechanic) const;
    Attrib::Instance &GetModifiableAttributes() {
        return mAttributes;
    }

  private:
    void Init(SimableType objType, WUID wuid);

#if defined(EA_PORT_NATIVE)
    // Container<Behavior, _type_UContainer>: every behaviour this object owns
    UTL::Std::list<Behavior *, _type_UContainer> mElements;
#endif
    WWorldPos *mWPos;               // offset 0x58, size 0x4
    SimableType mObjType;           // offset 0x5C, size 0x4
    HSIMABLE mOwner;                // offset 0x60, size 0x4
    Attrib::Instance mAttributes;   // offset 0x64, size 0x14
    IRigidBody *mRigidBody;         // offset 0x78, size 0x4
    HSIMTASK mSimulateTask;         // offset 0x7C, size 0x4
    Sim::IEntity *mEntity;          // offset 0x80, size 0x4
    IPlayer *mPlayer;               // offset 0x84, size 0x4
    HSIMSERVICE mBodyService;       // offset 0x88, size 0x4
    WUID mWorldID;                  // offset 0x8C, size 0x4
    mutable Mechanics mMechanics;   // offset 0x90, size 0x10
    Behaviors mBehaviors;           // offset 0xA0, size 0x8
    Sim::Attachments *mAttachments; // offset 0xA8, size 0x4
};

#endif
