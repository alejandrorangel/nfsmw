// Rigid-body articulation (joints + angular constraints), used for tractor /
// trailer hitches. Reconstructed from speed.exe 1.3 for the native port.

#include "Speed/Indep/Src/Physics/Dynamics/Articulation.h"
#include "Speed/Indep/Libs/Support/Utility/FastMem.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Libs/Support/Utility/UStandard.h"
#include "Speed/Indep/Libs/Support/Utility/UVector.h"
#include "Speed/Indep/Src/Debug/Debugable.h"
#include "Speed/Indep/Src/Physics/Dynamics.h"
#include "Speed/Indep/Src/Physics/Dynamics/Collision.h"
#include "Speed/Indep/bWare/Inc/bList.hpp"

namespace Dynamics {
namespace Articulation {

// total size: 0x10
struct Quaternion : public UMath::Vector4 {
    Quaternion() {}

    // speed.exe 1.3: 0x487370 (angle in revolutions)
    Quaternion(const UMath::Vector3 &axis, float angle) {
        float half = angle * 0.5f * (2.0f * MATH_PI);
        float s = sinf(half);
        w = cosf(half);
        x = s * axis.x;
        y = s * axis.y;
        z = s * axis.z;
    }

    // speed.exe 1.3: 0x48AD40, shortest arc rotating 'from' onto 'to' (both unit)
    Quaternion(const UMath::Vector3 &from, const UMath::Vector3 &to) {
        float d = to.z * from.z + to.y * from.y + to.x * from.x;
        if (!(d < 1.0f)) {
            x = y = z = 0.0f;
            w = 1.0f;
            return;
        }
        UMath::Vector3 c;
        UMath::Cross(from, to, c);
        if (d < -0.99999f) {
            x = c.x;
            y = c.y;
            z = c.z;
            w = 0.0f;
            UMath::Normalize(*this);
            return;
        }
        float s = UMath::Sqrt((d + 1.0f) + (d + 1.0f));
        float inv = 1.0f / s;
        x = c.x * inv;
        y = c.y * inv;
        z = inv * c.z;
        w = s * 0.5f;
    }

    void Conjugate(Quaternion &r) const {
        r.x = -x;
        r.y = -y;
        r.z = -z;
        r.w = w;
    }
};

// speed.exe's quaternion product (0x4866C0, the out-of-line VU0_qmul):
// QMul(a, b, r) gives the Hamilton product b * a.
static inline void QMul(const UMath::Vector4 &a, const UMath::Vector4 &b, UMath::Vector4 &r) {
    VU0_qmul(a, b, r);
}

// 0x5CAEA0: r = v rotated by q (safe in place, as the PC routine is)
static inline void QRotate(const UMath::Vector3 &v, const UMath::Vector4 &q, UMath::Vector3 &r) {
    UMath::Vector3 tmp = v;
    UMath::Rotate(tmp, q, r);
}

// 0x5CB0B0: r = Rotate(v, q) + t
static inline void RotateTranslate(const UMath::Vector4 &q, const UMath::Vector3 &v, const UMath::Vector3 &t, UMath::Vector3 &r) {
    QRotate(v, q, r);
    UMath::Add(r, t, r);
}

// total size: 0x18
class Lever : public Debugable {
  public:
    // speed.exe 1.3: 0x4873B0
    Lever(IEntity *entity, const UMath::Vector3 &arm, bool immobile) : mArm(arm), mEntity(entity), mImmoblie(immobile) {}

    IEntity *GetEntity() const {
        return mEntity;
    }

    bool IsImmobile() const {
        return mImmoblie;
    }

    const UVector3 &GetArm() const {
        return mArm;
    }

    void SetFulcrum(const UVector3 &fulcrum, bool rotate);

    // speed.exe 1.3: 0x4874E0
    virtual void OnDebugDraw() {}

  private:
    UVector3 mArm;    // offset 0x4, size 0xC
    IEntity *mEntity; // offset 0x10, size 0x4
    bool mImmoblie;   // offset 0x14, size 0x1
};

// total size: 0x68
class Constraint : public Debugable {
  public:
    USE_FASTALLOC(Constraint)

    Constraint(const UMath::Matrix4 &orient, float minTheta, float maxTheta, Lever &female, Lever &male, const UMath::Vector3 &post,
               eConstraint type);

    void Resolve(const UVector3 &joint_fulcrum);

    // speed.exe 1.3: 0x4874F0
    virtual void OnDebugDraw() {}

  protected:
    bool React(const UVector3 &joint_collision_normal, const UVector3 &joint_collision_point, const UVector3 &joint_fulcrum, float deltaA,
               const Quaternion &joint_rotation, const Quaternion &joint_rotationInv);
    static void Rotate(Lever &lever, const Quaternion &joint_rotation, const Quaternion &joint_rotationInv, const UVector3 &joint_fulcrum,
                       float angle);

  private:
    Quaternion mOrientation; // offset 0x4, size 0x10
    UVector3 mInnerN;        // offset 0x14, size 0xC
    UVector3 mInnerR;        // offset 0x20, size 0xC
    UVector3 mOuterN;        // offset 0x2C, size 0xC
    UVector3 mOuterR;        // offset 0x38, size 0xC
    Lever &mFemale;          // offset 0x44, size 0x4
    Lever &mMale;            // offset 0x48, size 0x4
    UVector3 mPost;          // offset 0x4C, size 0xC
    float mInnerA;           // offset 0x58, size 0x4
    float mOuterA;           // offset 0x5C, size 0x4
    bool mEnabled;           // offset 0x60, size 0x1
    eConstraint mType;       // offset 0x64, size 0x4
};

DECLARE_CONTAINER_TYPE(ArticulationConstraints);

// total size: 0x4C
class Joint : public bTNode<Joint>, public Debugable {
  public:
    USE_FASTALLOC(Joint)

    Joint(IEntity *female, const UMath::Vector3 &female_arm, IEntity *male, const UMath::Vector3 &male_arm, eJointFlags flags);
    ~Joint();

    void Resolve();
    void AddConstraint(IEntity *female, const UMath::Matrix4 &orient, float minTheta, float maxTheta, const UMath::Vector3 &post,
                       eConstraint type);

    // speed.exe 1.3: 0x4874B0
    virtual void OnDebugDraw() {}

    HJOINT GetHandle() const {
        return mHandle;
    }

    bool Owns(const IEntity *rb) const {
        return rb == mFemale.GetEntity() || rb == mMale.GetEntity();
    }

  private:
    typedef UTL::Std::list<Constraint *, _type_ArticulationConstraints> ConstraintList;

    Lever mFemale;                 // offset 0xC, size 0x18
    Lever mMale;                   // offset 0x24, size 0x18
    HJOINT mHandle;                // offset 0x3C, size 0x4
    static HJOINT mNextHandle;     // speed.exe 1.3: 0x8EE4C0
    ConstraintList mConstraints;   // offset 0x40, size 0xC
};

// total size: 0x8
class JoinList : public bTList<Joint> {
  public:
    // speed.exe 1.3: 0x48AE30
    Joint *Find(HJOINT hJoint) {
        for (Joint *joint = GetHead(); joint != EndOfList(); joint = joint->GetNext()) {
            if (joint->GetHandle() == hJoint) {
                return joint;
            }
        }
        return nullptr;
    }
};

HJOINT Joint::mNextHandle = reinterpret_cast<HJOINT>(1);
static JoinList Joints; // speed.exe 1.3: 0x91132C

// speed.exe 1.3: 0x48AF30
void Lever::SetFulcrum(const UVector3 &fulcrum, bool rotate) {
    if (rotate) {
        UVector3 arm;
        UMath::Rotate(mArm, mEntity->GetRotation(), arm);
        UVector3 armdir = arm;
        if (UMath::Normalize(armdir) > 0.0f) {
            UVector3 dir = fulcrum - mEntity->GetPosition();
            if (UMath::Normalize(dir) != 0.0f) {
                Quaternion rotation(armdir, dir);
                UMath::Vector4 orientation;
                QMul(mEntity->GetOrientation(), rotation, orientation);
                mEntity->SetOrientation(orientation);
            }
        }
    }
    UVector3 arm;
    UMath::Rotate(mArm, mEntity->GetRotation(), arm);
    mEntity->SetPosition(fulcrum - arm);
}

// speed.exe 1.3: 0x48B070 (angles in degrees)
Constraint::Constraint(const UMath::Matrix4 &orient, float minTheta, float maxTheta, Lever &female, Lever &male, const UMath::Vector3 &post,
                       eConstraint type)
    : mInnerN(0.0f, 0.0f, 0.0f), //
      mInnerR(0.0f, 0.0f, 0.0f), //
      mOuterN(0.0f, 0.0f, 0.0f), //
      mOuterR(0.0f, 0.0f, 0.0f), //
      mFemale(female),           //
      mMale(male),               //
      mPost(post),               //
      mInnerA(minTheta * (1.0f / 360.0f)),
      mOuterA(maxTheta * (1.0f / 360.0f)), //
      mEnabled(true),                      //
      mType(type) {
    UMath::Matrix4ToQuaternion(orient, mOrientation);
    float length = UMath::Sqrt(post.x * post.x + post.y * post.y + post.z * post.z);

    float a = mInnerA * (2.0f * MATH_PI);
    float c = cosf(a);
    float s = sinf(a);
    mInnerN.x = -c;
    mInnerN.y = 0.0f;
    mInnerN.z = s;
    mInnerR.x = s * length;
    mInnerR.y = 0.0f;
    mInnerR.z = c * length;

    a = mOuterA * (2.0f * MATH_PI);
    c = cosf(a);
    s = sinf(a);
    mOuterN.x = c;
    mOuterN.y = 0.0f;
    mOuterN.z = s;
    mOuterR.x = -(s * length);
    mOuterR.y = 0.0f;
    mOuterR.z = c * length;
}

// speed.exe 1.3: 0x48B190
// Spins the lever's body by 'angle' (revolutions) about the joint frame's Y
// axis, keeping its arm on the fulcrum.
void Constraint::Rotate(Lever &lever, const Quaternion &joint_rotation, const Quaternion &joint_rotationInv, const UVector3 &joint_fulcrum,
                        float angle) {
    IEntity *entity = lever.GetEntity();
    Quaternion spin(UVector3(0.0f, 1.0f, 0.0f), angle);

    UMath::Vector4 local;
    UMath::Vector4 rotated;
    QMul(entity->GetOrientation(), joint_rotationInv, local);
    QMul(local, spin, rotated);
    QMul(rotated, joint_rotation, local);
    UMath::Unit(local, local);
    entity->SetOrientation(local);

    UVector3 arm;
    UMath::Rotate(lever.GetArm(), entity->GetRotation(), arm);
    entity->SetPosition(joint_fulcrum - arm);
}

// speed.exe 1.3: 0x48B290
bool Constraint::React(const UVector3 &joint_collision_normal, const UVector3 &joint_collision_point, const UVector3 &joint_fulcrum, float deltaA,
                       const Quaternion &joint_rotation, const Quaternion &joint_rotationInv) {
    IEntity *female = mFemale.GetEntity();
    IEntity *male = mMale.GetEntity();

    if (mFemale.IsImmobile()) {
        Rotate(mMale, joint_rotation, joint_rotationInv, joint_fulcrum, deltaA);
        UVector3 w = male->GetAngularVelocity();
        QRotate(w, joint_rotationInv, w);
        if (w.y * deltaA < 0.0f) {
            w.y = 0.0f;
            QRotate(w, joint_rotation, w);
            male->SetAngularVelocity(w);
        }
        return true;
    }

    if (mMale.IsImmobile()) {
        Rotate(mFemale, joint_rotation, joint_rotationInv, joint_fulcrum, deltaA * -1.0f);
        UVector3 w = female->GetAngularVelocity();
        QRotate(w, joint_rotationInv, w);
        if (w.y * deltaA > 0.0f) {
            w.y = 0.0f;
            QRotate(w, joint_rotation, w);
            female->SetAngularVelocity(w);
        }
        return true;
    }

    float ratio = female->GetMass();
    ratio = ratio / (female->GetMass() + male->GetMass());
    Rotate(mFemale, joint_rotation, joint_rotationInv, joint_fulcrum, -((1.0f - ratio) * deltaA));
    Rotate(mMale, joint_rotation, joint_rotationInv, joint_fulcrum, ratio * deltaA);

    Collision::Plane plane;
    plane.point = UVector3(0.0f, 0.0f, 0.0f);
    plane.normal = joint_collision_normal;
    QRotate(plane.normal, joint_rotation, plane.normal);
    RotateTranslate(joint_rotation, joint_collision_point, joint_fulcrum, plane.point);

    Collision::Moment moFemale(female);
    moFemale.MakeImmobile(mFemale.IsImmobile(), 0.0f);
    Collision::Moment moMale(male);
    moMale.MakeImmobile(mMale.IsImmobile(), 0.0f);

    if (moMale.React(moFemale, plane, 32)) {
        female->SetLinearVelocity(moFemale.GetLinearVelocity());
        female->SetAngularVelocity(moFemale.GetAngularVelocity());
        male->SetLinearVelocity(moMale.GetLinearVelocity());
        male->SetAngularVelocity(moMale.GetAngularVelocity());
    }
    return true;
}

// speed.exe 1.3: 0x48C270
void Constraint::Resolve(const UVector3 &joint_fulcrum) {
    if (!mEnabled) {
        return;
    }
    IEntity *female = mFemale.GetEntity();
    IEntity *male = mMale.GetEntity();

    // male post tip relative to the fulcrum
    UVector3 malepos = male->GetPosition();
    UVector3 post = mMale.GetArm();
    post += mPost;
    UVector3 tip;
    UMath::Rotate(post, male->GetRotation(), tip);
    UVector3 delta;
    delta.x = (tip.x + malepos.x) - joint_fulcrum.x;
    delta.y = (tip.y + malepos.y) - joint_fulcrum.y;
    delta.z = (tip.z + malepos.z) - joint_fulcrum.z;

    const UMath::Vector4 &female_orientation = female->GetOrientation();
    Quaternion joint_rotation;
    Quaternion joint_rotationInv;
    QMul(mOrientation, female_orientation, joint_rotation);
    joint_rotation.Conjugate(joint_rotationInv);

    if (mType != PRISMATIC) {
        // let the post swing freely off the constraint plane
        UVector3 local(0.0f, 0.0f, 0.0f);
        QRotate(delta, joint_rotationInv, local);
        Quaternion swing(UVector3(1.0f, 0.0f, 0.0f), -VU0_Atan2(local.y, local.z));
        if (mType == CONICAL) {
            Quaternion twist(UVector3(0.0f, 0.0f, 1.0f), VU0_Atan2(local.y, local.x));
            QMul(swing, twist, swing);
        }
        UMath::Vector4 frame;
        QMul(swing, mOrientation, frame);
        QMul(frame, female_orientation, joint_rotation);
        joint_rotation.Conjugate(joint_rotationInv);
    }

    QRotate(delta, joint_rotationInv, delta);
    float angle = VU0_Atan2(delta.x, delta.z);
    if (angle > mInnerA) {
        React(mInnerN, mInnerR, joint_fulcrum, mInnerA - angle, joint_rotation, joint_rotationInv);
    } else if (angle < -mOuterA) {
        React(mOuterN, mOuterR, joint_fulcrum, -mOuterA - angle, joint_rotation, joint_rotationInv);
    }
}

// speed.exe 1.3: 0x48CB90
Joint::Joint(IEntity *female, const UMath::Vector3 &female_arm, IEntity *male, const UMath::Vector3 &male_arm, eJointFlags flags)
    : mFemale(female, female_arm, (flags & JF_IMMOBILE_FEMALE) != 0), //
      mMale(male, male_arm, (flags & JF_IMMOBILE_MALE) != 0),         //
      mHandle(mNextHandle) {
    mNextHandle++;
}

// speed.exe 1.3: 0x48CA40
Joint::~Joint() {
    for (ConstraintList::iterator iter = mConstraints.begin(); iter != mConstraints.end(); ++iter) {
        delete *iter;
    }
    mConstraints.clear();
}

// speed.exe 1.3: 0x48C5D0
void Joint::Resolve() {
    IEntity *female = mFemale.GetEntity();
    IEntity *male = mMale.GetEntity();
    if (!female || !male) {
        return;
    }
    if (mFemale.IsImmobile() && mMale.IsImmobile()) {
        return;
    }

    UVector3 femalepos = female->GetPosition();
    UVector3 arm = mFemale.GetArm();
    UVector3 worldarm;
    UMath::Rotate(arm, female->GetRotation(), worldarm);
    UVector3 femalepoint = worldarm + femalepos;

    UVector3 malepos = male->GetPosition();
    arm = mMale.GetArm();
    UMath::Rotate(arm, male->GetRotation(), worldarm);
    UVector3 malepoint = worldarm + malepos;

    // the fulcrum sits between the two attachment points, biased toward the heavier body
    float ratio;
    if (mMale.IsImmobile()) {
        ratio = 0.0f;
    } else if (mFemale.IsImmobile()) {
        ratio = 1.0f;
    } else {
        float femalemass = female->GetMass();
        float malemass = male->GetMass();
        ratio = femalemass / (female->GetMass() + malemass);
    }
    UVector3 fulcrum = femalepoint * ratio + malepoint * (1.0f - ratio);

    mFemale.SetFulcrum(fulcrum, !mFemale.IsImmobile());
    mMale.SetFulcrum(fulcrum, !mMale.IsImmobile());

    Collision::Joint joint;
    joint.fulcrum = fulcrum;
    if (mMale.IsImmobile()) {
        Collision::Moment mo(female);
        if (mo.React(joint, 32)) {
            female->SetLinearVelocity(mo.GetLinearVelocity());
            female->SetAngularVelocity(mo.GetAngularVelocity());
        }
    } else if (mFemale.IsImmobile()) {
        Collision::Moment mo(male);
        if (mo.React(joint, 32)) {
            male->SetLinearVelocity(mo.GetLinearVelocity());
            male->SetAngularVelocity(mo.GetAngularVelocity());
        }
    } else {
        Collision::Moment moFemale(female);
        Collision::Moment moMale(male);
        if (moFemale.React(moMale, joint, 32)) {
            female->SetLinearVelocity(moFemale.GetLinearVelocity());
            female->SetAngularVelocity(moFemale.GetAngularVelocity());
            male->SetLinearVelocity(moMale.GetLinearVelocity());
            male->SetAngularVelocity(moMale.GetAngularVelocity());
        }
    }

    for (ConstraintList::iterator iter = mConstraints.begin(); iter != mConstraints.end(); ++iter) {
        (*iter)->Resolve(fulcrum);
    }
}

// speed.exe 1.3: 0x48D050
void Joint::AddConstraint(IEntity *female, const UMath::Matrix4 &orient, float minTheta, float maxTheta, const UMath::Vector3 &post,
                          eConstraint type) {
    if (female == mFemale.GetEntity()) {
        mConstraints.push_back(new Constraint(orient, minTheta, maxTheta, mFemale, mMale, post, type));
    } else {
        mConstraints.push_back(new Constraint(orient, minTheta, maxTheta, mMale, mFemale, post, type));
    }
}

// speed.exe 1.3: 0x48CDD0
HJOINT Create(IEntity *female, const UMath::Vector3 &female_arm, IEntity *male, const UMath::Vector3 &male_arm, eJointFlags flags) {
    Release(female);
    Release(male);
    Joint *joint = new Joint(female, female_arm, male, male_arm, flags);
    Joints.AddTail(joint);
    return joint->GetHandle();
}

// speed.exe 1.3: 0x48CE70
HJOINT Create(IEntity *female, IEntity *male, const UMath::Vector3 &worldpos, eJointFlags flags) {
    Release(female);
    Release(male);

    UMath::Matrix4 femaleInv = female->GetRotation();
    femaleInv.v3 = UMath::Vector4Make(female->GetPosition(), 1.0f);
    OrthoInverse(femaleInv);

    UMath::Matrix4 maleInv = male->GetRotation();
    maleInv.v3 = UMath::Vector4Make(male->GetPosition(), 1.0f);
    OrthoInverse(maleInv);

    UMath::Vector3 female_arm;
    UMath::Vector3 male_arm;
    UMath::RotateTranslate(worldpos, femaleInv, female_arm);
    UMath::RotateTranslate(worldpos, maleInv, male_arm);

    Joint *joint = new Joint(female, female_arm, male, male_arm, flags);
    Joints.AddTail(joint);
    return joint->GetHandle();
}

// speed.exe 1.3: 0x48D1A0
void Constrain(HJOINT hJoint, IEntity *female, const UMath::Matrix4 &orient, float minTheta, float maxTheta, const UMath::Vector3 &post,
               eConstraint type) {
    Joint *joint = Joints.Find(hJoint);
    if (joint) {
        joint->AddConstraint(female, orient, minTheta, maxTheta, post, type);
    }
}

// speed.exe 1.3: 0x48AE80
bool IsJoined(const IEntity *rb0, const IEntity *rb1) {
    for (Joint *joint = Joints.GetHead(); joint != Joints.EndOfList(); joint = joint->GetNext()) {
        if (joint->Owns(rb0) && joint->Owns(rb1)) {
            return true;
        }
    }
    return false;
}

// speed.exe 1.3: 0x48AEE0
bool IsJoined(const IEntity *rb) {
    for (Joint *joint = Joints.GetHead(); joint != Joints.EndOfList(); joint = joint->GetNext()) {
        if (joint->Owns(rb)) {
            return true;
        }
    }
    return false;
}

// speed.exe 1.3: 0x48C910
void Resolve() {
    for (Joint *joint = Joints.GetHead(); joint != Joints.EndOfList(); joint = joint->GetNext()) {
        joint->Resolve();
    }
}

// speed.exe 1.3: 0x48CB30
void Release(HJOINT hJoint) {
    Joint *joint = Joints.Find(hJoint);
    if (joint) {
        joint->Remove();
        delete joint;
    }
}

// speed.exe 1.3: 0x48CD20
void Release(IEntity *rb0) {
    Joint *joint = Joints.GetHead();
    while (joint != Joints.EndOfList()) {
        Joint *next = joint->GetNext();
        if (joint->Owns(rb0)) {
            joint->Remove();
            delete joint;
        }
        joint = next;
    }
}

} // namespace Articulation
} // namespace Dynamics
