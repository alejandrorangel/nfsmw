#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Libs/Support/Utility/UTypes.h"
#include "Speed/Indep/Libs/Support/Utility/UVectorMath.h"
#include "Speed/Indep/bWare/Inc/bMath.hpp"
#include <cmath>

namespace UMath {

float Ceil(const float x) {
    return bCeil(x);
}

// STRIPPED
float Floor(const float x) {
    return bFloor(x);
}

float Mod(const float x, const float e) {
    return bFMod(x, e);
}

bool IsNaN(const float f) {
#ifndef _MSC_VER // TODO
    return isnanf(f) ? true : false;
#endif
}

#if defined(EA_PORT_NATIVE)
bool IsNaN(const UMath::Vector3 &v) {
    return IsNaN(v.x) || IsNaN(v.y) || IsNaN(v.z);
}
bool IsNaN(const UMath::Vector4 &v) {
    return IsNaN(v.x) || IsNaN(v.y) || IsNaN(v.z) || IsNaN(v.w);
}
bool IsNaN(const UMath::Matrix3 &v) {
    const float *f = reinterpret_cast<const float *>(&v);
    for (unsigned int i = 0; i < sizeof(v) / sizeof(float); i++) {
        if (IsNaN(f[i])) {
            return true;
        }
    }
    return false;
}
bool IsNaN(const UMath::Matrix4 &v) {
    return IsNaN(v.v0) || IsNaN(v.v1) || IsNaN(v.v2) || IsNaN(v.v3);
}

// The constants every UMath user reads; never defined in the decomp, so the
// link stubs zero-filled them (an all-zero Matrix4::kIdentity, ...).
const Vector2 Vector2::kZero = {0.0f, 0.0f};
const Vector3 Vector3::kZero = {0.0f, 0.0f, 0.0f};
const Vector4 Vector4::kZero = {0.0f, 0.0f, 0.0f, 0.0f};
const Vector4 Vector4::kIdentity = {0.0f, 0.0f, 0.0f, 1.0f};
const Matrix4 Matrix4::kIdentity = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
float Infinity = 3.4028235e38f;
#else
// STRIPPED
bool IsNaN(const UMath::Vector3 &v) {}
bool IsNaN(const UMath::Vector4 &v) {}
bool IsNaN(const UMath::Matrix3 &v) {}
bool IsNaN(const UMath::Matrix4 &v) {}
#endif
//...

} // namespace UMath

// UNSOLVED
void BuildRotate(UMath::Matrix4 &m, float r, float x, float y, float z) {
    r /= 360.0f;
    if (r == 0.0f) {
        VU0_MATRIX4Init(m, 1.0f, 1.0f, 1.0f);
    } else {
        float angle = r * UMath::TWOPI;
        float fSin = VU0_Sin(angle);
        float fCos = VU0_Cos(angle);

        float invllen = VU0_rsqrt(x * x + y * y + z * z);
        x *= invllen;
        y *= invllen;
        z *= invllen;

        float fT = 1.0f;
        float fTX = fT * x;   // f9
        float fTY = fT * y;   // f0
        float fTZ = fT * z;   // f12
        float fSX = fSin * x; // f11
        float fSY = fSin * y; // f30
        float fSZ = fSin * z; // f8

        m.v0.x = fTX * x + fCos; // 0
        m.v0.y = fTX * y + fSZ;  // 1
        m.v0.z = fTX * z - fSY;  // 2
        m.v1.x = fTY * x - fSZ;  // 3
        m.v1.y = fTY * y + fCos; // 4
        m.v1.z = fTY * z + fSX;  // 5
        m.v2.x = fTZ * x + fSin; // 6
        m.v2.y = fTZ * y - fSX;  // 7
        m.v2.z = fTZ * z + fCos; // 8

        m.v3.x = 0.0f;
        m.v3.y = 0.0f;
        m.v3.z = 0.0f;

        m.v0.w = 0.0f;
        m.v1.w = 0.0f;
        m.v2.w = 0.0f;
        m.v3.w = fT;
    }
}
