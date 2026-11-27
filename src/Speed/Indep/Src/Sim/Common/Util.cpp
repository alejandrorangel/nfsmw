#include "../UTil.h"
#include "Speed/Indep/Src/Physics/Dynamics/Inertia.h"

#if defined(EA_PORT_NATIVE)
// speed.exe 1.3: 0x6F1220: an orientation looking along <vec>, rolled to <InUp>
// (world up when null)
UMath::Matrix4 Util_GenerateMatrix(const UMath::Vector3 &vec, const UMath::Vector3 *InUp) {
    UMath::Vector3 up = InUp ? *InUp : UMath::Vector3Make(0.0f, 1.0f, 0.0f);
    UMath::Vector3 forward;
    UMath::Unit(vec, forward);
    UMath::Vector3 side;
    UMath::Cross(up, forward, side);
    UMath::Unit(side, side);
    UMath::Vector3 newup;
    UMath::Cross(forward, side, newup);

    UMath::Matrix4 m;
    m.v0 = UMath::Vector4Make(side, 0.0f);
    m.v1 = UMath::Vector4Make(newup, 0.0f);
    m.v2 = UMath::Vector4Make(forward, 0.0f);
    m.v3 = UMath::Vector4::kIdentity;
    return m;
}
#else
// UMath::Matrix4 Util_GenerateMatrix(const UMath::Vector3 &vec, const UMath::Vector3 *InUp) {}
#endif

UMath::Vector3 Util_GenerateCarTensor(const float mass, const float width, const float height, const float length,
                                      const UMath::Vector3 &tensorScale) {
    Dynamics::Inertia::Tensor t = Dynamics::Inertia::Box(mass, 2 * width, 2 * height, 2 * length);
    UMath::Scale(t, tensorScale, t);
    return t;
}
