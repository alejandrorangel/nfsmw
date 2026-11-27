// Dynamics::Collision -- rigid body impulse response.
// Reconstructed from speed.exe 1.3 (PC addresses noted per function); local
// names follow the PS2 debug info.

#include "Speed/Indep/Src/Physics/Dynamics/Collision.h"

namespace Dynamics {
namespace Collision {

typedef UVector3 Velocity;
typedef UVector3 Acceleration;
typedef UVector3 Force;
typedef UVector3 Momentum;

// PC 0x4886F0
// Splits the momentum P into the part tangential to n and returns the friction
// impulse opposing it (static: cancels it, dynamic: clamped to uk * impulse).
Friction::State Friction::GetForce(const UVector3 &P, float impulse, const UVector3 &n, UVector3 &Ff) const {
    impulse = -impulse;
    Ff.x = 0.0f;
    Ff.y = 0.0f;
    Ff.z = 0.0f;
    if (impulse <= 0.0f) {
        return None;
    }

    Ff.x = impulse * n.x + P.x;
    Ff.y = impulse * n.y + P.y;
    Ff.z = impulse * n.z + P.z;

    float fy = UMath::Sqrt(Ff.x * Ff.x + Ff.y * Ff.y + Ff.z * Ff.z);
    if (fy > 0.0f) {
        if (impulse * mUs < fy) {
            float scalar = -(impulse * mUk) / fy;
            Ff.x = scalar * Ff.x;
            Ff.y = scalar * Ff.y;
            Ff.z = scalar * Ff.z;
            return Dynamic;
        }
        Ff.x = -Ff.x;
        Ff.y = -Ff.y;
        Ff.z = -Ff.z;
    }
    return Static;
}

// PC 0x487000
Moment::Moment(const UMath::Matrix4 &orientation, float mass, const UMath::Vector3 &inertia, const UMath::Vector3 &cg, const UMath::Vector3 &linearVel,
               const UMath::Vector3 &angularVel, const UMath::Vector3 &position) {
    mInertiaP = inertia;
    mCG = cg;
    mLinearVelocity = linearVel;
    mAngularVelocity = angularVel;
    mPosition = position;
    mInertialScale.x = 1.0f;
    mInertialScale.y = 1.0f;
    mInertialScale.z = 1.0f;
    mMass = mass;
    mImmobile = false;
    mBrakingForce = 0.0f;
    mClosingVelocity.x = 0.0f;
    mClosingVelocity.y = 0.0f;
    mClosingVelocity.z = 0.0f;
    mSlidingVelocity.x = 0.0f;
    mSlidingVelocity.y = 0.0f;
    mSlidingVelocity.z = 0.0f;
    mForce.x = 0.0f;
    mForce.y = 0.0f;
    mForce.z = 0.0f;
    mFriction.x = 0.0f;
    mFriction.y = 0.0f;
    mFriction.z = 0.0f;
    mFrictionState = Friction::None;
    mElasticity = 0.0f;
    mFixedCG = false;
    mOrientation = orientation;
    UMath::Transpose(mOrientation, mOrientationInv);
    mMassInv = 1.0f / mMass;
}

// PC 0x487160
Moment::Moment(const IEntity *rb) {
    mInertiaP = rb->GetPrincipalInertia();
    mCG = rb->GetCenterOfGravity();
    mLinearVelocity = rb->GetLinearVelocity();
    mAngularVelocity = rb->GetAngularVelocity();
    mPosition = rb->GetPosition();
    mInertialScale.x = 1.0f;
    mInertialScale.y = 1.0f;
    mInertialScale.z = 1.0f;
    mImmobile = rb->IsImmobile();
    mBrakingForce = 0.0f;
    mMass = rb->GetMass();
    mClosingVelocity.x = 0.0f;
    mClosingVelocity.y = 0.0f;
    mClosingVelocity.z = 0.0f;
    mSlidingVelocity.x = 0.0f;
    mSlidingVelocity.y = 0.0f;
    mSlidingVelocity.z = 0.0f;
    mForce.x = 0.0f;
    mForce.y = 0.0f;
    mForce.z = 0.0f;
    mFriction.x = 0.0f;
    mFriction.y = 0.0f;
    mFriction.z = 0.0f;
    mFrictionState = Friction::None;
    mElasticity = 0.0f;
    mFixedCG = false;
    mOrientation = rb->GetRotation();
    UMath::Transpose(mOrientation, mOrientationInv);
    mMassInv = 1.0f / mMass;
}

// PC 0x4872E0
void Moment::SetInertia(const UMath::Vector3 &inertiaP) {
    mInertiaP = inertiaP;
}

// PC 0x487310
void Moment::SetMass(float mass) {
    mMass = mass;
    mMassInv = 1.0f / mass;
}

// PC 0x487330
void Moment::SetCG(const UMath::Vector3 &cg) {
    mCG = cg;
}

// PC 0x486DA0
// Linear and angular velocity change produced by impulse F applied at arm R.
void Moment::ResolveForce(const UVector3 &F, const UVector3 &R, const UMath::Matrix4 &Ic, UVector3 &dV, UVector3 &dW) const {
    float oomass = mMassInv;
    dV.x = oomass * F.x;
    dV.y = oomass * F.y;
    dV.z = oomass * F.z;

    UMath::Vector3 torque;
    torque.x = R.y * F.z - F.y * R.z;
    torque.y = F.x * R.z - R.x * F.z;
    torque.z = F.y * R.x - R.y * F.x;
    dW = torque;
    UMath::Rotate(dW, Ic, dW);
}

// PC 0x486E30
// Arm from the world centre of gravity to a world point.
void Moment::GetMomentumArm(const UMath::Vector3 &point, UVector3 &R) const {
    UMath::Vector3 cg;
    UMath::Rotate(mCG, mOrientation, cg);
    cg.x += mPosition.x;
    cg.y += mPosition.y;
    cg.z += mPosition.z;
    R.x = point.x - cg.x;
    R.y = point.y - cg.y;
    R.z = point.z - cg.z;
}

// PC 0x486FB0
void Moment::GetPrincipalTensor(UMath::Matrix4 &Ic) const {
    Inertia::Tensor inertia;
    inertia.x = mInertiaP.x * mInertialScale.x;
    inertia.y = mInertiaP.y * mInertialScale.y;
    inertia.z = mInertiaP.z * mInertialScale.z;
    inertia.GetInverseWorldTensor(mOrientation, Ic);
}

// PC 0x486EC0
// Inverse world tensor about a pivot at arm R (parallel axis theorem).
void Moment::GetParallelTensor(const UVector3 &R, UMath::Matrix4 &Ic) const {
    Inertia::Tensor inertia;
    inertia = mInertiaP;

    UMath::Vector3 r = R;
    UMath::Vector3 local;
    UMath::Rotate(r, mOrientationInv, local);

    float mass = mMass;
    float x2 = local.x * local.x;
    float y2 = local.y * local.y;
    float z2 = local.z * local.z;

    UMath::Vector3 parallel;
    parallel.x = (z2 + y2) * mass;
    parallel.y = (z2 + x2) * mass;
    parallel.z = (x2 + y2) * mass;

    inertia.x = (inertia.x + parallel.x) * mInertialScale.x;
    inertia.y = (inertia.y + parallel.y) * mInertialScale.y;
    inertia.z = (inertia.z + parallel.z) * mInertialScale.z;
    inertia.GetInverseWorldTensor(mOrientation, Ic);
}

// PC 0x488830
// Single body against an immovable plane. The impulse is applied over nSteps
// sub-steps (step 0 only records the contact state).
bool Moment::React(const Plane &plane, int nSteps) {
    UVector3 R;
    Acceleration dVf;
    Acceleration dWf;
    UMath::Matrix4 Ic;
    UVector3 normal(plane.normal);

    UMath::Normalize(normal);
    float mass = mMass;
    float Ft = 0.0f;
    GetMomentumArm(plane.point, R);
    GetPrincipalTensor(Ic);

    Velocity w(mAngularVelocity);
    Velocity v(mLinearVelocity);

    int i;
    for (i = 0; i < nSteps; ++i) {
        Acceleration dV_impact;
        Acceleration dV_friction;
        Acceleration dW_impact;
        Acceleration dW_friction;

        UVector3 r;
        UMath::Cross(w, R, r);
        Velocity Vv;
        Vv.x = r.x + v.x;
        Vv.y = r.y + v.y;
        Vv.z = r.z + v.z;

        Momentum P;
        P.x = Vv.x * mass;
        P.y = Vv.y * mass;
        P.z = Vv.z * mass;

        float t = normal.z * P.z + normal.y * P.y + normal.x * P.x;
        if (t >= 0.0f) {
            break;
        }

        Force F;
        F.x = normal.x * -t;
        F.y = normal.y * -t;
        F.z = normal.z * -t;
        Force Ff;
        Friction::State fstate = plane.friction.GetForce(P, t, normal, Ff);

        if (i == 0) {
            float s = normal.z * Vv.z + normal.y * Vv.y + normal.x * Vv.x;
            mClosingVelocity.x = normal.x * s;
            mClosingVelocity.y = normal.y * s;
            mClosingVelocity.z = normal.z * s;
            mSlidingVelocity.x = Vv.x - mClosingVelocity.x;
            mSlidingVelocity.y = Vv.y - mClosingVelocity.y;
            mSlidingVelocity.z = Vv.z - mClosingVelocity.z;
            mFriction = Ff;
            mFrictionState = fstate;
        } else {
            float scale = static_cast<float>(i) / static_cast<float>(nSteps);
            F *= scale;
            Ff *= scale;
            Ft -= scale * t;

            ResolveForce(F, R, Ic, dV_impact, dW_impact);
            ResolveForce(Ff, R, Ic, dV_friction, dW_friction);

            UVector3 dW;
            dW.x = dW_friction.x + dW_impact.x;
            dW.y = dW_friction.y + dW_impact.y;
            dW.z = dW_friction.z + dW_impact.z;
            UVector3 dV;
            dV.x = dV_friction.x + dV_impact.x;
            dV.y = dV_friction.y + dV_impact.y;
            dV.z = dV_friction.z + dV_impact.z;

            v.x += dV.x;
            v.y += dV.y;
            v.z += dV.z;
            w.x += dW.x;
            w.y += dW.y;
            w.z += dW.z;

            float e = mElasticity;
            dVf.x += dV_impact.x * e + dV.x;
            dVf.y += dV_impact.y * e + dV.y;
            dVf.z += dV_impact.z * e + dV.z;

            e = mElasticity;
            dWf.x += dW_impact.x * e + dW.x;
            dWf.y += dW_impact.y * e + dW.y;
            dWf.z += dW_impact.z * e + dW.z;
        }
    }

    if (i == 0) {
        return false;
    }

    mForce.x = normal.x * Ft;
    mForce.y = normal.y * Ft;
    mForce.z = normal.z * Ft;
    mLinearVelocity += dVf;
    mAngularVelocity += dWf;
    return true;
}

// PC 0x488F70
// Two bodies colliding across a contact plane (normal points from other to this).
bool Moment::React(Moment &other, const Plane &plane, int nSteps) {
    Moment *o0 = this;
    Moment *o1 = &other;
    UVector3 R0;
    UVector3 R1;
    UMath::Matrix4 I0c;
    UMath::Matrix4 I1c;
    UVector3 normal(plane.normal);

    UMath::Normalize(normal);

    bool o0_immobile = (o0->mImmobile && o1->mImmobile) ? false : o0->mImmobile;
    bool o1_immobile = (o0->mImmobile && o1->mImmobile) ? false : o1->mImmobile;
    if (o0_immobile && o1_immobile) {
        o0_immobile = false;
        o1_immobile = false;
    }

    o0->GetMomentumArm(plane.point, R0);
    o0->GetPrincipalTensor(I0c);
    UVector3 Rn0(R0);
    UMath::Normalize(Rn0);

    o1->GetMomentumArm(plane.point, R1);
    o1->GetPrincipalTensor(I1c);
    UVector3 Rn1(R1);
    UMath::Normalize(Rn1);

    Velocity w0(o0->mAngularVelocity);
    Velocity v0(o0->mLinearVelocity);
    Velocity w1(o1->mAngularVelocity);
    Velocity v1(o1->mLinearVelocity);

    float mass;
    if (o0_immobile) {
        mass = o1->mMass;
    } else if (o1_immobile) {
        mass = o0->mMass;
    } else {
        mass = (o1->mMass * o0->mMass) / (o1->mMass + o0->mMass);
    }

    float Ft = 0.0f;
    Acceleration dVf0;
    Acceleration dWf0;
    Acceleration dVf1;
    Acceleration dWf1;

    int i;
    for (i = 0; i < nSteps; ++i) {
        UVector3 r;
        UMath::Cross(w0, R0, r);
        Velocity Vv0;
        Vv0.x = r.x + v0.x;
        Vv0.y = r.y + v0.y;
        Vv0.z = r.z + v0.z;

        UMath::Cross(w1, R1, r);
        Velocity Vv1;
        Vv1.x = r.x + v1.x;
        Vv1.y = r.y + v1.y;
        Vv1.z = r.z + v1.z;

        UVector3 Vrel;
        Vrel.x = Vv0.x - Vv1.x;
        Vrel.y = Vv0.y - Vv1.y;
        Vrel.z = Vv0.z - Vv1.z;

        Momentum P;
        P.x = Vrel.x * mass;
        P.y = Vrel.y * mass;
        P.z = Vrel.z * mass;

        float t = P.x * normal.x + normal.z * P.z + normal.y * P.y;
        if (t >= 0.0f) {
            break;
        }

        float impulse = -t;
        Force F;
        F.x = normal.x * impulse;
        F.y = normal.y * impulse;
        F.z = normal.z * impulse;
        Force Ff;
        Friction::State fstate = plane.friction.GetForce(P, t, normal, Ff);

        if (i == 0) {
            float s = Vrel.z * normal.z + Vrel.y * normal.y + Vrel.x * normal.x;
            o0->mClosingVelocity.x = normal.x * s;
            o0->mClosingVelocity.y = normal.y * s;
            o0->mClosingVelocity.z = normal.z * s;
            o0->mSlidingVelocity.x = Vrel.x - o0->mClosingVelocity.x;
            o0->mSlidingVelocity.y = Vrel.y - o0->mClosingVelocity.y;
            o0->mSlidingVelocity.z = Vrel.z - o0->mClosingVelocity.z;
            o0->mFrictionState = fstate;
            o0->mFriction = Ff;
            if (o0->mBrakingForce > 0.0f && impulse > o0->mBrakingForce) {
                o0->mImmobile = false;
                o0_immobile = false;
                mass = (o1->mMass * o0->mMass) / (o1->mMass + o0->mMass);
            }

            UVector3 Vrel1;
            Vrel1.x = Vv1.x - Vv0.x;
            Vrel1.y = Vv1.y - Vv0.y;
            Vrel1.z = Vv1.z - Vv0.z;
            s = Vrel1.z * normal.z + Vrel1.y * normal.y + Vrel1.x * normal.x;
            o1->mClosingVelocity.x = normal.x * s;
            o1->mClosingVelocity.y = normal.y * s;
            o1->mClosingVelocity.z = normal.z * s;
            o1->mSlidingVelocity.x = Vrel1.x - o1->mClosingVelocity.x;
            o1->mSlidingVelocity.y = Vrel1.y - o1->mClosingVelocity.y;
            o1->mSlidingVelocity.z = Vrel1.z - o1->mClosingVelocity.z;
            o1->mFrictionState = fstate;
            o1->mFriction.x = Ff.x * -1.0f;
            o1->mFriction.y = Ff.y * -1.0f;
            o1->mFriction.z = Ff.z * -1.0f;
            if (o1->mBrakingForce > 0.0f && impulse > o1->mBrakingForce) {
                o1->mImmobile = false;
                o1_immobile = false;
                mass = (o1->mMass * o0->mMass) / (o1->mMass + o0->mMass);
            }
        } else {
            float scale = static_cast<float>(i) / static_cast<float>(nSteps);
            F *= scale;
            Ff *= scale;
            Ft -= scale * t;

            if (!o0_immobile) {
                Acceleration dV_impact;
                Acceleration dV_friction;
                Acceleration dW_impact;
                Acceleration dW_friction;
                o0->ResolveForce(F, R0, I0c, dV_impact, dW_impact);
                o0->ResolveForce(Ff, R0, I0c, dV_friction, dW_friction);

                Acceleration dV0;
                dV0.x = dV_friction.x + dV_impact.x;
                dV0.y = dV_friction.y + dV_impact.y;
                dV0.z = dV_friction.z + dV_impact.z;
                Acceleration dW0;
                dW0.x = dW_friction.x + dW_impact.x;
                dW0.y = dW_friction.y + dW_impact.y;
                dW0.z = dW_friction.z + dW_impact.z;

                if (!o0->mFixedCG) {
                    v0 += dV0;
                    float e = o0->mElasticity;
                    dVf0.x += dV_impact.x * e + dV0.x;
                    dVf0.y += dV_impact.y * e + dV0.y;
                    dVf0.z += dV_impact.z * e + dV0.z;
                }
                w0 += dW0;
                float e = o0->mElasticity;
                dWf0.x += dW_impact.x * e + dW0.x;
                dWf0.y += dW_impact.y * e + dW0.y;
                dWf0.z += dW_impact.z * e + dW0.z;
            }

            if (!o1_immobile) {
                F.x = -F.x;
                F.y = -F.y;
                F.z = -F.z;
                Ff.x = -Ff.x;
                Ff.y = -Ff.y;
                Ff.z = -Ff.z;

                Acceleration dV_impact;
                Acceleration dV_friction;
                Acceleration dW_impact;
                Acceleration dW_friction;
                o1->ResolveForce(F, R1, I1c, dV_impact, dW_impact);
                o1->ResolveForce(Ff, R1, I1c, dV_friction, dW_friction);

                Acceleration dV1;
                dV1.x = dV_friction.x + dV_impact.x;
                dV1.y = dV_friction.y + dV_impact.y;
                dV1.z = dV_friction.z + dV_impact.z;
                Acceleration dW1;
                dW1.x = dW_friction.x + dW_impact.x;
                dW1.y = dW_friction.y + dW_impact.y;
                dW1.z = dW_friction.z + dW_impact.z;

                if (!o1->mFixedCG) {
                    v1 += dV1;
                    float e = o1->mElasticity;
                    dVf1.x += dV_impact.x * e + dV1.x;
                    dVf1.y += dV_impact.y * e + dV1.y;
                    dVf1.z += dV_impact.z * e + dV1.z;
                }
                w1 += dW1;
                float e = o1->mElasticity;
                dWf1.x += dW_impact.x * e + dW1.x;
                dWf1.y += dW_impact.y * e + dW1.y;
                dWf1.z += dW_impact.z * e + dW1.z;
            }
        }
    }

    if (i == 0) {
        return false;
    }

    o0->mForce.x = normal.x * Ft;
    o0->mForce.y = normal.y * Ft;
    o0->mForce.z = normal.z * Ft;
    o1->mForce.x = normal.x * -Ft;
    o1->mForce.y = normal.y * -Ft;
    o1->mForce.z = normal.z * -Ft;
    o0->mLinearVelocity += dVf0;
    o0->mAngularVelocity += dWf0;
    o1->mLinearVelocity += dVf1;
    o1->mAngularVelocity += dWf1;
    return true;
}

// PC 0x48A000
// Two bodies pinned together at joint.fulcrum: removes their relative velocity
// at the pivot.
bool Moment::React(Moment &other, const Joint &joint, int nSteps) {
    Moment *o0 = this;
    Moment *o1 = &other;
    UVector3 R0;
    UVector3 R1;
    UMath::Matrix4 I0c;
    UMath::Matrix4 I1c;

    bool o0_immobile = (o0->mImmobile && o1->mImmobile) ? false : o0->mImmobile;
    bool o1_immobile = (o0->mImmobile && o1->mImmobile) ? false : o1->mImmobile;
    if (o0_immobile && o1_immobile) {
        o0_immobile = false;
        o1_immobile = false;
    }

    o0->GetMomentumArm(joint.fulcrum, R0);
    o0->GetParallelTensor(R0, I0c);
    o1->GetMomentumArm(joint.fulcrum, R1);
    o1->GetParallelTensor(R1, I1c);

    Velocity w0(o0->mAngularVelocity);
    Velocity v0(o0->mLinearVelocity);
    Velocity w1(o1->mAngularVelocity);
    Velocity v1(o1->mLinearVelocity);

    float mass;
    if (o0_immobile) {
        mass = o1->mMass;
    } else if (o1_immobile) {
        mass = o0->mMass;
    } else {
        mass = (o0->mMass * o1->mMass) / (o0->mMass + o1->mMass);
    }

    Acceleration dVf0;
    Acceleration dVf1;
    Acceleration dWf0;
    Acceleration dWf1;

    int i;
    for (i = 0; i < nSteps; ++i) {
        UVector3 r0;
        UMath::Cross(w0, R0, r0);
        UVector3 r1;
        UMath::Cross(w1, R1, r1);

        Velocity Vrel;
        Vrel.x = (r0.x + v0.x) - (r1.x + v1.x);
        Vrel.y = (r0.y + v0.y) - (r1.y + v1.y);
        Vrel.z = (r0.z + v0.z) - (r1.z + v1.z);

        Momentum P;
        P.x = Vrel.x * mass;
        P.y = Vrel.y * mass;
        P.z = Vrel.z * mass;

        if (UMath::Sqrt(P.y * P.y + P.z * P.z + P.x * P.x) == 0.0f) {
            break;
        }

        Force F;
        F.x = P.x * -1.0f;
        F.y = P.y * -1.0f;
        F.z = P.z * -1.0f;

        if (i == 0) {
            o0->mForce = F;
            o0->mClosingVelocity = Vrel;
            o1->mForce.x = F.x * -1.0f;
            o1->mForce.y = F.y * -1.0f;
            o1->mForce.z = F.z * -1.0f;
            o1->mClosingVelocity.x = Vrel.x * -1.0f;
            o1->mClosingVelocity.y = Vrel.y * -1.0f;
            o1->mClosingVelocity.z = Vrel.z * -1.0f;
        } else {
            float scale = static_cast<float>(i) / static_cast<float>(nSteps);
            F.x = F.x * scale;
            F.y = F.y * scale;
            F.z = F.z * scale;

            if (!o0_immobile) {
                Acceleration dV0;
                Acceleration dW0;
                o0->ResolveForce(F, R0, I0c, dV0, dW0);
                v0 += dV0;
                w0 += dW0;
                dVf0 += dV0;
                dWf0 += dW0;
            }
            if (!o1_immobile) {
                F.x = -F.x;
                F.y = -F.y;
                F.z = -F.z;
                Acceleration dV1;
                Acceleration dW1;
                o1->ResolveForce(F, R1, I1c, dV1, dW1);
                v1 += dV1;
                w1 += dW1;
                dVf1 += dV1;
                dWf1 += dW1;
            }
        }
    }

    if (i == 0) {
        return false;
    }

    o0->mLinearVelocity += dVf0;
    o0->mAngularVelocity += dWf0;
    o1->mLinearVelocity += dVf1;
    o1->mAngularVelocity += dWf1;
    return true;
}

// PC 0x48A910
// Single body pinned to the world at joint.fulcrum.
bool Moment::React(const Joint &joint, int nSteps) {
    UVector3 R;
    UMath::Matrix4 Ic;

    GetMomentumArm(joint.fulcrum, R);
    GetParallelTensor(R, Ic);

    Velocity w(mAngularVelocity);
    Velocity v(mLinearVelocity);
    float mass = mMass;
    Acceleration dVf;
    Acceleration dWf;

    int i;
    for (i = 0; i < nSteps; ++i) {
        UVector3 r;
        UMath::Cross(w, R, r);
        Velocity Vv;
        Vv.x = r.x + v.x;
        Vv.y = r.y + v.y;
        Vv.z = r.z + v.z;

        Momentum P;
        P.x = Vv.x * mass;
        P.y = Vv.y * mass;
        P.z = Vv.z * mass;

        if (UMath::Sqrt(P.x * P.x + P.y * P.y + P.z * P.z) == 0.0f) {
            break;
        }

        Force F;
        F.x = P.x * -1.0f;
        F.y = P.y * -1.0f;
        F.z = P.z * -1.0f;

        if (i == 0) {
            mForce = F;
            mClosingVelocity.x = Vv.x * -1.0f;
            mClosingVelocity.y = Vv.y * -1.0f;
            mClosingVelocity.z = Vv.z * -1.0f;
        } else {
            float scale = static_cast<float>(i) / static_cast<float>(nSteps);
            F.x = F.x * scale;
            F.y = F.y * scale;
            F.z = F.z * scale;

            Acceleration dV;
            Acceleration dW;
            ResolveForce(F, R, Ic, dV, dW);
            v += dV;
            w += dW;
            dVf += dV;
            dWf += dW;
        }
    }

    if (i == 0) {
        return false;
    }

    mLinearVelocity += dVf;
    mAngularVelocity += dWf;
    return true;
}

} // namespace Collision
} // namespace Dynamics
