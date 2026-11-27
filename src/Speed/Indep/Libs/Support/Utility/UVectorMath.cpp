#include "UVectorMath.h"
#include "UMath.h"

#ifndef EA_PLATFORM_GAMECUBE
// Decl: Carbon: UVectorMath.cpp: 1137, GC MW: UVectorMathGC.hpp: 352, PS2 MW: UVectorMath.hpp: TODO
void VU0_ExtractXAxis3FromQuat(const RQUAT &quat, UMath::Vector3 &result) {
    const float scale = 2.0f;
    float yy = scale * (quat.y * quat.y);
    float zz = scale * (quat.z * quat.z);
    float xy = scale * (quat.x * quat.y);
    float xz = scale * (quat.x * quat.z);
    float yw = scale * (quat.y * quat.w);
    float zw = scale * (quat.z * quat.w);

    result.x = 1.0f - (yy + zz);
    result.y = xy + zw;
    result.z = xz - yw;
}

// Decl: Carbon: UVectorMath.cpp: 1169, GC MW: UVectorMathGC.hpp: 386, PS2 MW: UVectorMath.hpp: TODO
void VU0_ExtractYAxis3FromQuat(const RQUAT &quat, UMath::Vector3 &result) {
    const float scale = 2.0f;
    float xx = scale * (quat.x * quat.x);
    float zz = scale * (quat.z * quat.z);
    float xy = scale * (quat.x * quat.y);
    float xw = scale * (quat.x * quat.w);
    float yz = scale * (quat.y * quat.z);
    float zw = scale * (quat.z * quat.w);

    result.x = xy - zw;
    result.y = 1.0f - (xx + zz);
    result.z = yz + xw;
}

// Decl: Carbon: UVectorMath.cpp: 1202, GC MW: UVectorMathGC.hpp: 423, PS2 MW: UVectorMath.hpp: TODO
void VU0_ExtractZAxis3FromQuat(const RQUAT &quat, UMath::Vector3 &result) {
    const float scale = 2.0f;
    float xx = scale * (quat.x * quat.x);
    float yy = scale * (quat.y * quat.y);
    float xz = scale * (quat.x * quat.z);
    float xw = scale * (quat.x * quat.w);
    float yz = scale * (quat.y * quat.z);
    float yw = scale * (quat.y * quat.w);

    result.x = xz + yw;
    result.y = yz - xw;
    result.z = 1.0f - (xx + yy);
}

// Decl: Carbon: UVectorMath.cpp: 1235, GC MW: UVectorMathGC.hpp: 472, PS2 MW: UVectorMath.hpp: TODO
void VU0_quattom4(const RQUAT &quat, UMath::Matrix4 &result) {
    const float scale = 2.0f;
    float xx = quat.x * quat.x * scale;
    float yy = quat.y * quat.y * scale;
    float zz = quat.z * quat.z * scale;

    float xy = quat.x * quat.y * scale;
    float xz = quat.x * quat.z * scale;
    float xw = quat.x * quat.w * scale;

    float yz = quat.y * quat.z * scale;
    float yw = quat.y * quat.w * scale;
    float zw = quat.z * quat.w * scale;

    result[0][0] = 1.0f - (yy + zz);
    result[0][1] = (xy + zw);
    result[0][2] = (xz - yw);
    result[0][3] = 0.0f;

    result[1][0] = (xy - zw);
    result[1][1] = 1.0f - (xx + zz);
    result[1][2] = (yz + xw);
    result[1][3] = 0.0f;

    result[2][0] = (xz + yw);
    result[2][1] = (yz - xw);
    result[2][2] = 1.0f - (xx + yy);
    result[2][3] = 0.0f;

    result[3][0] = 0.0f;
    result[3][1] = 0.0f;
    result[3][2] = 0.0f;
    result[3][3] = 1.0f;
}

// Decl: Carbon: UVectorMath.cpp: 1313, GC MW: UVectorMathGC.hpp: 538, PS2 MW: UVectorMath.hpp: 1891
void VU0_MATRIX4_mult(const UMath::Matrix4 &m1, const UMath::Matrix4 &m2, UMath::Matrix4 &result) {
    UMath::Matrix4 temp;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            temp[i][j] = m1[i][0] * m2[0][j] + m1[i][1] * m2[1][j] + m1[i][2] * m2[2][j] + m1[i][3] * m2[3][j];
        }
    }
    result = temp;
}
#endif

// Decl: Carbon: UVectorMath.cpp: 4934, GC MW: UVectorMath.cpp: 4935, PS2 MW: UVectorMath.cpp: 4936
float VU0_Atan2(const float opposite, const float adjacent) {
    float x = UMath::Epsilon < VU0_fabs(adjacent) ? adjacent : 0.0f;
    float y = UMath::Epsilon < VU0_fabs(opposite) ? opposite : 0.0f;

    short bangle = bATan(x, y);
    return bangle / 65536.0f;
}

#if defined(EA_PORT_NATIVE)
// The CPU (PC) versions of the VU0 helpers the decomp declares but never
// defines; link stubs returned 0 for all of them. Row-vector convention:
// v * M = v.x * M.v0 + v.y * M.v1 + v.z * M.v2 (+ v.w * M.v3).

float VU0_rsqrt(const float a) {
    // the PC callers treat a zero length as a zero vector, not a NaN
    return a > 0.0f ? 1.0f / sqrtf(a) : 0.0f;
}

void VU0_v3addscale(const UMath::Vector3 &a, const UMath::Vector3 &b, const float scaleby, UMath::Vector3 &result) {
    result.x = (a.x + b.x) * scaleby;
    result.y = (a.y + b.y) * scaleby;
    result.z = (a.z + b.z) * scaleby;
}

void VU0_v4scaleaddxyz(const UMath::Vector4 &a, const float scaleby, const UMath::Vector4 &b, UMath::Vector4 &result) {
    result.x = a.x * scaleby + b.x;
    result.y = a.y * scaleby + b.y;
    result.z = a.z * scaleby + b.z;
}

float VU0_v4lengthsquare(const UMath::Vector4 &a) {
    return a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w;
}

void VU0_v4sub(const UMath::Vector4 &a, const UMath::Vector4 &b, UMath::Vector4 &result) {
    result.x = a.x - b.x;
    result.y = a.y - b.y;
    result.z = a.z - b.z;
    result.w = a.w - b.w;
}

void VU0_v4subxyz(const UMath::Vector4 &a, const UMath::Vector4 &b, UMath::Vector4 &result) {
    result.x = a.x - b.x;
    result.y = a.y - b.y;
    result.z = a.z - b.z;
}

float VU0_v4dotprodxyz(const UMath::Vector4 &a, const UMath::Vector4 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

void VU0_v4scalexyz(const UMath::Vector4 &a, const float scaleby, UMath::Vector4 &result) {
    result.x = a.x * scaleby;
    result.y = a.y * scaleby;
    result.z = a.z * scaleby;
}

void VU0_v4scalexyz(const UMath::Vector4 &a, const UMath::Vector4 &b, UMath::Vector4 &result) {
    result.x = a.x * b.x;
    result.y = a.y * b.y;
    result.z = a.z * b.z;
}

void VU0_v4add(const UMath::Vector4 &a, const UMath::Vector4 &b, UMath::Vector4 &result) {
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
    result.w = a.w + b.w;
}

// speed.exe 1.3: 0x401960
float VU0_v4distancesquarexyz(const UMath::Vector4 &p1, const UMath::Vector4 &p2) {
    float x = p1.x - p2.x;
    float y = p1.y - p2.y;
    float z = p1.z - p2.z;
    return x * x + y * y + z * z;
}

void VU0_v4addxyz(const UMath::Vector4 &a, const UMath::Vector4 &b, UMath::Vector4 &result) {
    result.x = a.x + b.x;
    result.y = a.y + b.y;
    result.z = a.z + b.z;
}

// speed.exe 1.3: 0x444DC0
void VU0_v4crossprodxyz(const UMath::Vector4 &a, const UMath::Vector4 &b, UMath::Vector4 &dest) {
    UMath::Vector4 result;
    result.x = a.y * b.z - a.z * b.y;
    result.y = a.z * b.x - a.x * b.z;
    result.z = a.x * b.y - a.y * b.x;
    result.w = 0.0f;
    dest = result;
}

void VU0_MATRIX3x4_vect4mult(const UMath::Vector4 &v, const UMath::Matrix4 &m, UMath::Vector4 &result) {
    UMath::Vector4 r;
    r.x = v.x * m.v0.x + v.y * m.v1.x + v.z * m.v2.x;
    r.y = v.x * m.v0.y + v.y * m.v1.y + v.z * m.v2.y;
    r.z = v.x * m.v0.z + v.y * m.v1.z + v.z * m.v2.z;
    r.w = v.w;
    result = r;
}

// speed.exe 1.3: 0x486610: rotate and translate
void VU0_MATRIX4_vect3mult(const UMath::Vector3 &v, const UMath::Matrix4 &m, UMath::Vector3 &result) {
    UMath::Vector3 r;
    r.x = v.x * m.v0.x + v.y * m.v1.x + v.z * m.v2.x + m.v3.x;
    r.y = v.x * m.v0.y + v.y * m.v1.y + v.z * m.v2.y + m.v3.y;
    r.z = v.x * m.v0.z + v.y * m.v1.z + v.z * m.v2.z + m.v3.z;
    result = r;
}

// speed.exe 1.3: 0x4FA510 (UMath::Mult)
void VU0_MATRIX4_vect4mult(const UMath::Vector4 &v, const UMath::Matrix4 &m, UMath::Vector4 &result) {
    UMath::Vector4 r;
    r.x = v.x * m.v0.x + v.y * m.v1.x + v.z * m.v2.x + v.w * m.v3.x;
    r.y = v.x * m.v0.y + v.y * m.v1.y + v.z * m.v2.y + v.w * m.v3.y;
    r.z = v.x * m.v0.z + v.y * m.v1.z + v.z * m.v2.z + v.w * m.v3.z;
    r.w = v.x * m.v0.w + v.y * m.v1.w + v.z * m.v2.w + v.w * m.v3.w;
    result = r;
}

void VU0_MATRIX4_vect4mult(const UMath::Vector4 *v, const UMath::Matrix4 &m, UMath::Vector4 *result, int count) {
    for (int i = 0; i < count; i++) {
        VU0_MATRIX4_vect4mult(v[i], m, result[i]);
    }
}

// Same rotation UMath::QuaternionToMatrix4 builds
void VU0_v3quatrotate(const UMath::Vector4 &q, const UMath::Vector3 &v, UMath::Vector3 &result) {
    UMath::Matrix4 m;
    UMath::QuaternionToMatrix4(q, m);
    UMath::Vector3 r;
    r.x = v.x * m.v0.x + v.y * m.v1.x + v.z * m.v2.x;
    r.y = v.x * m.v0.y + v.y * m.v1.y + v.z * m.v2.y;
    r.z = v.x * m.v0.z + v.y * m.v1.z + v.z * m.v2.z;
    result = r;
}

// speed.exe 1.3: 0x5D06A0
void VU0_m4toquat(const UMath::Matrix4 &mat, UMath::Vector4 &result) {
    const float *m = &mat.v0.x;
    float trace = m[5] + m[10] + m[0];
    if (trace > 0.0f) {
        float s = sqrtf(trace + 1.0f);
        result.w = 0.5f * s;
        s = 0.5f / s;
        result.x = (m[6] - m[9]) * s;
        result.y = (m[8] - m[2]) * s;
        result.z = (m[1] - m[4]) * s;
        return;
    }
    int i = m[0] < m[5] ? 5 : 0;
    if (m[i] < m[10]) {
        float s = sqrtf(m[10] - (m[5] + m[0]) + 1.0f);
        result.z = 0.5f * s;
        if (s != 0.0f) {
            s = 0.5f / s;
        }
        result.w = (m[1] - m[4]) * s;
        result.x = (m[8] + m[2]) * s;
        result.y = (m[9] + m[6]) * s;
        return;
    }
    if (i != 0) {
        float s = sqrtf(m[5] - (m[0] + m[10]) + 1.0f);
        result.y = 0.5f * s;
        if (s != 0.0f) {
            s = 0.5f / s;
        }
        result.w = (m[8] - m[2]) * s;
        result.z = (m[9] + m[6]) * s;
        result.x = (m[4] + m[1]) * s;
        return;
    }
    float s = sqrtf(m[0] - (m[5] + m[10]) + 1.0f);
    result.x = 0.5f * s;
    if (s != 0.0f) {
        s = 0.5f / s;
    }
    result.w = (m[6] - m[9]) * s;
    result.y = (m[4] + m[1]) * s;
    result.z = (m[8] + m[2]) * s;
}

// speed.exe 1.3: 0x5CB420 (angle in revolutions)
void VU0_MATRIX4setyrot(UMath::Matrix4 &dest, const float yangle) {
    float c = cosf(yangle * 6.2831855f);
    float s = sinf(yangle * 6.2831855f);
    VU0_MATRIX4Init(dest);
    dest.v0.x = c;
    dest.v2.z = c;
    dest.v0.z = -s;
    dest.v2.x = s;
}

// speed.exe 1.3: 0x5D0A70 (angles in revolutions)
void VU0_Matrix4ToEuler(const UMath::Matrix4 &m, UMath::Vector3 &e) {
    float sy = m.v0.z;
    if (sy < -1.0f) {
        sy = -1.0f;
    } else if (sy > 1.0f) {
        sy = 1.0f;
    }
    float cy = sqrtf(1.0f - sy * sy);
    if (m.v0.x < 0.0f && m.v2.z < 0.0f) {
        cy = -cy;
    }
    if (VU0_fabs(cy) > 0.001f) {
        float r = 1.0f / cy;
        e.x = VU0_Atan2(r * m.v1.z, r * m.v2.z);
        e.y = -VU0_Atan2(sy, cy);
        e.z = VU0_Atan2(r * m.v0.y, r * m.v0.x);
        return;
    }
    e.x = VU0_Atan2(-m.v2.y, m.v1.y);
    e.y = asinf(sy) * -0.15915494f;
    e.z = 0.0f;
}

namespace UMath {

// speed.exe 1.3: 0x5CA910
void QuaternionToMatrix4(const Vector4 &q, Matrix4 &m) {
    float xx = q.x * q.x + q.x * q.x;
    float yy = q.y * q.y + q.y * q.y;
    float zz = q.z * q.z + q.z * q.z;
    float xy = q.y * q.x + q.y * q.x;
    float xz = q.z * q.x + q.z * q.x;
    float xw = q.w * q.x + q.w * q.x;
    float yz = q.z * q.y + q.z * q.y;
    float yw = q.w * q.y + q.w * q.y;
    float zw = q.w * q.z + q.w * q.z;
    m.v0.x = 1.0f - (zz + yy);
    m.v0.y = zw + xy;
    m.v0.z = xz - yw;
    m.v0.w = 0.0f;
    m.v1.x = xy - zw;
    m.v1.y = 1.0f - (zz + xx);
    m.v1.z = yz + xw;
    m.v1.w = 0.0f;
    m.v2.x = yw + xz;
    m.v2.y = yz - xw;
    m.v2.z = 1.0f - (yy + xx);
    m.v2.w = 0.0f;
    m.v3.x = 0.0f;
    m.v3.y = 0.0f;
    m.v3.z = 0.0f;
    m.v3.w = 1.0f;
}

}; // namespace UMath
#endif
