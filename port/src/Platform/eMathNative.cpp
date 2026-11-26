// Native Ecstasy matrix math (Speed/Indep/Src/Ecstasy/eMath.hpp).
//
// On PC these wrap D3DX (speed.exe 1.3, 0x6BD830..0x6BE170): bMatrix4 is four
// row vectors with the translation in v3, points are row vectors (p' = p * M),
// and eMulMatrix(ab, a, b) = D3DXMatrixMultiply(ab, a, b) = a * b.

#include "Speed/Indep/Src/Ecstasy/eMath.hpp"
#include "Speed/Indep/bWare/Inc/bMath.hpp"

#include <cmath>
#include <cstring>

bMatrix4 eMathIdentityMatrix;
bMatrix4 eMathZeroMatrix;

namespace {

typedef float M44[4][4];

const float *Row(const bMatrix4 *m, int r) {
    return reinterpret_cast<const float *>(m) + r * 4;
}

void Store(bMatrix4 *dest, const M44 m) {
    std::memcpy(dest, m, sizeof(M44));
}

void Load(M44 out, const bMatrix4 *m) {
    std::memcpy(out, m, sizeof(M44));
}

void Identity(M44 m) {
    std::memset(m, 0, sizeof(M44));
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}

void Multiply(M44 out, const M44 a, const M44 b) {
    M44 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    std::memcpy(out, r, sizeof(M44));
}

// Angles are 16-bit fractions of a full turn (bAngle).
void SinCos(bAngle angle, float &s, float &c) {
    s = bSin(angle);
    c = bCos(angle);
}

} // namespace

void eMathInit(void) {
    M44 m;
    Identity(m);
    Store(&eMathIdentityMatrix, m);
    std::memset(&eMathZeroMatrix, 0, sizeof(eMathZeroMatrix));
}

void eCopyMatrix(bMatrix4 *dest, bMatrix4 *src) {
    std::memmove(dest, src, sizeof(bMatrix4));
}

void eMulMatrix(bMatrix4 *ab, bMatrix4 *a, bMatrix4 *b) {
    M44 ma, mb, r;
    Load(ma, a);
    Load(mb, b);
    Multiply(r, ma, mb);
    Store(ab, r);
}

// D3DXVec4Transform: vm = v * m.
void eMulVector(bVector4 *vm, const bMatrix4 *m, const bVector4 *v) {
    const float in[4] = {v->x, v->y, v->z, v->w};
    float out[4];
    for (int j = 0; j < 4; j++) {
        out[j] = in[0] * Row(m, 0)[j] + in[1] * Row(m, 1)[j] + in[2] * Row(m, 2)[j] + in[3] * Row(m, 3)[j];
    }
    vm->x = out[0];
    vm->y = out[1];
    vm->z = out[2];
    vm->w = out[3];
}

// D3DXVec3TransformCoord: (v, 1) * m, divided by w.
void eMulVector(bVector3 *vm, const bMatrix4 *m, const bVector3 *v) {
    const float in[3] = {v->x, v->y, v->z};
    float out[4];
    for (int j = 0; j < 4; j++) {
        out[j] = in[0] * Row(m, 0)[j] + in[1] * Row(m, 1)[j] + in[2] * Row(m, 2)[j] + Row(m, 3)[j];
    }
    float inv_w = (out[3] != 0.0f) ? 1.0f / out[3] : 1.0f;
    vm->x = out[0] * inv_w;
    vm->y = out[1] * inv_w;
    vm->z = out[2] * inv_w;
}

void eCreateRotationZ(bMatrix4 *dest, bAngle angle) {
    float s, c;
    SinCos(angle, s, c);
    M44 m;
    Identity(m);
    m[0][0] = c;
    m[0][1] = s;
    m[1][0] = -s;
    m[1][1] = c;
    Store(dest, m);
}

static void RotateBy(bMatrix4 *dest, bMatrix4 *a, const M44 rot) {
    M44 ma, r;
    Load(ma, a);
    Multiply(r, ma, rot);
    Store(dest, r);
}

void eRotateX(bMatrix4 *dest, bMatrix4 *a, bAngle angle) {
    float s, c;
    SinCos(angle, s, c);
    M44 rot;
    Identity(rot);
    rot[1][1] = c;
    rot[1][2] = s;
    rot[2][1] = -s;
    rot[2][2] = c;
    RotateBy(dest, a, rot);
}

void eRotateY(bMatrix4 *dest, bMatrix4 *a, bAngle angle) {
    float s, c;
    SinCos(angle, s, c);
    M44 rot;
    Identity(rot);
    rot[0][0] = c;
    rot[0][2] = -s;
    rot[2][0] = s;
    rot[2][2] = c;
    RotateBy(dest, a, rot);
}

void eRotateZ(bMatrix4 *dest, bMatrix4 *a, bAngle angle) {
    bMatrix4 rot;
    eCreateRotationZ(&rot, angle);
    eMulMatrix(dest, a, &rot);
}

void eCreateTranslationMatrix(bMatrix4 *dest, bVector3 &tran) {
    M44 m;
    Identity(m);
    m[3][0] = tran.x;
    m[3][1] = tran.y;
    m[3][2] = tran.z;
    Store(dest, m);
}

void eTranslate(bMatrix4 *dest, bMatrix4 *a, bVector3 *tran) {
    bMatrix4 t;
    eCreateTranslationMatrix(&t, *tran);
    eMulMatrix(dest, a, &t);
}

// D3DXMatrixRotationAxis (row-vector form).
void eCreateAxisRotationMatrix(bMatrix4 *dest, bVector3 &axis, bAngle angle) {
    float s, c;
    SinCos(angle, s, c);
    float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    float x = axis.x, y = axis.y, z = axis.z;
    if (len > 0.0f) {
        x /= len, y /= len, z /= len;
    }
    float t = 1.0f - c;
    M44 m;
    Identity(m);
    m[0][0] = t * x * x + c;
    m[0][1] = t * x * y + s * z;
    m[0][2] = t * x * z - s * y;
    m[1][0] = t * x * y - s * z;
    m[1][1] = t * y * y + c;
    m[1][2] = t * y * z + s * x;
    m[2][0] = t * x * z + s * y;
    m[2][1] = t * y * z - s * x;
    m[2][2] = t * z * z + c;
    Store(dest, m);
}

// General 4x4 inverse (cofactor expansion).
bMatrix4 *eInvertMatrix(bMatrix4 *dest, bMatrix4 *src) {
    const float *m = reinterpret_cast<const float *>(src);
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f) {
        std::memmove(dest, src, sizeof(bMatrix4));
        return dest;
    }
    float inv_det = 1.0f / det;
    for (float &f : inv) {
        f *= inv_det;
    }
    std::memcpy(dest, inv, sizeof(inv));
    return dest;
}

// Affine inverse: [R t] -> [R^-1, -t R^-1].
bMatrix4 *eInvertTransformationMatrix(bMatrix4 *dest, const bMatrix4 *tm) {
    bMatrix4 copy;
    std::memcpy(&copy, tm, sizeof(copy));
    return eInvertMatrix(dest, &copy);
}

// Pure rotation: the inverse is the transpose of the 3x3 part.
bMatrix4 *eInvertRotationMatrix(bMatrix4 *dest, bMatrix4 *src) {
    M44 s, r;
    Load(s, src);
    Identity(r);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r[i][j] = s[j][i];
        }
    }
    Store(dest, r);
    return dest;
}
