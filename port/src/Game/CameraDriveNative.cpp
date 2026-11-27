// The in-race player camera, reconstructed from speed.exe 1.3 (the decomp has
// only declarations for CameraAI and nothing of the drive camera):
//
//   CameraAI          0x480E40 Update    0x47CC50 Reset    0x479EB0 SetAction
//                     0x471B60 StartCinematicSlowdown      0x479F20 MaybeDoTotaledCam
//   CameraAI::Director 0x480D20 ctor     0x479DE0 Update   0x479900 SelectAction
//                     0x4797F0 SetAction 0x480DD0 dtor
//   CDActionDrive     0x484C20 ctor      0x481030 dtor     0x472740 Update
//                     0x4725D0 AquireCar 0x472460 OnCollision 0x485BA0 Construct
//   CameraAnchor      0x4716A0 ctor      0x468FA0 Update   0x4713C0 GetPov  0x471370 SetModel
//   CameraMover       0x468BE0 FovCubicInit 0x468C50 EyeCubicInit 0x468D30 LookCubicInit
//   CubicCameraMover  0x481BD0 ctor      0x474100 dtor     0x47D3C0 Update
//                     0x47CF00 SetDesired 0x46B1F0 SetForward 0x46B410 BuildBasis
//                     0x47CDD0 CameraAccelCurve 0x47CE80 CameraSpeedHug
//                     0x47C440 avoidable push   0x470FE0 MakeSpace 0x470BF0 wall clearance
//                     0x47BEC0 terrain/velocity noise  0x46B0C0 ResetState
//
// The director picks a CameraAI::Action per player view; the actions ported
// are CDActionDrive, which owns a CameraAnchor (the followed car's state) and
// a CubicCameraMover attached to the view, and CDActionIce (ICENative.cpp). Tables are the 1.3
// .data values (addresses next to each).

#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Camera/CameraAI.hpp"
#include "Speed/Indep/Src/Camera/CameraMover.hpp"
#include "Speed/Indep/Src/Camera/Movers/Cubic.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/Database/VehicleDB.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/pvehicle.h"
#include "Speed/Indep/Src/Interfaces/IBody.h"
#include "Speed/Indep/Src/Interfaces/IListener.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/ICollisionBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/IEngine.h"
#include "Speed/Indep/Src/Interfaces/Simables/IINput.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRigidBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISuspension.h"
#include "Speed/Indep/Src/Interfaces/Simables/ITransmission.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#include "Speed/Indep/Src/Misc/Point.hpp"
#include "Speed/Indep/Src/Misc/Timer.hpp"
#include "Speed/Indep/Src/Sim/Collision.h"
#include "Speed/Indep/Src/Sim/SimTypes.h"
#include "Speed/Indep/Src/Sim/Simulation.h"
#include "Speed/Indep/Src/World/CarInfo.hpp"
#include "Speed/Indep/Src/World/Rain.hpp"
#include "Speed/Indep/Src/World/WCollisionMgr.h"
#include "Speed/Indep/Src/World/WWorldMath.h"
#include "Speed/Indep/Src/World/WWorldPos.h"
#include "Speed/Indep/Src/World/WorldConn.h"
#include "Speed/Indep/Src/Camera/ICE/ICEAnimScene.hpp"
#include "Speed/Indep/Src/Camera/ICE/ICEManager.hpp"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Generated/Messages/MICECameraFinished.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/INIS.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/ITrafficCenter.h"
#include "CameraAINative.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>

bool gCinematicMomementCamera; // 0x911054
bool gGameBreakerCamera;       // 0x911055

void SetNewSndCamAction(Attrib::StringKey camtype, EVIEW_ID viewID);
void ApplyCameraShake(int nViewID, bMatrix4 *pMatrix);
void MaybeCameraShake(int nPlayer, bVector3 *pAccel);
void FlushAccumulationBuffer();
namespace Attrib {
Key StringToLowerCaseKey(const char *str);
}

namespace {

const float kTicksToSeconds = 0.00025f; // WorldTimer ticks (1/4000 s)

int Now() {
    return WorldTimer.GetPackedTime();
}

float Clamp01(float v) {
    return v <= 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// (int)(degrees * 65536) / 360, as the original rounds it.
bAngle DegreesToAngle(float degrees) {
    int scaled = static_cast<int>(degrees * 65536.0f);
    return static_cast<bAngle>(scaled / 360);
}

bool IsOutsidePov(int type) {
    return type == POV_OUTSIDE_CLOSE || type == POV_OUTSIDE_FAR || type == POV_SUPER_FAR || type == POV_PURSUIT || type == POV_DRIFT;
}

void InitCubic(tCubic1D &c, short flags, float duration) {
    std::memset(&c, 0, sizeof(c));
    c.duration = duration;
    c.state = 0;
    c.flags = flags;
}

void InitCubic(tCubic3D &c, short flags, float dx, float dy, float dz) {
    InitCubic(c.x, flags, dx);
    InitCubic(c.y, flags, dy);
    InitCubic(c.z, flags, dz);
}

void SetDurations(tCubic3D &c, float dx, float dy, float dz) {
    c.x.duration = dx;
    c.y.duration = dy;
    c.z.duration = dz;
}

void Snap(tCubic3D *c) {
    c->x.Snap();
    c->y.Snap();
    c->z.Snap();
}

void SetValDesired(tCubic3D *c, const bVector3 &v) {
    c->SetValDesired(v.x, v.y, v.z);
}

bVector3 Val(const tCubic3D *c) {
    return bVector3(c->x.Val, c->y.Val, c->z.Val);
}

bVector3 ClampV(const bVector3 &v, const bVector3 &lo, const bVector3 &hi) {
    return bVector3(ClampF(v.x, lo.x, hi.x), ClampF(v.y, lo.y, hi.y), ClampF(v.z, lo.z, hi.z));
}

// tTable<T>::GetValue (0x46F410 / 0x46F4A0 / 0x46F020 / 0x46F2B0): evenly
// spaced entries over [min, max], linear blend between neighbours.
template <typename T, typename Blend> void TableValue(T *out, const T *table, int num, float min, float mult, float arg, Blend blend) {
    float f = (arg - min) * mult;
    int i = static_cast<int>(f);
    if (i < 0) {
        *out = table[0];
        return;
    }
    if (i >= num - 1) {
        *out = table[num - 1];
        return;
    }
    float fi = static_cast<float>(i);
    if (f < fi) {
        fi -= 1.0f;
    }
    blend(out, table[i + 1], table[i], f - fi);
}

float TableFloat(const float *table, int num, float min, float max, float arg) {
    float out;
    TableValue(&out, table, num, min, (num - 1) / (max - min), arg,
               [](float *o, float a, float b, float t) { *o = t * a + (1.0f - t) * b; });
    return out;
}

// Graph::GetValue over (x, y) points, clamped at the ends.
float GraphValue(const bVector2 *pts, int num, float x) {
    if (x <= pts[0].x) {
        return pts[0].y;
    }
    for (int i = 0; i < num - 1; i++) {
        if (x < pts[i + 1].x) {
            float t = (x - pts[i].x) / (pts[i + 1].x - pts[i].x);
            return pts[i].y + (pts[i + 1].y - pts[i].y) * t;
        }
    }
    return pts[num - 1].y;
}

} // namespace

// ---------------------------------------------------------------------------
// Tweak tables (speed.exe 1.3 .data)
// ---------------------------------------------------------------------------

// tTable<CubicTweak> entry, 0xB0 bytes: spline durations plus per-axis
// lag scales and clamps, blended by speed between a slow and a fast entry.
struct CubicCameraTweak {
    float EyeDuration;  // 0x00
    float LookDuration; // 0x04
    float FovDuration;  // 0x08
    float UpDuration;   // 0x0C
    bVector3 UpScale, UpMin, UpMax;       // 0x10.. tilt from averaged acceleration
    bVector3 EyeScale, EyeMin, EyeMax;    // 0x40..
    bVector3 LookScale, LookMin, LookMax; // 0x70..
    bVector3 ForwardDuration;             // 0xA0
};

namespace {

#define V3(x, y, z) bVector3(x, y, z)
// 0x8EE040: one 2-entry table per POV type over speed fraction [0, 1].
const CubicCameraTweak kCubicTweaks[NUM_POV_TYPES][2] = {
    // POV_BUMPER (0x8ED460)
    {{0, 0, 1, 0, V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0),
      V3(0, 0, 0)},
     {0, 0, 0.5f, 0, V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0),
      V3(0, 0, 0)}},
    // POV_HOOD (0x8ED5C0)
    {{0.7f, 0.7f, 1, 0.7f, V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(-0.01f, -0.02f, 0.1f), V3(-0.1f, -0.1f, -0.1f), V3(0.3f, 0.1f, 0.1f),
      V3(-0.1f, -0.02f, 0), V3(-0.05f, -0.1f, 0), V3(0.2f, 0.1f, 0), V3(0.4f, 0.4f, 0.4f)},
     {0.5f, 0.5f, 0.5f, 0.5f, V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(-0.1f, -0.1f, -0.1f), V3(0.3f, 0.1f, 0.1f), V3(0, 0, 0),
      V3(-0.05f, -0.1f, 0), V3(0.2f, 0.1f, 0), V3(0.2f, 0.2f, 0.2f)}},
    // POV_OUTSIDE_CLOSE (0x8ED720)
    {{1.25f, 0.4f, 1, 0.8f, V3(0.004f, 0.004f, 0), V3(-1, -1, 1), V3(1, 1, 1), V3(-1, 0, 0), V3(-1.25f, -1, -1), V3(1.5f, 1, 1),
      V3(-0.5f, 0, 0), V3(-2, 0, -1), V3(2, 0, 1), V3(0.4f, 0.4f, 0.4f)},
     {0.75f, 0.3f, 0.5f, 0.4f, V3(0.002f, 0.002f, 0), V3(-1, -1, 1), V3(1, 1, 1), V3(-0.75f, 0, 0), V3(-1, -1, -1), V3(1, 1, 1),
      V3(-0.2f, 0, 0), V3(-0.5f, 0, -1), V3(0.5f, 0, 1), V3(0.2f, 0.2f, 0.2f)}},
    // POV_OUTSIDE_FAR (0x8ED880)
    {{1.25f, 0.4f, 1, 0.8f, V3(0.005f, 0.005f, 0), V3(-1, -1, 1), V3(1, 1, 1), V3(-1, 0, 0), V3(-1, -1, -1), V3(2, 1, 1), V3(-0.1f, 0, 0),
      V3(-2, 0, -1), V3(2, 0, 1), V3(0.4f, 0.4f, 0.4f)},
     {0.75f, 0.3f, 0.5f, 0.4f, V3(0.003f, 0.003f, 0), V3(-1, -1, 1), V3(1, 1, 1), V3(-0.5f, 0, 0), V3(-1, -1, -1), V3(2, 1, 1),
      V3(-0.05f, 0, 0), V3(-0.5f, 0, -1), V3(0.5f, 0, 1), V3(0.2f, 0.2f, 0.2f)}},
    // POV_SUPER_FAR (0x8ED9E0); POV_DRIFT (0x8EDB40) and POV_PURSUIT (0x8EDCA0) are identical
    {{1.5f, 0.5f, 1, 1, V3(0.006f, 0.006f, 0), V3(-1, -1, 1), V3(1, 1, 1), V3(-0.25f, 0, 0), V3(-2.5f, -1, -1), V3(2, 1, 1),
      V3(-0.1f, 0, 0), V3(-2, 0, -1), V3(2, 0, 1), V3(0.5f, 0.5f, 0.5f)},
     {0.5f, 0.2f, 0.5f, 0.5f, V3(0.002f, 0.002f, 0), V3(-1, -1, 1), V3(1, 1, 1), V3(-0.05f, 0, 0), V3(-0.5f, -1, -1), V3(0.5f, 1, 1),
      V3(-0.02f, 0, 0), V3(-0.5f, 0, -1), V3(0.5f, 0, 1), V3(0.1f, 0.1f, 0.1f)}},
    {{1.5f, 0.5f, 1, 1, V3(0.006f, 0.006f, 0), V3(-1, -1, 1), V3(1, 1, 1), V3(-0.25f, 0, 0), V3(-2.5f, -1, -1), V3(2, 1, 1),
      V3(-0.1f, 0, 0), V3(-2, 0, -1), V3(2, 0, 1), V3(0.5f, 0.5f, 0.5f)},
     {0.5f, 0.2f, 0.5f, 0.5f, V3(0.002f, 0.002f, 0), V3(-1, -1, 1), V3(1, 1, 1), V3(-0.05f, 0, 0), V3(-0.5f, -1, -1), V3(0.5f, 1, 1),
      V3(-0.02f, 0, 0), V3(-0.5f, 0, -1), V3(0.5f, 0, 1), V3(0.1f, 0.1f, 0.1f)}},
    {{1.5f, 0.5f, 1, 1, V3(0.006f, 0.006f, 0), V3(-1, -1, 1), V3(1, 1, 1), V3(-0.25f, 0, 0), V3(-2.5f, -1, -1), V3(2, 1, 1),
      V3(-0.1f, 0, 0), V3(-2, 0, -1), V3(2, 0, 1), V3(0.5f, 0.5f, 0.5f)},
     {0.5f, 0.2f, 0.5f, 0.5f, V3(0.002f, 0.002f, 0), V3(-1, -1, 1), V3(1, 1, 1), V3(-0.05f, 0, 0), V3(-0.5f, -1, -1), V3(0.5f, 1, 1),
      V3(-0.02f, 0, 0), V3(-0.5f, 0, -1), V3(0.5f, 0, 1), V3(0.1f, 0.1f, 0.1f)}},
};
#undef V3

// sub_46AB60: dest = t * a + (1 - t) * b, field by field.
void BlendTweak(CubicCameraTweak *out, const CubicCameraTweak &a, const CubicCameraTweak &b, float t) {
    const float *pa = reinterpret_cast<const float *>(&a);
    const float *pb = reinterpret_cast<const float *>(&b);
    float *po = reinterpret_cast<float *>(out);
    for (size_t i = 0; i < sizeof(CubicCameraTweak) / sizeof(float); i++) {
        po[i] = t * pa[i] + (1.0f - t) * pb[i];
    }
}

void GetCubicTweak(int pov_type, float speed_fraction, CubicCameraTweak *out) {
    if (pov_type < 0 || pov_type >= NUM_POV_TYPES) {
        pov_type = POV_OUTSIDE_FAR;
    }
    // tTable(2 entries, 0..1): index = (int)arg.
    TableValue(out, kCubicTweaks[pov_type], 2, 0.0f, 1.0f, speed_fraction,
               [](CubicCameraTweak *o, const CubicCameraTweak &a, const CubicCameraTweak &b, float t) { BlendTweak(o, a, b, t); });
}

const float kCameraAccelerationCurve[5] = {-1.0f, -0.1f, 0.0f, 0.1f, 1.0f};                              // 0x8EDE54, -30..30
const float kCameraGearChangingCurve[9] = {0.0f, 0.2f, 0.13f, -0.08f, -0.2f, -0.13f, -0.07f, -0.03f, 0.0f}; // 0x8EDE68, 0..1.5 s
const float kCameraImpactCurveH[5] = {0.0f, 0.02f, 0.25f, 0.99f, 0.0f};                                  // 0x8EDE8C, 0..1
const float kCameraImpactCurveV[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};                                     // 0x91107C (.bss, never set)
const bVector2 kSpeedHug[5] = {bVector2(1.0f, 1.0f), bVector2(1.0f, 1.0f), bVector2(1.02f, 1.0f), bVector2(1.125f, 1.0f),
                               bVector2(1.2f, 1.0f)}; // 0x8EE0CC, 0..top speed
const bVector2 kForwardBlendGraph[2] = {bVector2(8.0f, 0.0f), bVector2(13.0f, 1.0f)}; // 0x8EE02C (Graph at 0x9111F0)
const bVector4 kTerrainNoise[5] = {bVector4(0.0f, 0.0f, 0.6f, 1.0f), bVector4(0.005f, 0.5f, 1.1f, 1.1f), bVector4(0.03f, 0.8f, 1.2f, 1.2f),
                                   bVector4(0.07f, 1.0f, 1.3f, 1.3f), bVector4(0.02f, 1.0f, 1.0f, 1.4f)}; // 0x8EDFB0, 0..80
const float kTerrainNoiseFreq1[4] = {1.8f, 2.0f, 2.125f, 2.0f};    // 0x8EDF70
const float kTerrainNoiseAmp1[4] = {0.03f, 0.025f, 0.68f, 0.28f};  // 0x8EDF80
const float kTerrainNoiseFreq2[4] = {3.0f, 5.0f, 7.0f, 5.5f};      // 0x8EDF90
const float kTerrainNoiseAmp2[4] = {0.007f, 0.01f, 0.3f, 0.4f};    // 0x8EDFA0
const float kCameraImpactThreshold = 24.0f;                        // 0x9111E0
const bAngle kNosFovBump = 0x666;                                  // 0x91112C
const float kChopperNoiseScale[NUM_POV_TYPES] = {1, 1, 1, 1, 1, 1, 1};  // 0x8EDE00
const float kTerrainSpeedScale[NUM_POV_TYPES] = {1, 1, 1, 1, 1, 1, 1};  // 0x8EDE1C
const float kTerrainScale[NUM_POV_TYPES] = {1, 1, 1, 1, 1, 1, 1};       // 0x8EDE38
const float kHandheldNoiseScale[NUM_POV_TYPES] = {0, 0, 0, 0, 0, 0, 0}; // 0x911060 (.bss, never set)

// Bodies the cameras steer around (0x911050), maintained by PVehicle.
DECLARE_CONTAINER_TYPE(CameraAIAvoidables);
UTL::Std::list<IBody *, _type_CameraAIAvoidables> &CameraAIAvoidables() {
    static UTL::Std::list<IBody *, _type_CameraAIAvoidables> avoidables;
    return avoidables;
}

} // namespace

// tAverage<bVector3>(5): running mean over the last 5 samples.
class CubicAccelAverage {
  public:
    explicit CubicAccelAverage(int slots) : nSlots(slots), nSamples(0), nCurrent(0), vTotal(0.0f, 0.0f, 0.0f), vAverage(0.0f, 0.0f, 0.0f) {
        for (int i = 0; i < kMaxSlots; i++) {
            aData[i] = bVector3(0.0f, 0.0f, 0.0f);
        }
    }
    void Record(const bVector3 &v) {
        if (nSamples < nSlots) {
            nSamples++;
        }
        vTotal -= aData[nCurrent];
        vTotal += v;
        aData[nCurrent] = v;
        float inv = 1.0f / static_cast<float>(nSamples);
        vAverage = bVector3(vTotal.x * inv, vTotal.y * inv, vTotal.z * inv);
        if (++nCurrent >= nSlots) {
            nCurrent = 0;
        }
    }
    const bVector3 &GetValue() const {
        return vAverage;
    }

  private:
    static const int kMaxSlots = 5;
    int nSlots, nSamples, nCurrent;
    bVector3 aData[kMaxSlots];
    bVector3 vTotal;
    bVector3 vAverage;
};

// ---------------------------------------------------------------------------
// CameraAnchor
// ---------------------------------------------------------------------------

CameraAnchor::CameraAnchor(int model)
    : mVelocity(0.0f, 0.0f, 0.0f), mVelMag(0.0f), mTopSpeed(0.0f), mGeomPos(0.0f, 0.0f, 0.0f), mDimension(0.0f, 0.0f, 0.0f),
      mAccel(0.0f, 0.0f, 0.0f), mModel(0), mWorldID(0), mSurface(SimSurface::kNull), mCollisionDamping(0.0f), mDrift(0.0f),
      mGroundCollision(0.0f), mObjectCollision(0.0f), mIsNosEngaged(false), mIsBrakeEngaged(false), mIsDragRace(false), mIsOverRev(false),
      mIsTouchingGround(false), mIsVehicleDestroyed(false), mIsGearChanging(false), mIsCloseToRoadBlock(false), mZoom(1.0f),
      mModelAttributes(0xeec2271a, 0, nullptr), mCameraInfoAttributes(0xeec2271a, 0, nullptr) {
    mPOV.Type = POV_OUTSIDE_FAR;
    mPOV.Angle = DegreesToAngle(mCameraInfoAttributes.ANGLE(0));
    mPOV.Lag = mCameraInfoAttributes.LAG(0);
    mPOV.Height = mCameraInfoAttributes.HEIGHT(0);
    mPOV.LatOffset = mCameraInfoAttributes.LATOFFSET(0);
    mPOV.Fov = DegreesToAngle(mCameraInfoAttributes.FOV(0));
    mPOV.AllowTilting = mCameraInfoAttributes.TILTING(0);
    mPOV.Stiffness = mCameraInfoAttributes.STIFFNESS(0);
    SetModel(model);
    bIdentity(&mGeomRot);
    mGeomRot.v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
}

CameraAnchor::~CameraAnchor() {}

void CameraAnchor::SetModel(int model) {
    if (mModel == model) {
        return;
    }
    CarTypeInfo *info = GetCarTypeInfoFromHash(static_cast<uint32>(model));
    const char *name = info != nullptr ? info->GetName() : "";
    mModel = model;
    mModelAttributes.Change(Attrib::FindCollectionWithDefault(Attrib::Gen::ecar::ClassKey(), Attrib::StringToLowerCaseKey(name)));
}

POV *CameraAnchor::GetPov(int pov_type) {
    mPOV.Type = static_cast<short>(pov_type);
    switch (pov_type) {
        case POV_BUMPER:
            mCameraInfoAttributes.ChangeWithDefault(mModelAttributes.CameraInfo_Bumper());
            break;
        case POV_HOOD:
            mCameraInfoAttributes.ChangeWithDefault(mModelAttributes.CameraInfo_Hood());
            break;
        case POV_OUTSIDE_CLOSE:
            mCameraInfoAttributes.ChangeWithDefault(mModelAttributes.CameraInfo_Close());
            break;
        case POV_OUTSIDE_FAR:
            mCameraInfoAttributes.ChangeWithDefault(mModelAttributes.CameraInfo_Far());
            break;
        case POV_SUPER_FAR:
            mCameraInfoAttributes.ChangeWithDefault(mModelAttributes.CameraInfo_SuperFar());
            break;
        case POV_DRIFT:
            mCameraInfoAttributes.ChangeWithDefault(mModelAttributes.CameraInfo_Drift());
            break;
        case POV_PURSUIT:
            mCameraInfoAttributes.ChangeWithDefault(mModelAttributes.CameraInfo_Pursuit());
            break;
        default:
            mPOV.Type = POV_OUTSIDE_FAR;
            mCameraInfoAttributes.Change(Attrib::FindCollection(Attrib::Gen::camerainfo::ClassKey(), 0xeec2271a));
            break;
    }
    float zoom = mZoom > 0.0f ? mZoom : 1.0f;
    unsigned int i = eGetCurrentViewMode() == 3 ? 1 : 0; // split screen uses the second column
    mPOV.Angle = DegreesToAngle(mCameraInfoAttributes.ANGLE(i));
    mPOV.Lag = mCameraInfoAttributes.LAG(i) / zoom;
    mPOV.Height = mCameraInfoAttributes.HEIGHT(i);
    mPOV.LatOffset = mCameraInfoAttributes.LATOFFSET(i);
    mPOV.Fov = DegreesToAngle(mCameraInfoAttributes.FOV(i));
    mPOV.AllowTilting = mCameraInfoAttributes.TILTING(i);
    mPOV.Stiffness = mCameraInfoAttributes.STIFFNESS(i);
    return &mPOV;
}

void CameraAnchor::Update(float dT, const bMatrix4 &matrix, const bVector3 &velocity, const bVector3 &accel) {
    float moved = bDistBetween(&mGeomPos, reinterpret_cast<const bVector3 *>(&matrix.v3));
    mGeomRot = matrix;
    mGeomRot.v3.x = 0.0f;
    mGeomRot.v3.y = 0.0f;
    mGeomRot.v3.z = 0.0f;
    mGeomPos = bVector3(matrix.v3.x, matrix.v3.y, matrix.v3.z);
    float prev_mag = mVelMag;
    mVelocity = velocity;
    mVelMag = bLength(&mVelocity);
    if (dT > 0.0f && moved * (1.0f / dT) < 300.0f) {
        // Longitudinal acceleration along the car's heading (accel is unused).
        bVector3 local((mVelMag - prev_mag) * (1.0f / dT), 0.0f, 0.0f);
        bMulMatrix(&mAccel, &mGeomRot, &local);
    } else {
        mAccel = bVector3(0.0f, 0.0f, 0.0f);
    }
}

// ---------------------------------------------------------------------------
// CameraMover spline helpers
// ---------------------------------------------------------------------------

void CameraMover::FovCubicInit(tCubic1D *fov) {
    float vel_fov = static_cast<float>(pCamera->GetVelocityFov());
    fov->SetVal(vel_fov * (1.0f / 30.0f) + static_cast<float>(pCamera->GetFov()));
    fov->SetdVal(vel_fov * fov->duration);
}

void CameraMover::EyeCubicInit(tCubic3D *eye, bMatrix4 *world_to_local, bVector3 *anchor_velocity) {
    bVector3 v;
    bScaleAdd(&v, pCamera->GetPosition(), pCamera->GetVelocityPosition(), 1.0f / 30.0f);
    if (world_to_local != nullptr) {
        bMulMatrix(&v, world_to_local, &v);
    }
    eye->SetVal(&v);
    bVector3 vel = *pCamera->GetVelocityPosition();
    if (anchor_velocity != nullptr) {
        vel -= *anchor_velocity;
    }
    float d = eye->x.duration;
    bVector4 dv(vel.x * d, vel.y * d, vel.z * d, 0.0f);
    if (world_to_local != nullptr) {
        eMulVector(&dv, world_to_local, &dv);
    }
    eye->SetdVal(dv.x, dv.y, dv.z);
}

void CameraMover::LookCubicInit(tCubic3D *look, bMatrix4 *world_to_local, bVector3 *anchor_velocity) {
    bVector3 v;
    bScaleAdd(&v, pCamera->GetTarget(), pCamera->GetVelocityTarget(), 1.0f / 30.0f);
    if (world_to_local != nullptr) {
        bMulMatrix(&v, world_to_local, &v);
    }
    look->SetVal(&v);
    bVector3 vel = *pCamera->GetVelocityTarget();
    if (anchor_velocity != nullptr) {
        vel -= *anchor_velocity;
    }
    float d = look->x.duration;
    bVector4 dv(vel.x * d, vel.y * d, vel.z * d, 0.0f);
    if (world_to_local != nullptr) {
        eMulVector(&dv, world_to_local, &dv);
    }
    look->SetdVal(dv.x, dv.y, dv.z);
}

void CameraMover::SetEyeLook(tCubic3D *eye, tCubic3D *look, tCubic1D *fov, bMatrix4 *world_to_local, bVector3 *anchor_velocity) {
    FovCubicInit(fov);
    EyeCubicInit(eye, world_to_local, anchor_velocity);
    LookCubicInit(look, world_to_local, anchor_velocity);
}

// ---------------------------------------------------------------------------
// CubicCameraMover
// ---------------------------------------------------------------------------

CubicCameraMover::CubicCameraMover(int view_id, CameraAnchor *anchor, int pov_type, bool blend_from_ice, bool disable_lag, bool look_back,
                                   bool perfect_focus)
    : CameraMover(view_id, CM_DRIVE_CUBIC) {
    nPovType = pov_type;
    nPovTypeUsed = pov_type;
    pCar = anchor;
    bPerfectFocus = perfect_focus;
    bAccelLag = !disable_lag;
    bLookBack = look_back;
    bSnapNext = false;
    int now = Now();
    tLastGrounded = now - 8000;
    tLastUnderVehicle = now - 6400;
    tLastGearChange = now - 6000;
    fIceBlend = 0.0f;
    bFirstTime = true;

    POV *pov = pCar->GetPov(nPovType);
    CubicCameraTweak tweak;
    GetCubicTweak(nPovType, 0.0f, &tweak);
    pFov = new tCubic1D(0, 0.0f);
    InitCubic(*pFov, 1, tweak.FovDuration);
    pEye = new tCubic3D(1, 0.0f);
    InitCubic(*pEye, 1, tweak.EyeDuration, tweak.EyeDuration, tweak.EyeDuration);
    pLook = new tCubic3D(1, 0.0f);
    InitCubic(*pLook, 1, tweak.LookDuration, tweak.LookDuration, tweak.LookDuration);
    pForward = new tCubic3D(1, 0.0f);
    InitCubic(*pForward, 1, tweak.ForwardDuration.x, tweak.ForwardDuration.y, tweak.ForwardDuration.z);
    pUp = new tCubic3D(1, 0.0f);
    InitCubic(*pUp, 1, tweak.UpDuration, tweak.UpDuration, tweak.UpDuration);
    pAvgAccel = new CubicAccelAverage(5);

    bMatrix4 basis, world_to_local;
    SetDesired(&basis, pov, &tweak, true);
    eInvertTransformationMatrix(&world_to_local, &basis);
    if (bLookBack) {
        world_to_local.v0.x = -world_to_local.v0.x;
        world_to_local.v0.y = -world_to_local.v0.y;
        world_to_local.v1.x = -world_to_local.v1.x;
        world_to_local.v1.y = -world_to_local.v1.y;
        world_to_local.v2.x = -world_to_local.v2.x;
        world_to_local.v2.y = -world_to_local.v2.y;
        world_to_local.v3.x = -world_to_local.v3.x;
        world_to_local.v3.y = -world_to_local.v3.y;
    }
    SetEyeLook(pEye, pLook, pFov, &world_to_local, pCar->GetVelocity());

    bVector3 eye, look, eye_desired, look_desired;
    pEye->GetVal(&eye);
    pLook->GetVal(&look);
    pEye->GetValDesired(&eye_desired);
    pLook->GetValDesired(&look_desired);
    bVector3 dir = look - eye;
    bVector3 dir_desired = look_desired - eye_desired;
    bNormalize(&dir, &dir);
    bNormalize(&dir_desired, &dir_desired);
    vSavedEye = bVector3(0.0f, 0.0f, 0.0f);
    vCameraImpact = bVector2(0.0f, 0.0f);
    vCameraImpactTimer = bVector2(0.0f, 0.0f);
    // Blend in from where the camera is (ICE hand-off) unless it is far off.
    if (!blend_from_ice || bDistBetween(&eye_desired, &eye) > 50.0f || bDot(&dir_desired, &dir) < -0.9f) {
        Snap(pUp);
        pFov->Snap();
        Snap(pEye);
        Snap(pLook);
    } else {
        fIceBlend = 1.0f;
    }
}

CubicCameraMover::~CubicCameraMover() {
    delete pUp;
    delete pFov;
    delete pEye;
    delete pLook;
    delete pForward;
    delete pAvgAccel;
}

void CubicCameraMover::SetPovType(int pov_type) {
    if (pov_type != nPovTypeUsed) {
        // Switching between two outside cameras blends; anything else snaps.
        bSnapNext |= !(IsOutsidePov(pov_type) && IsOutsidePov(nPovTypeUsed));
        nPovType = pov_type;
    }
}

bool CubicCameraMover::OutsidePOV() {
    return IsOutsidePov(nPovTypeUsed);
}

bool CubicCameraMover::RenderCarPOV() {
    switch (nPovTypeUsed) {
        case POV_OUTSIDE_CLOSE:
        case POV_OUTSIDE_FAR:
        case POV_SUPER_FAR:
        case POV_PURSUIT:
        case POV_DRIFT:
            return true;
        case POV_HOOD:
            return !bLookBack;
        default:
            return false;
    }
}

float CubicCameraMover::MinDistToWall() {
    return 0.7f; // 0x8945E8
}

bool CubicCameraMover::IsHoodCamera() {
    return nPovTypeUsed == POV_HOOD && !bLookBack;
}

void CubicCameraMover::ResetState() {
    Snap(pUp);
    pFov->Snap();
    Snap(pEye);
    Snap(pLook);
    Snap(pForward);
    GetCamera()->ClearVelocity();
    vCameraImpact = bVector2(0.0f, 0.0f);
    vCameraImpactTimer = bVector2(0.0f, 0.0f);
}

bool CubicCameraMover::IsUnderVehicle() {
    // TODO(port): 0x4741F0 tests the camera against the boxes of other active
    // vehicles (trucks); not ported, so the camera never ducks.
    return false;
}

void CubicCameraMover::CameraAccelCurve(bVector3 *accel) {
    const bVector3 *a = pCar->GetAcceleration();
    accel->x = TableFloat(kCameraAccelerationCurve, 5, -30.0f, 30.0f, a->x) * 30.0f;
    accel->y = TableFloat(kCameraAccelerationCurve, 5, -30.0f, 30.0f, a->y) * 30.0f;
    accel->z = TableFloat(kCameraAccelerationCurve, 5, -30.0f, 30.0f, a->z) * 30.0f;
}

void CubicCameraMover::CameraSpeedHug(bVector3 *offset) {
    float top = pCar->mTopSpeed;
    if (top > 0.0f) {
        bVector2 hug;
        TableValue(&hug, kSpeedHug, 5, 0.0f, 4.0f / top, pCar->mVelMag,
                   [](bVector2 *o, const bVector2 &a, const bVector2 &b, float t) { *o = bVector2(t * a.x + (1.0f - t) * b.x, t * a.y + (1.0f - t) * b.y); });
        offset->x *= hug.x;
        offset->z *= hug.y;
    }
}

void CubicCameraMover::SetForward(POV *pov, bool snap) {
    if (pov == nullptr || !IsOutsidePov(pov->Type)) {
        if (pCar != nullptr) {
            SetValDesired(pForward, *reinterpret_cast<bVector3 *>(&pCar->mGeomRot.v0));
            Snap(pForward);
        }
        return;
    }
    const bVector3 &heading = *reinterpret_cast<bVector3 *>(&pCar->mGeomRot.v0);
    bVector3 fwd = pCar->mVelocity;
    float d = bDot(&fwd, &heading);
    if (d < 0.0f) {
        bScaleAdd(&fwd, &fwd, &heading, d * -2.0f); // reversing: mirror onto the heading
    }
    float g = GraphValue(kForwardBlendGraph, 2, pCar->mVelMag);
    bNormalize(&fwd, &fwd);
    fwd = bVector3(fwd.x * g, fwd.y * g, fwd.z * g);
    bScaleAdd(&fwd, &fwd, &heading, 1.0f - g);
    float grounded = Clamp01(1.0f - static_cast<float>(Now() - tLastGrounded) * kTicksToSeconds * 0.5f);
    float hz = pCar->mGeomRot.v0.z;
    fwd.z = (1.0f - hz * hz) * grounded * fwd.z;
    SetValDesired(pForward, fwd);
    if (snap) {
        Snap(pForward);
    }
}

void CubicCameraMover::BuildBasis(bMatrix4 *basis) {
    if (OutsidePOV()) {
        bIdentity(basis);
        bVector3 fwd = Val(pForward);
        bVector3 *v0 = reinterpret_cast<bVector3 *>(&basis->v0);
        bVector3 *v1 = reinterpret_cast<bVector3 *>(&basis->v1);
        bVector3 *v2 = reinterpret_cast<bVector3 *>(&basis->v2);
        bNormalize(v0, &fwd);
        bCross(v1, v2, v0);
        bCross(v2, v0, v1);
        basis->v3 = bVector4(pCar->mGeomPos.x, pCar->mGeomPos.y, pCar->mGeomPos.z, 1.0f);
        return;
    }
    *basis = pCar->mGeomRot;
    basis->v3 = bVector4(pCar->mGeomPos.x, pCar->mGeomPos.y, pCar->mGeomPos.z, 1.0f);
    if (pCar->mGeomRot.v2.z < 0.0f) { // upside down
        basis->v1 = bVector4(-basis->v1.x, -basis->v1.y, -basis->v1.z, -basis->v1.w);
        basis->v2 = bVector4(-basis->v2.x, -basis->v2.y, -basis->v2.z, -basis->v2.w);
    }
}

bVector3 CubicCameraMover::AvoidablesPush(const bVector3 &pos, const bVector3 &vel) {
    bVector3 push(0.0f, 0.0f, 0.0f);
    UTL::Std::list<IBody *, _type_CameraAIAvoidables> &list = CameraAIAvoidables();
    for (UTL::Std::list<IBody *, _type_CameraAIAvoidables>::iterator it = list.begin(); it != list.end(); ++it) {
        IBody *body = *it;
        UMath::Matrix4 sim;
        body->GetTransform(sim);
        bMatrix4 m;
        bConvertFromBond(m, *reinterpret_cast<bMatrix4 *>(&sim));
        bVector3 diff(m.v3.x - pos.x, m.v3.y - pos.y, m.v3.z - pos.z);
        float d2 = bDot(&diff, &diff);
        if (d2 <= 0.64f || d2 >= 49.0f) {
            continue;
        }
        bNormalize(&diff, &diff);
        bVector3 vel_n;
        bNormalize(&vel_n, &vel);
        if (bDot(&vel_n, &diff) <= 0.5f) {
            continue;
        }
        float w = Clamp01(4.84f / d2);
        UMath::Vector3 sim_vel;
        body->GetLinearVelocity(sim_vel);
        bVector3 other(sim_vel.z, -sim_vel.x, sim_vel.y);
        float closing = Clamp01((bDistBetween(&vel, &other) - 15.0f) * (1.0f / 85.0f));
        bVector3 other_n;
        bNormalize(&other_n, &other);
        float d = bDot(&other_n, &vel_n);
        float oncoming = d < 0.0f ? -d : 0.0f;
        bScaleAdd(&push, &push, &diff, (closing + oncoming) * w);
    }
    return push;
}

void CubicCameraMover::SetDesired(bMatrix4 *basis, POV *pov, const CubicCameraTweak *tweak, bool snap) {
    bool outside = IsOutsidePov(pov->Type);
    if (pCar->mIsTouchingGround) {
        tLastGrounded = Now();
    }
    if (IsUnderVehicle()) {
        tLastUnderVehicle = Now();
    }
    SetForward(pov, snap);
    BuildBasis(basis);
    bMatrix4 world_to_local;
    eInvertTransformationMatrix(&world_to_local, basis);

    bVector4 lag(0.0f, 0.0f, 0.0f, 0.0f);
    if (bAccelLag) {
        const bVector3 &avg = pAvgAccel->GetValue();
        lag = bVector4(avg.x, avg.y, avg.z, 0.0f);
        eMulVector(&lag, &world_to_local, &lag);
    }

    bVector3 offset;
    bAngle angle;
    // TODO(port): burnout / drag tweak flags (0x904FFA, g_tweakIsBurnout) swap
    // in the fixed burnout offset; never set natively.
    const bool burnout = false;
    if (!burnout) {
        offset = bVector3(pov->Lag, pov->LatOffset, pov->Height);
        CameraSpeedHug(&offset);
        angle = pov->Angle;
    } else {
        offset = bVector3(-10.0f, 0.0f, 5.0f); // 0x8EE120
        angle = 0xbbb;                          // 0x9111E4
    }
    float t = bSin(angle) / bCos(angle);
    float look_x, look_z;
    if (outside) {
        look_x = 0.0f;
        look_z = t * offset.x + offset.z;
    } else {
        look_x = offset.x + 1.0f;
        look_z = offset.z - t;
    }

    bVector3 eye(lag.x * tweak->EyeScale.x, lag.y * tweak->EyeScale.y, lag.z * tweak->EyeScale.z);
    eye = ClampV(eye, tweak->EyeMin, tweak->EyeMax);
    eye += offset;
    SetValDesired(pEye, eye);

    bVector3 look(lag.x * tweak->LookScale.x, lag.y * tweak->LookScale.y, lag.z * tweak->LookScale.z);
    look = ClampV(look, tweak->LookMin, tweak->LookMax);
    look.x += look_x;
    look.z += look_z;
    SetValDesired(pLook, look);

    bVector3 up(0.0f, 0.0f, 1.0f);
    if (pov->AllowTilting != 0) {
        const bVector3 &avg = pAvgAccel->GetValue();
        up = bVector3(tweak->UpScale.x * avg.x, tweak->UpScale.y * avg.y, tweak->UpScale.z * avg.z + 1.0f);
        up = ClampV(up, tweak->UpMin, tweak->UpMax);
    }
    if (pCar->mVelMag > 5.0f) {
        up += AvoidablesPush(pCar->mGeomPos, pCar->mVelocity);
    }
    bNormalize(&up, &up);
    SetValDesired(pUp, up);

    unsigned int fov = static_cast<unsigned short>(pov->Fov);
    if (outside && pCar->mIsNosEngaged && pCar->mVelMag > 0.0f) {
        fov += kNosFovBump;
    }
    pFov->SetValDesired(static_cast<float>(fov & 0xffff));

    if (bSnapNext || !outside) {
        Snap(pUp);
        pFov->Snap();
        Snap(pEye);
        Snap(pLook);
    }
    if (bSnapNext) {
        FlushAccumulationBuffer();
    }
}

bool CameraMover::WallClearance(bVector3 *target, bVector3 *eye, bVector4 *push) {
    *push = bVector4(0.0f, 0.0f, 0.0f, 0.0f);
    float fov_scale = ClampF(static_cast<float>(GetCamera()->GetFov()) * 7.847377e-05f, 1.0f, 1.5f);
    bVector3 d = *target - *eye;
    float len = bLength(&d);
    if (len < 0.001f) {
        return false;
    }
    bVector3 n(d.x / len, d.y / len, d.z / len);
    bVector3 zaxis(0.0f, 0.0f, 1.0f);
    bVector3 side;
    bCross(&side, &zaxis, &n);
    float w = fov_scale * len * 0.44f;
    bVector3 rays[2];
    bScaleAdd(&rays[0], &d, &side, w);
    bScaleAdd(&rays[1], &d, &side, -w);
    float best = 0.0f;
    bool hit = false;
    for (int i = 0; i < 2; i++) {
        bVector3 dir;
        bNormalize(&dir, &rays[i]);
        bVector3 start;
        bScaleAdd(&start, eye, &dir, -0.9f);
        UMath::Vector4 seg[2];
        seg[0] = UMath::Vector4Make(-target->y, target->z, target->x, 1.0f);
        seg[1] = UMath::Vector4Make(-start.y, start.z, start.x, 1.0f);
        WCollisionMgr::WorldCollisionInfo info;
        hit = WCollisionMgr(0, 3).CheckHitWorld(seg, info, 2) != 0;
        if (hit) {
            bVector3 pt(info.fCollidePt.z, -info.fCollidePt.x, info.fCollidePt.y);
            bVector3 nrm(info.fNormal.z, -info.fNormal.x, info.fNormal.y);
            float facing = 1.0f - std::fabs(bDot(&nrm, &n));
            float amount = (facing * 0.6f + 1.0f) * (bDistBetween(target, &start) - bDistBetween(target, &pt));
            if (amount > best) {
                best = amount;
            }
        }
    }
    float f = (best + fAccumulatedClearance) * 0.5f;
    fAccumulatedClearance += best - f;
    if (f > 0.0f) {
        *push = bVector4(n.x * f, n.y * f, n.z * f, 0.0f);
    }
    return hit;
}

// sub_468AF0: re-aim a world-to-camera matrix from a new eye so the target
// keeps its screen offset.
static void ReaimCamera(bMatrix4 *world_to_camera, bVector3 *eye, bVector3 *target, const float screen[2]) {
    bVector3 up(-world_to_camera->v0.y, -world_to_camera->v1.y, -world_to_camera->v2.y);
    bMatrix4 look, inv;
    eCreateLookAtMatrix(&look, *eye, *target, up);
    eInvertTransformationMatrix(&inv, &look);
    bVector4 t(target->x, target->y, target->z, 1.0f);
    bVector4 c;
    eMulVector(&c, &look, &t);
    c.x -= c.z * screen[0];
    c.y -= c.z * screen[1];
    bVector4 w;
    eMulVector(&w, &inv, &c);
    bVector3 center(w.x, w.y, w.z);
    eCreateLookAtMatrix(world_to_camera, *eye, center, up);
}

bool CameraMover::MakeSpace(bMatrix4 *world_to_camera, bVector3 *target) {
    bMatrix4 cam;
    eInvertTransformationMatrix(&cam, world_to_camera);
    bVector3 eye(cam.v3.x, cam.v3.y, cam.v3.z);
    bVector3 half_eye(eye.x * 0.5f, eye.y * 0.5f, eye.z * 0.5f);
    bVector3 mid;
    bScaleAdd(&mid, &half_eye, target, 0.5f);
    float radius = (bDistBetween(target, &eye) + 1.0f) * 0.5f;
    if (radius > 24.9f) {
        radius = 24.9f;
    }
    UMath::Vector3 sim_mid = UMath::Vector3Make(-mid.y, mid.z, mid.x);
    mCollider->Refresh(sim_mid, radius, true);
    bVector4 push;
    bool hit = WallClearance(target, &eye, &push);
    eye.x += push.x;
    eye.y += push.y;
    eye.z += push.z;
    bool tunnel = AmIinATunnel(GetView(), 1) != 0;
    UMath::Vector3 probe = UMath::Vector3Make(-eye.y, eye.z, eye.x);
    if (!tunnel && eye.z < target->z + 0.5f) {
        probe.y = target->z + 0.5f;
    }
    WWorldPos wpos(0.025f);
    wpos.FindClosestFace(mCollider, probe, true);
    if (wpos.OnValidFace()) {
        UMath::Vector3 normal;
        wpos.UNormal(&normal);
        const UMath::Vector4 &p0 = wpos.FacePoint(0);
        UMath::Vector3 on_plane = UMath::Vector3Make(p0.x, p0.y, p0.z);
        float ground = WWorldMath::GetPlaneY(normal, on_plane, probe);
        if (eye.z < ground + 0.25f) {
            eye.z = ground + 0.25f;
        }
        if (tunnel && eye.z > ground + 4.5f) {
            eye.z = ground + 4.5f;
        }
    }
    bVector4 t4(target->x, target->y, target->z, 1.0f);
    bVector4 c;
    eMulVector(&c, world_to_camera, &t4);
    if (c.z >= 0.0f) {
        float screen[2] = {c.x * (1.0f / c.z), c.y * (1.0f / c.z)};
        ReaimCamera(world_to_camera, &eye, target, screen);
        return hit;
    }
    return false;
}

void CubicCameraMover::TerrainVelocityNoise(bMatrix4 *world_to_camera, CameraAnchor *car, float speed_scale, float terrain_scale) {
    if (car == nullptr) {
        return;
    }
    bVector4 n;
    TableValue(&n, kTerrainNoise, 5, 0.0f, 0.05f, car->mVelMag,
               [](bVector4 *o, const bVector4 &a, const bVector4 &b, float t) {
                   *o = bVector4(t * a.x + (1.0f - t) * b.x, t * a.y + (1.0f - t) * b.y, t * a.z + (1.0f - t) * b.z, t * a.w + (1.0f - t) * b.w);
               });
    float surf0 = car->mSurface.CAMERA_NOISE(0);
    float surf1 = car->mSurface.CAMERA_NOISE(1);
    float amp1 = n.x * speed_scale;
    float freq1 = n.z;
    if (car->mIsDragRace) {
        amp1 *= 1.5f;
    }
    if (car->mVelMag > 5.0f) {
        const bVector3 &heading = *reinterpret_cast<bVector3 *>(&car->mGeomRot.v0);
        float a = ClampF(bDot(&heading, &car->mAccel), 0.0f, 20.0f);
        amp1 += 0.15f * a * 0.05f;
        freq1 += (0.5f - n.z) * a * 0.05f;
    }
    if (car->mIsOverRev) {
        amp1 += 0.15f;
        freq1 *= 0.7f;
    }
    if (car->mIsNosEngaged && car->mVelMag > 5.0f) {
        amp1 = 0.3f;
        freq1 = 2.0f;
    }
    if (car->mIsBrakeEngaged && car->mVelMag > 5.0f) {
        amp1 = 0.3f;
        freq1 = 5.0f;
    }
    float amp2 = n.y * surf0 * terrain_scale;
    float freq2 = n.w * surf1;
    if (!OutsidePOV()) {
        amp1 *= 0.25f;
        amp2 *= 0.25f;
    }
    Camera *camera = GetCamera();
    camera->SetNoiseFrequency1(kTerrainNoiseFreq1[0] * freq1, kTerrainNoiseFreq1[1] * freq1, kTerrainNoiseFreq1[2] * freq1,
                               kTerrainNoiseFreq1[3] * freq1);
    camera->SetNoiseAmplitude1(kTerrainNoiseAmp1[0] * amp1, kTerrainNoiseAmp1[1] * amp1, kTerrainNoiseAmp1[2] * amp1, kTerrainNoiseAmp1[3] * amp1);
    camera->SetNoiseFrequency2(kTerrainNoiseFreq2[0] * freq2, kTerrainNoiseFreq2[1] * freq2, kTerrainNoiseFreq2[2] * freq2,
                               kTerrainNoiseFreq2[3] * freq2);
    camera->SetNoiseAmplitude2(kTerrainNoiseAmp2[0] * amp2, kTerrainNoiseAmp2[1] * amp2, kTerrainNoiseAmp2[2] * amp2, kTerrainNoiseAmp2[3] * amp2);
    camera->ApplyNoise(world_to_camera, WorldTimer.GetSeconds(), 1.0f);
}

void CubicCameraMover::Update(float dT) {
    if (TheGameFlowManager.IsPaused() && !bFirstTime) {
        return;
    }
    bFirstTime = false;
    if (fIceBlend > 0.0f) {
        fIceBlend -= dT;
    }
    nPovTypeUsed = nPovType;
    POV *pov = pCar->GetPov(nPovType);
    float damping = pCar->mCollisionDamping;
    float drift = pCar->mDrift;
    float speed_fraction = Clamp01(pCar->mVelMag * 0.014084507f); // 0x895078: 1/71 m/s

    CubicCameraTweak tweak;
    GetCubicTweak(nPovTypeUsed, speed_fraction, &tweak);
    // TODO(port): drift/drag race and burnout tweak flags (0x904FF9/0x904FFA,
    // g_tweakIsDriftRace/g_tweakIsBurnout) zero the eye lag here.

    bVector3 accel(0.0f, 0.0f, 0.0f);
    if (fIceBlend <= 0.0f) {
        CameraAccelCurve(&accel);
    }
    pAvgAccel->Record(accel);
    float lateral = std::max(std::fabs(accel.x), std::fabs(accel.y));
    if (lateral >= kCameraImpactThreshold && damping > 0.0f && vCameraImpact.x < damping) {
        vCameraImpact.x = damping;
        vCameraImpactTimer.x = 1.0f;
    }
    if (accel.z >= kCameraImpactThreshold) {
        float f = Clamp01((accel.z - kCameraImpactThreshold) / (30.0f - kCameraImpactThreshold));
        if (vCameraImpact.y < f) {
            vCameraImpact.y = f;
            vCameraImpactTimer.y = 1.0f;
        }
    }

    SetDurations(*pUp, tweak.UpDuration, tweak.UpDuration, tweak.UpDuration);
    pFov->SetDuration(tweak.FovDuration);
    SetDurations(*pLook, tweak.LookDuration, tweak.LookDuration, tweak.LookDuration);
    SetDurations(*pForward, tweak.ForwardDuration.x + drift + damping, tweak.ForwardDuration.y + drift + damping,
                 tweak.ForwardDuration.z + drift + damping);
    SetDurations(*pEye, tweak.EyeDuration, tweak.EyeDuration, tweak.EyeDuration);
    pUp->Update(dT, 0.0f, 0.0f);
    pFov->Update(dT, 0.0f, 0.0f);
    pEye->Update(dT, 0.0f, 0.0f);
    pLook->Update(dT, 0.0f, 0.0f);
    pForward->Update(dT, 0.0f, 0.0f);

    bool outside = IsOutsidePov(pov->Type);
    Camera *camera = GetCamera();
    float target_distance = bDistBetween(camera->GetPosition(), &pCar->mGeomPos);
    if (!Camera::StopUpdating) {
        camera->SetTargetDistance(target_distance);
        camera->SetFocalDistance(bPerfectFocus ? 0.0f : 15.0f);
        camera->SetDepthOfField(bPerfectFocus ? 0.0f : 65.0f);
    }

    bMatrix4 basis;
    SetDesired(&basis, pov, &tweak, bSnapNext);
    bSnapNext = false;

    float side = bLookBack ? -1.0f : 1.0f;
    bVector3 up(pUp->x.Val, pUp->y.Val, 1.0f);
    if (!outside) {
        up = *reinterpret_cast<bVector3 *>(&pCar->mGeomRot.v2);
    }
    bVector3 eye(side * pEye->x.Val, side * pEye->y.Val, pEye->z.Val);
    bVector3 look(side * pLook->x.Val, side * pLook->y.Val, pLook->z.Val);

    if (vCameraImpact.x > 0.0f && vCameraImpactTimer.x > 0.0f) {
        float f = TableFloat(kCameraImpactCurveH, 5, 0.0f, 1.0f, vCameraImpactTimer.x) * vCameraImpact.x * 0.25f;
        eye.x *= 1.0f - f;
        look.x *= 1.0f + f;
    }
    if (vCameraImpactTimer.x > 0.0f) {
        vCameraImpactTimer.x -= dT;
    }
    if (vCameraImpactTimer.x <= 0.0f) {
        vCameraImpact.x = 0.0f;
    }
    if (vCameraImpact.y > 0.0f && vCameraImpactTimer.y > 0.0f) {
        float f = TableFloat(kCameraImpactCurveV, 5, 0.0f, 1.0f, vCameraImpactTimer.y);
        eye.z *= 1.0f - f * vCameraImpact.y * 0.25f;
    }
    if (vCameraImpactTimer.y > 0.0f) {
        vCameraImpactTimer.y -= dT;
    }
    if (vCameraImpactTimer.y <= 0.0f) {
        vCameraImpact.y = 0.0f;
    }

    if (outside) {
        // Gear change: a short push back and forth on the eye.
        float since = static_cast<float>(Now() - tLastGearChange) * kTicksToSeconds;
        if (since < 1.5f) {
            float g = TableFloat(kCameraGearChangingCurve, 9, 0.0f, 1.5f, since);
            float s = Clamp01(pCar->mVelMag * 0.01f);
            eye.x += (1.0f - s) * std::fabs(eye.x) * 0.2f * g;
        } else if (pCar->mVelMag > 10.0f && pCar->mIsGearChanging) {
            tLastGearChange = Now();
        }
    }
    bool ducking = false;
    if (outside) {
        float f = ClampF(1.2f - static_cast<float>(Now() - tLastUnderVehicle) * kTicksToSeconds * 1.25f, 0.0f, 1.2f);
        if (f > 1.0f) {
            f = 2.0f - f;
        }
        eye.z -= 0.9f * f;
        ducking = f > 0.0f;
    }

    bMulMatrix(&eye, &basis, &eye);
    bMulMatrix(&look, &basis, &look);

    bAngle fov = static_cast<bAngle>(static_cast<int>(pFov->Val));
    bAngle half = fov >> 1;
    float tan_half = bSin(half) / bCos(half);
    if (!Camera::StopUpdating) {
        camera->SetFieldOfView(static_cast<unsigned short>(bATan(1.0f, tan_half) << 1));
    }

    bMatrix4 world_to_camera;
    eCreateLookAtMatrix(&world_to_camera, eye, look, up);
    ApplyCameraShake(GetViewID(), &world_to_camera);
    HandheldNoise(&world_to_camera, kHandheldNoiseScale[nPovTypeUsed], true);
    if (AmIinATunnel(GetView(), 1) == 0) {
        float s = Clamp01(pCar->mVelMag * (1.0f / 30.0f));
        ChopperNoise(&world_to_camera, (1.0f - s * 0.8f) * kChopperNoiseScale[nPovTypeUsed], true);
    }
    TerrainVelocityNoise(&world_to_camera, pCar, kTerrainSpeedScale[nPovTypeUsed], kTerrainScale[nPovTypeUsed]);
    if (outside) {
        MakeSpace(&world_to_camera, &pCar->mGeomPos);
        if (!ducking) {
            // TODO(port): 0x47C6F0 lifts the eye over nearby avoidable bodies;
            // not ported.
        }
    }
    camera->SetCameraMatrix(world_to_camera, dT);
}

// ---------------------------------------------------------------------------
// CameraAI actions
// ---------------------------------------------------------------------------

namespace CameraAI {
Director *Director::sDirectors[2];
int Director::sNumDirectors;

} // namespace CameraAI

// ITrafficCenter (vtable 0x895128 at +0x24): AITrafficManager spawns traffic
// around the followed car.
class CDActionDrive : public CameraAI::Action, public Sim::Collision::IListener, public ITrafficCenter {
  public:
    static CameraAI::Action *Construct(CameraAI::Director *director);
    CDActionDrive(CameraAI::Director *director, IPlayer *player);
    ~CDActionDrive() override;

    void Update(float dT) override;
    void Reset() override;
    const Attrib::StringKey &GetName() const override {
        static const Attrib::StringKey name("CDActionDrive");
        return name;
    }
    CameraMover *GetMover() override {
        return mMover;
    }
    void SetSpecial(float f) override {
        if (f > 0.0f) {
            sCinematicSeconds = f;
            mCinematicMomementTimerInc = true;
        }
    }
    void OnCollision(const Sim::Collision::Info &cinfo) override;
    bool GetTrafficBasis(UMath::Matrix4 &matrix, UMath::Vector3 &velocity) override;

  private:
    void AquireCar();
    void FeedAnchor(float dT);

    static float sCinematicSeconds; // 0x8ED454

    CubicCameraMover *mMover;
    CameraAnchor *mAnchor;
    WorldConn::Reference mTarget;
    IPlayer *mPlayer;
    IVehicle *mVehicle;
    float mGameBreakerScale;
    EVIEW_ID mViewID;
    float mDampCollisionTime;
    float mGroundCollisionTime;
    float mObjectCollisionTime;
    float mMaxCollisionTime;
    float mPulseTimer;
    float mCinematicMomementTimer;
    bool mCinematicMomementTimerInc;
    int mGear;
};

float CDActionDrive::sCinematicSeconds = 2.0f;

namespace {
int sSavedPovType = -1; // 0x8EE4B4: POV to restore after the pull-back button
} // namespace

IPlayer *CameraAI::FindPlayerForView(int view_id) {
    const IPlayer::List &players = IPlayer::GetList(PLAYER_ALL);
    for (IPlayer::List::const_iterator it = players.begin(); it != players.end(); ++it) {
        if ((*it)->GetRenderPort() == view_id) {
            return *it;
        }
    }
    return nullptr;
}

CameraAI::Action *CDActionDrive::Construct(CameraAI::Director *director) {
    IPlayer *player = CameraAI::FindPlayerForView(director->GetViewID());
    if (player == nullptr || player->GetSettings() == nullptr) {
        return nullptr;
    }
    ISimable *simable = player->GetSimable();
    if (simable == nullptr || simable->GetWorldID() == 0) {
        return nullptr;
    }
    return new CDActionDrive(director, player);
}

CDActionDrive::CDActionDrive(CameraAI::Director *director, IPlayer *player)
    : mMover(nullptr), mAnchor(nullptr), mTarget(0), mPlayer(player), mVehicle(nullptr), mGameBreakerScale(0.0f),
      mViewID(director->GetViewID()), mDampCollisionTime(0.0f), mGroundCollisionTime(0.0f), mObjectCollisionTime(0.0f),
      mMaxCollisionTime(2.0f), mPulseTimer(0.0f), mCinematicMomementTimer(0.0f), mCinematicMomementTimerInc(false), mGear(0) {
    gCinematicMomementCamera = false;
    gGameBreakerCamera = false;
    if (director->IsCinematicMomement()) {
        gCinematicMomementCamera = true;
        mCinematicMomementTimer = 1.0f;
        sCinematicSeconds = 2.0f;
        mPulseTimer = 0.3f;
    }
    // TODO(port): MJumpCut handler ("CameraMessagePort", 0x473220) and the
    // Sim::Attachments of the player/vehicle are not ported.
    bool blend_from_ice = false;
    CameraMover *previous = director->GetMover();
    if (previous != nullptr && previous->GetType() == CM_ICE) {
        blend_from_ice = TheICEManager.IsSmoothExit(); // byte_8EE392: the ICE key's smooth exit
    }
    mAnchor = new CameraAnchor(0);
    if (mCinematicMomementTimer > 0.0f) {
        mAnchor->mZoom = 1.0f - mCinematicMomementTimer * 0.4f;
    }
    AquireCar();
    if (mTarget.IsValid()) {
        FeedAnchor(0.0f);
    }
    mMover = new CubicCameraMover(mViewID, mAnchor, GetPOVTypeFromPlayerCamera(player->GetSettings()->CurCam), blend_from_ice, false, false,
                                  true);
    // TODO(port): the rear-view mirror mover (RearViewMirrorCameraMover on
    // view 3, 0x476690) is not ported.
}

CDActionDrive::~CDActionDrive() {
    Sim::Collision::RemoveListener(this);
    delete mMover;
    delete mAnchor;
}

void CDActionDrive::Reset() {
    mGameBreakerScale = 0.0f;
    mDampCollisionTime = 0.0f;
    mGroundCollisionTime = 0.0f;
    mObjectCollisionTime = 0.0f;
    mPulseTimer = 0.0f;
    mCinematicMomementTimer = 0.0f;
    mCinematicMomementTimerInc = false;
    mGear = 0;
}

void CDActionDrive::OnCollision(const Sim::Collision::Info &cinfo) {
    float speed = UMath::Length(cinfo.closingVel);
    switch (cinfo.type) {
        case Sim::Collision::Info::OBJECT:
            if (speed > 20.0f) {
                float f = std::min((speed - 20.0f) * 0.01f, 1.0f) * mMaxCollisionTime;
                mObjectCollisionTime = std::max(f, mObjectCollisionTime);
            }
            break;
        case Sim::Collision::Info::WORLD:
            if (speed > 5.0f) {
                float f = std::min((speed - 5.0f) * 0.1f, 1.0f) * mMaxCollisionTime;
                mDampCollisionTime = std::max(f, mDampCollisionTime);
            }
            break;
        case Sim::Collision::Info::GROUND:
            if (speed > 3.0f) {
                float f = std::min((speed - 3.0f) * 0.05f, 1.0f) * mMaxCollisionTime;
                mGroundCollisionTime = std::max(f, mGroundCollisionTime);
            }
            break;
        default:
            break;
    }
}

// The decomp header declares the base as a plain virtual; every implementer
// overrides it (the PC vtables carry only the overrides).
bool ITrafficCenter::GetTrafficBasis(UMath::Matrix4 &, UMath::Vector3 &) {
    return false;
}

// 0x4731D0
bool CDActionDrive::GetTrafficBasis(UMath::Matrix4 &matrix, UMath::Vector3 &velocity) {
    if (mVehicle == nullptr) {
        return false;
    }
    IBody *body;
    if (!mVehicle->QueryInterface(&body) || body == nullptr) {
        return false;
    }
    body->GetTransform(matrix);
    body->GetLinearVelocity(velocity);
    return true;
}

void CDActionDrive::AquireCar() {
    if (mPlayer == nullptr) {
        return;
    }
    ISimable *simable = mPlayer->GetSimable();
    if (mVehicle != nullptr && (simable == nullptr || mVehicle->GetSimable() != simable)) {
        Sim::Collision::RemoveListener(this);
        mVehicle = nullptr;
    }
    if (mVehicle != nullptr || simable == nullptr) {
        return;
    }
    mTarget.Set(simable->GetWorldID());
    if (!mTarget.IsValid()) {
        return;
    }
    IVehicle *vehicle = nullptr;
    if (!simable->QueryInterface(&vehicle) || vehicle == nullptr) {
        return;
    }
    mVehicle = vehicle;
    Sim::Collision::AddListener(this, mVehicle, "CDActionDrive");
    mAnchor->SetModel(static_cast<int>(bStringHash(mVehicle->GetVehicleAttributes().MODEL().GetString())));
    mAnchor->mWorldID = mTarget.GetWorldID();
    IRigidBody *body = simable->GetRigidBody();
    if (body != nullptr) {
        UMath::Vector3 dim;
        body->GetDimension(dim);
        mAnchor->mDimension = bVector3(dim.z, dim.x, dim.y);
    }
    ITransmission *transmission = nullptr;
    if (mVehicle->QueryInterface(&transmission) && transmission != nullptr) {
        mAnchor->mTopSpeed = transmission->GetMaxSpeedometer();
        mGear = transmission->GetGear();
    }
}

// Copy the followed body into the anchor; the anchor point is the rigid
// body raised to its centre of gravity (0x484E46 / 0x472D2C).
void CDActionDrive::FeedAnchor(float dT) {
    bMatrix4 m = *mTarget.GetMatrix();
    ICollisionBody *cbody = nullptr;
    if (mVehicle != nullptr && mVehicle->QueryInterface(&cbody) && cbody != nullptr) {
        IRigidBody *body = mVehicle->GetSimable()->GetRigidBody();
        if (body != nullptr) {
            const UMath::Vector3 &cog = cbody->GetCenterOfGravity();
            UMath::Vector3 p = UMath::Vector3Make(0.0f, cog.y, cog.z);
            body->ConvertLocalToWorld(p, false);
            const UMath::Vector3 &pos = body->GetPosition();
            p.x += pos.x;
            p.y += pos.y;
            p.z += pos.z;
            m.v3.x = p.z;
            m.v3.y = -p.x;
            m.v3.z = p.y;
        }
    }
    static const bVector3 kZero(0.0f, 0.0f, 0.0f);
    const bVector3 *vel = mTarget.GetVelocity() != nullptr ? mTarget.GetVelocity() : &kZero;
    const bVector3 *acc = mTarget.GetAcceleration() != nullptr ? mTarget.GetAcceleration() : &kZero;
    mAnchor->Update(dT, m, *vel, *acc);
}

void CDActionDrive::Update(float dT) {
    gCinematicMomementCamera = false;
    gGameBreakerCamera = false;
    mDampCollisionTime = std::max(mDampCollisionTime - dT, 0.0f);
    mGroundCollisionTime = std::max(mGroundCollisionTime - dT, 0.0f);
    mObjectCollisionTime = std::max(mObjectCollisionTime - dT, 0.0f);
    if (mPlayer == nullptr) {
        if (mVehicle != nullptr) {
            Sim::Collision::RemoveListener(this);
            mVehicle = nullptr;
        }
        return;
    }
    AquireCar();
    mAnchor->mIsVehicleDestroyed = mVehicle != nullptr && mVehicle->IsDestroyed();

    // Once this racer's camera is detached (race over) the view holds still.
    if (GRaceStatus::Exists()) {
        GRaceStatus &status = GRaceStatus::Get();
        GRacerInfo *info = status.GetRacerInfo(mPlayer->GetSimable());
        if (mMover->OutsidePOV() && status.GetIsTimeLimited() && status.GetRaceTimeRemaining() <= 0.0f && info != nullptr &&
            !info->IsFinishedRacing()) {
            return;
        }
        if (info != nullptr && info->GetCameraDetached()) {
            return;
        }
    }

    // TODO(port): roadblock proximity (mIsCloseToRoadBlock, 0x4728A0) needs
    // the AI vehicle road-block query; left false.
    mAnchor->mIsCloseToRoadBlock = false;

    IInput *input = nullptr;
    if (mVehicle != nullptr && mVehicle->QueryInterface(&input) && input != nullptr) {
        mMover->SetLookBack(false);
        if (input->IsLookBackButtonPressed() && !mVehicle->IsStaging()) {
            mMover->SetLookBack(true);
        }
        const InputControls &controls = input->GetControls();
        mAnchor->mIsBrakeEngaged = controls.fHandBrake > 0.0f || controls.fBrake > 0.0f;
    }

    // TODO(port): visual treatment pulse and cinematic-moment zoom
    // (IVisualTreatment::TriggerPulse, 0x472B30).
    if (mCinematicMomementTimerInc) {
        mCinematicMomementTimer = std::min(mCinematicMomementTimer + (dT / sCinematicSeconds) * 4.0f, 1.0f);
    } else {
        mCinematicMomementTimer = mCinematicMomementTimer > 0.0f ? mCinematicMomementTimer - dT / sCinematicSeconds : 0.0f;
    }
    if (mCinematicMomementTimer > 0.001f) {
        gCinematicMomementCamera = true;
    }
    if (mPlayer->InGameBreaker()) {
        mGameBreakerScale = std::min(mGameBreakerScale + dT * 4.0f, 1.0f);
    } else {
        mGameBreakerScale = std::max(mGameBreakerScale - dT, 0.0f);
    }
    if (mGameBreakerScale > 0.001f) {
        gGameBreakerCamera = true;
    }

    PlayerSettings *settings = mPlayer->GetSettings();
    if (settings != nullptr) {
        int pov = GetPOVTypeFromPlayerCamera(settings->CurCam);
        if (input != nullptr && input->IsPullBackButtonPressed()) {
            sSavedPovType = pov;
            mMover->SetPovType(POV_PURSUIT);
        } else if (input != nullptr && sSavedPovType >= 0) {
            mMover->SetPovType(sSavedPovType);
            sSavedPovType = -1;
        } else {
            mMover->SetPovType(pov);
        }
    }

    if (!mTarget.IsValid()) {
        return;
    }
    if (mVehicle != nullptr) {
        mAnchor->mIsDragRace = mVehicle->GetDriverStyle() == 1;
    }
    mAnchor->mSurface.Change(static_cast<const Attrib::Collection *>(nullptr));
    mAnchor->mIsTouchingGround = false;
    ISuspension *suspension = nullptr;
    if (mVehicle != nullptr && mVehicle->QueryInterface(&suspension) && suspension != nullptr) {
        int on_ground = suspension->GetNumWheelsOnGround();
        if (on_ground == static_cast<int>(suspension->GetNumWheels())) {
            mAnchor->mSurface.Change(mVehicle->GetSimable()->GetWPos().GetSurface());
        }
        mAnchor->mIsTouchingGround = on_ground > 0;
    }
    mAnchor->mIsNosEngaged = false;
    mAnchor->mIsOverRev = false;
    IEngine *engine = nullptr;
    if (mVehicle != nullptr && mVehicle->QueryInterface(&engine) && engine != nullptr) {
        mAnchor->mIsNosEngaged = engine->IsNOSEngaged();
        mAnchor->mIsOverRev = engine->GetRedline() - 500.0f < engine->GetRPM();
    }
    float drift = 0.0f;
    if (mAnchor->mIsTouchingGround && mVehicle != nullptr) {
        float slip = std::fabs(mVehicle->GetSlipAngle());
        float speed_ramp = UMath::Ramp(mVehicle->GetSpeed(), 10.0f, 20.0f);
        drift = UMath::Ramp(slip * 360.0f, 0.0f, 90.0f) * speed_ramp;
    }
    mAnchor->mDrift = drift;
    mAnchor->mIsGearChanging = false;
    ITransmission *transmission = nullptr;
    if (mVehicle != nullptr && mVehicle->QueryInterface(&transmission) && transmission != nullptr) {
        int gear = transmission->GetGear();
        mAnchor->mIsGearChanging = gear != mGear;
        mGear = gear;
    }
    mAnchor->mCollisionDamping = mDampCollisionTime / mMaxCollisionTime;
    mAnchor->mGroundCollision = mGroundCollisionTime / mMaxCollisionTime;
    mAnchor->mObjectCollision = mObjectCollisionTime / mMaxCollisionTime;
    mAnchor->mZoom = 1.0f - std::max(mCinematicMomementTimer, mGameBreakerScale) * 0.4f;
    FeedAnchor(dT);
    if (mViewID == EVIEW_PLAYER1 || mViewID == EVIEW_PLAYER2) {
        MaybeCameraShake(mViewID - 1, &mAnchor->mAccel);
        // TODO(port): explosion shakes (IExplosion list, 0x472F70).
    }
}

// ---------------------------------------------------------------------------
// CameraAI::Director
// ---------------------------------------------------------------------------

namespace CameraAI {

namespace {
struct ActionEntry {
    const char *name;
    ActionConstructor construct;
};
// The UTL::COM::Factory<Director*, Action, UCrc32> registry (0x9111FC). Only
// CDActionDrive and CDActionIce are ported; asking for any other action keeps
// the current one.
const ActionEntry kActions[] = {
    {"CDActionDrive", CDActionDrive::Construct},
    {"CDActionIce", ConstructCDActionIce},
};
} // namespace

ActionConstructor FindActionConstructor(unsigned int hash32) {
    for (const ActionEntry &e : kActions) {
        if (Attrib::StringHash32(e.name) == hash32) {
            return e.construct;
        }
    }
    return nullptr;
}

Director::Director(EVIEW_ID view_id)
    : mViewID(view_id), mDesiredMode("CDActionDrive"), mAction(nullptr), mPursuitStartTime(0.0f), mJumpTime(0.0f),
      mIsCinematicMomement(false), mCinematicSlowdownSeconds(0.0f) {
    if (sNumDirectors < 2) {
        sDirectors[sNumDirectors++] = this;
    }
}

Director::~Director() {
    delete mAction;
    mAction = nullptr;
    for (int i = 0; i < sNumDirectors; i++) {
        if (sDirectors[i] == this) {
            sDirectors[i] = sDirectors[--sNumDirectors];
            break;
        }
    }
}

// The tail of SelectAction (0x479C32) and SetAction (0x4797F0): take the
// action's follow-up, then build the desired action if it isn't running.
// Only SelectAction clears the cinematic-moment state of the new action.
void Director::SwitchToDesired(bool from_select) {
    if (mAction != nullptr) {
        Attrib::StringKey next = mAction->GetNext();
        if (!next.IsEmpty()) {
            mDesiredMode = next;
        }
    }
    if (mAction != nullptr && mAction->GetName().GetHash64() == mDesiredMode.GetHash64()) {
        return;
    }
    if (mDesiredMode.IsEmpty()) {
        return;
    }
    ActionConstructor construct = FindActionConstructor(mDesiredMode.GetHash32());
    if (construct == nullptr) {
        return;
    }
    Action *action = construct(this);
    if (action == nullptr) {
        return;
    }
    if (from_select) {
        mIsCinematicMomement = false;
        mCinematicSlowdownSeconds = 0.0f;
    }
    delete mAction;
    mAction = action;
    SetNewSndCamAction(mDesiredMode, mViewID);
}

void Director::SetAction(const Attrib::StringKey &desired) {
    mDesiredMode = desired;
    SwitchToDesired(false);
}

void Director::TotaledStart() {
    mJumpTime = 2.0f;
    SetAction(Attrib::StringKey("CDActionTrackCar"));
}

void Director::SelectAction() {
    if (TheICEManager.GetState() <= 0) { // the ICE editor drives the camera itself
        if (mJumpTime < 0.0f) {
            mJumpTime = 0.0f;
            mDesiredMode = Attrib::StringKey("CDActionDrive");
            mIsCinematicMomement = true;
        }
        if (mPursuitStartTime < 0.0f) {
            mPursuitStartTime = 0.0f;
            mDesiredMode = Attrib::StringKey("CDActionDrive");
            mIsCinematicMomement = true;
        }
        if (mPursuitStartTime > 0.0f && mPursuitStartTime < 2.0f) {
            mDesiredMode = Attrib::StringKey("CDActionTrackCop");
        }
        // TODO(port): the game-breaker track-car cut (CDActionTrackCar +
        // MGamePlayMoment, 0x4799F0) is not ported.

        // An NIS scene that controls the camera, or a generic ICE camera
        // (ECinematicMoment), plays through CDActionIce.
        bool ice = false;
        INIS *nis = INIS::Get();
        ICEScene *scene = nis != nullptr && nis->IsPlaying() ? nis->GetScene() : nullptr;
        if (scene != nullptr) {
            ice = scene->IsControllingCamera();
            mIsCinematicMomement = nis->IsWorldMomement();
        }
        if (!ice) {
            if (TheICEManager.GetPlayGenericGroupHash() != bStringHash("")) {
                ice = true;
            } else if (mAction != nullptr && mAction->GetName().GetHash64() == Attrib::StringHash64("CDActionIce")) {
                // The ICE camera is done: back to driving.
                TheICEManager.SetUseRealTime(false);
                mDesiredMode = Attrib::StringKey("CDActionDrive");
                if (INIS::Get() != nullptr) {
                    INIS::Get()->FireEventTag("ICEEnd");
                }
                MICECameraFinished().Post(UCrc32(UCRC32_Gameplay));
            }
        }
        if (ice) {
            mDesiredMode = Attrib::StringKey("CDActionIce");
            mJumpTime = 0.0f;
            mPursuitStartTime = 0.0f;
        }
    }
    SwitchToDesired(true);
}

void Director::Update(float dT) {
    if (!TheGameFlowManager.IsPaused() && mJumpTime > 0.0f) {
        mJumpTime -= dT;
    }
    if (!FEManager::ShouldPauseSimulation(true) && mPursuitStartTime > 0.0f) {
        mPursuitStartTime -= dT;
    }
    SelectAction();
    if (mAction != nullptr && mAction->GetName().GetHash64() == Attrib::StringHash64("CDActionDrive")) {
        mAction->SetSpecial(mCinematicSlowdownSeconds);
    }
    if (mAction != nullptr) {
        mAction->Update(dT);
    }
}

void Update(float dT) {
    for (int view = EVIEW_PLAYER1; view <= EVIEW_PLAYER2; view++) {
        IPlayer *player = FindPlayerForView(view);
        Director *director = nullptr;
        for (int i = 0; i < Director::sNumDirectors; i++) {
            if (Director::sDirectors[i]->GetViewID() == view) {
                director = Director::sDirectors[i];
                break;
            }
        }
        if (director != nullptr) {
            if (player == nullptr) {
                delete director;
            }
        } else if (player != nullptr) {
            new Director(static_cast<EVIEW_ID>(view));
        }
    }
    for (int i = 0; i < Director::sNumDirectors; i++) {
        Director::sDirectors[i]->Update(dT);
    }
}

void Reset() {
    for (int i = 0; i < Director::sNumDirectors; i++) {
        Director *d = Director::sDirectors[i];
        d->ResetTimers();
        d->SetAction(Attrib::StringKey("CDActionDrive"));
        if (d->GetAction() != nullptr) {
            d->GetAction()->Reset();
        }
    }
}

void SetAction(EVIEW_ID viewID, const char *desiredMode) {
    for (int i = 0; i < Director::sNumDirectors; i++) {
        if (Director::sDirectors[i]->GetViewID() == viewID) {
            Director::sDirectors[i]->SetAction(Attrib::StringKey(desiredMode));
        }
    }
}

void StartCinematicSlowdown(EVIEW_ID viewID, float seconds) {
    for (int i = 0; i < Director::sNumDirectors; i++) {
        Director *d = Director::sDirectors[i];
        if (d->GetViewID() == viewID && d->GetAction() != nullptr &&
            d->GetAction()->GetName().GetHash64() == Attrib::StringHash64("CDActionDrive")) {
            d->SetCinematicSlowdown(seconds);
        }
    }
}

void MaybeDoTotaledCam(IPlayer *iplayer) {
    if (Sim::GetUserMode() != Sim::USER_SINGLE) {
        return;
    }
    for (int i = 0; i < Director::sNumDirectors; i++) {
        if (Director::sDirectors[i]->GetViewID() == iplayer->GetRenderPort()) {
            Director::sDirectors[i]->TotaledStart();
        }
    }
}

void MaybeDoPursuitCam(IVehicle *ivehicle) {
    // TODO(port): 0x471BF0 starts the CDActionTrackCop pursuit intro, which
    // isn't ported (the factory would refuse it anyway).
}

void AddAvoidable(IBody *body) {
    UTL::Std::list<IBody *, _type_CameraAIAvoidables> &list = CameraAIAvoidables();
    if (std::find(list.begin(), list.end(), body) == list.end()) {
        list.push_back(body);
    }
}

void RemoveAvoidable(IBody *body) {
    CameraAIAvoidables().remove(body);
}

} // namespace CameraAI
