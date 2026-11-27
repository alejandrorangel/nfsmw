// USpline: cubic Bezier / Catmull-Rom curves (road segments, AI paths),
// reconstructed from speed.exe 1.3 -- the decomp has only the header.
//
//   0x5D5ED0 ctor             0x5D51E0 dtor
//   0x5D0BF0 BuildSplineEx    0x5D0C70 EvaluateSpline
//   0x5D0D10 EvaluateDerivative (static helper)   0x5D0D80 Evaluate2ndDerivative
//   0x5D2D30 EvaluateTangent  0x5D2E60 EvaluateCurvatureXZ
//   basis tables 0x8A3180 (Bezier), 0x8A31C0 (Catmull-Rom) and their
//   1st/2nd derivative tables 0x8A3200/0x8A3240, 0x8A3280/0x8A32C0
//
// fSplineMat rows are the four control points (start, start control, end
// control, end); a point is [t^3 t^2 t 1] * Basis * fSplineMat.

#include "Speed/Indep/Libs/Support/Utility/USpline.h"

#include <cmath>

namespace {

const UMath::Matrix4 kBezierBasis = {
    {-1.0f, 3.0f, -3.0f, 1.0f}, {3.0f, -6.0f, 3.0f, 0.0f}, {-3.0f, 3.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}};
const UMath::Matrix4 kCatRomBasis = {
    {-1.0f, 3.0f, -3.0f, 1.0f}, {2.0f, -5.0f, 4.0f, -1.0f}, {-1.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.0f}};
const UMath::Matrix4 kBezierTanBasis = {
    {0.0f, 0.0f, 0.0f, 0.0f}, {-3.0f, 9.0f, -9.0f, 3.0f}, {6.0f, -12.0f, 6.0f, 0.0f}, {-3.0f, 3.0f, 0.0f, 0.0f}};
const UMath::Matrix4 kCatRomTanBasis = {
    {0.0f, 0.0f, 0.0f, 0.0f}, {-3.0f, 6.0f, 3.0f, 0.0f}, {6.0f, -10.0f, 0.0f, 4.0f}, {-3.0f, 4.0f, 1.0f, 0.0f}};
const UMath::Matrix4 kBezier2ndBasis = {
    {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {-6.0f, 18.0f, -18.0f, 6.0f}, {6.0f, -12.0f, 6.0f, 0.0f}};
const UMath::Matrix4 kCatRom2ndBasis = {
    {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {-6.0f, 12.0f, 6.0f, 0.0f}, {6.0f, -10.0f, 0.0f, 4.0f}};

// [t^3 t^2 t 1] * basis * points, w = 1
void Evaluate(const UMath::Matrix4 &basis, const UMath::Matrix4 &points, float t, UMath::Vector4 &result) {
    const float tv[4] = {t * t * t, t * t, t, 1.0f};
    float weights[4];
    for (int col = 0; col < 4; col++) {
        weights[col] = tv[0] * basis[0][col] + tv[1] * basis[1][col] + tv[2] * basis[2][col] + tv[3] * basis[3][col];
    }
    float out[3];
    for (int axis = 0; axis < 3; axis++) {
        out[axis] = weights[0] * points[0][axis] + weights[1] * points[1][axis] + weights[2] * points[2][axis] +
                    weights[3] * points[3][axis];
    }
    result.x = out[0];
    result.y = out[1];
    result.z = out[2];
    result.w = 1.0f;
}

void EvaluateDerivative(const USpline &spline, float t, UMath::Vector4 &result) {
    Evaluate(spline.fSplineType == USpline::kSpline_CatMulRom ? kCatRomTanBasis : kBezierTanBasis, spline.fSplineMat, t, result);
}

void Evaluate2ndDerivative(const USpline &spline, float t, UMath::Vector4 &result) {
    Evaluate(spline.fSplineType == USpline::kSpline_CatMulRom ? kCatRom2ndBasis : kBezier2ndBasis, spline.fSplineMat, t, result);
}

} // namespace

USpline::USpline() {
    fSplineType = kSpline_Invalid;
    fSplineMat = UMath::Matrix4::kIdentity;
    fLookAt[0] = UMath::Vector4Make(0.0f, 0.0f, 0.0f, 1.0f);
    fLookAt[1] = UMath::Vector4Make(0.0f, 0.0f, 0.0f, 1.0f);
}

USpline::~USpline() {}

const UMath::Matrix4 &USpline::GetBasisMatrix(SplineType splineType) {
    return splineType == kSpline_CatMulRom ? kCatRomBasis : kBezierBasis;
}

void USpline::BuildSplineEx(const UMath::Vector3 &start, const UMath::Vector3 &startControl, const UMath::Vector3 &end,
                            const UMath::Vector3 &endControl) {
    // a spline built from a point list keeps it
    if (fSplinePtList.size() >= 2) {
        return;
    }
    fSplineType = kSpline_Bezier;
    fSplineMat.v0 = UMath::Vector4Make(start.x, start.y, start.z, 0.0f);
    fSplineMat.v1 = UMath::Vector4Make(startControl.x, startControl.y, startControl.z, 0.0f);
    fSplineMat.v2 = UMath::Vector4Make(endControl.x, endControl.y, endControl.z, 0.0f);
    fSplineMat.v3 = UMath::Vector4Make(end.x, end.y, end.z, 0.0f);
}

void USpline::EvaluateSpline(float t, UMath::Vector4 &result) {
    Evaluate(GetBasisMatrix(fSplineType), fSplineMat, t, result);
    if (fSplineType == kSpline_CatMulRom) {
        result.x *= 0.5f;
        result.y *= 0.5f;
        result.z *= 0.5f;
    }
}

void USpline::EvaluateTangent(float t, UMath::Vector4 &tangent) {
    EvaluateDerivative(*this, t, tangent);
    float len = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
    float scale = len != 0.0f ? 1.0f / len : 0.0f;
    tangent.x *= scale;
    tangent.y *= scale;
    tangent.z *= scale;
}

float USpline::EvaluateCurvatureXZ(float t) {
    UMath::Vector4 d;
    UMath::Vector4 dd;
    EvaluateDerivative(*this, t, d);
    Evaluate2ndDerivative(*this, t, dd);
    float len = std::sqrt(d.z * d.z + d.x * d.x);
    float denom = len * len * len;
    if (denom < 1e-4f && denom > -1e-4f) {
        return 1000.0f;
    }
    return (dd.z * d.x - dd.x * d.z) / denom;
}
