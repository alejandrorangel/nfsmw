#include "Speed/Indep/Src/Physics/Wheel.h"
#include "Speed/Indep/Libs/Support/Utility/UTypes.h"

bool Wheel::UpdatePosition(const UMath::Vector3 &body_av, const UMath::Vector3 &body_lv, 
    const UMath::Matrix4 &body_matrix, const UMath::Vector3 &cog,
    float dT, float wheel_radius, bool usecache, const WCollider *collider, float vehicle_height) {
    UMath::Rotate(this->mLocalArm, body_matrix, this->mWorldArm);
    UMath::Add(this->mWorldArm, UMath::Vector4To3(body_matrix.v3), this->mPosition);

    UMath::Vector3 pVel;
    UMath::Vector3 pos;
    UMath::Sub(this->mWorldArm, cog, pos);
    UMath::Cross(body_av, pos, pVel);
    UMath::Add(pVel, body_lv, pVel);
    SetVelocity(pVel);

    UMath::Add(this->mWorldArm, UMath::Vector4To3(body_matrix.v3), this->mPosition);

    float tolerance = UMath::Max(-pVel.y * dT, 0.0f) + wheel_radius + UMath::Lengthxz(pVel) * dT;
    float prev = vehicle_height * 0.5f;
    this->mWorldPos.SetTolerance(UMath::Min(tolerance, prev));

    bool result = this->mWorldPos.Update(this->mPosition, this->mNormal, IsOnGround() && usecache, collider, true);
    UpdateSurface(SimSurface(this->mWorldPos.GetSurface()));
    return result;
}

#if defined(EA_PORT_NATIVE)
// Reconstructed from speed.exe 1.3 for the native port.

// speed.exe 1.3: 0x671330
Wheel::Wheel(unsigned int flags)
    : mWorldPos(0.025f),                    //
      mNormal(UMath::Vector4::kZero),       //
      mPosition(UMath::Vector3::kZero),     //
      mFlags(flags),                        //
      mForce(UMath::Vector3::kZero),        //
      mAirTime(0.0f),                       //
      mLocalArm(UMath::Vector3::kZero),     //
      mCompression(0.0f),                   //
      mWorldArm(UMath::Vector3::kZero),     //
      mVelocity(UMath::Vector3::kZero),     //
      mSurface(SimSurface::kNull.GetConstCollection()), //
      mSurfaceStick(0.0f),                  //
      mIntegral(UMath::Vector4::kZero) {}

// speed.exe 1.3: 0x66A460: how long the current surface still sticks
void Wheel::UpdateTime(float dT) {
    if (mSurfaceStick > 0.0f && dT < mSurfaceStick) {
        mSurfaceStick -= dT;
    } else {
        mSurfaceStick = 0.0f;
    }
}

// speed.exe 1.3: 0x66A4B0: a new surface only takes over once the old one
// stops sticking
void Wheel::UpdateSurface(const SimSurface &surface) {
    if (mSurfaceStick <= 0.0f) {
        mSurface.Change(surface.GetConstCollection());
        mSurfaceStick = surface.STICK();
    } else if (surface.GetConstCollection() == mSurface.GetConstCollection()) {
        mSurfaceStick = surface.STICK();
    }
}

// speed.exe 1.3: 0x671460
void Wheel::Reset() {
    mIntegral = UMath::Vector4::kZero;
    mSurfaceStick = 0.0f;
    mAirTime = 0.0f;
    mVelocity = UMath::Vector3::kZero;
    mCompression = 0.0f;
    mNormal = UMath::Vector4::kZero;
    mForce = UMath::Vector3::kZero;
    mSurface.Change(SimSurface::kNull.GetConstCollection());
    mWorldPos = WWorldPos(0.025f);
}

// speed.exe 1.3: 0x671730: places the wheel for the body's current pose
bool Wheel::InitPosition(const IRigidBody &rb, float maxcompression) {
    UMath::Matrix4 mat;
    rb.GetMatrix4(mat);
    const UMath::Vector3 &pos = rb.GetPosition();
    mat.v3.x = pos.x;
    mat.v3.y = pos.y;
    mat.v3.z = pos.z;
    mat.v3.w = 1.0f;
    UMath::Vector3 dim;
    rb.GetDimension(dim);
    return UpdatePosition(rb.GetAngularVelocity(), rb.GetLinearVelocity(), mat, UMath::Vector3::kZero, 0.0f, maxcompression, false, rb.GetWCollider(),
                          dim.y * 2.0f);
}
#endif
