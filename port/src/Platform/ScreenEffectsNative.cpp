// The PC screen effects of the 3D frame (speed.exe 1.3 ___eDisplayFrame,
// 0x6DE300) that sit between the cars and the light flares:
//
//   0x6DEFCE  the tunnel-exit glare quad: with SE_GLARE active (ScreenEffects
//             +0x30, DoTunnelBloom) in a race, the four corners in
//             SE_data[SE_GLARE].data[2..13] drawn by eViewPlatInterfaceImpl::
//             Render (0x6D1A40, no texture, identity matrix, colour
//             0xFFFFFFFF) under IDI_WORLDMIN_FX (effect 7: fixed function,
//             COLORWRITEENABLE 8, z test, no z writes). It writes alpha 1
//             into the frame, which over bright's downsample (IDI_FILTER_FX
//             p5: rgb times alpha) blooms.
//   0x6DF1CC  the motion blur, sub_6DBB20, with g_MotionBlurEnable (0x9017DC)
//             and 0x8F9218 (1): the camera's speed and the visual
//             treatment's radial blur (NOS, pursuit breaker, heat change)
//             zoom-blur the frame.

#include "Speed/PC/Src/PCGraphicsOptions.hpp"
#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#include "Speed/Indep/Src/World/Rain.hpp"
#include "Speed/Indep/Src/World/ScreenEffects.hpp"

#include "../Mac/MetalBridge.h"
#include "../Mac/MetalPrelitBridge.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// 0x982C74: IVisualTreatment +0x178 (RadialBlur), stored by sub_6D4EF0 when
// the treatment's constants are set (VisualTreatmentNative.cpp).
float gPortRadialBlur = 0.0f;

namespace {

float gMotionBlurBoost = 0.0f; // 0x982C6C

bool EnvOn(const char *name) {
    const char *e = std::getenv(name);
    return e != nullptr && *e != '\0' && *e != '0';
}

} // namespace

// ___eDisplayFrame 0x6DEFCE. NFSMW_NO_GLARE_QUAD=1 leaves the quad out.
void PortTunnelGlareFrame(eView *view) {
    static const bool off = EnvOn("NFSMW_NO_GLARE_QUAD");
    if (off || TheGameFlowManager.GetState() != GAMEFLOW_STATE_RACING) {
        return;
    }
    ScreenEffectDB *se = view->ScreenEffects;
    static const bool trace = EnvOn("NFSMW_TRACE_GLARE");
    if (trace) {
        static int f;
        if (f++ % 60 == 0) {
            std::fprintf(stderr, "[glare] in tunnel %d, glare active %u intensity %.2f\n", AmIinATunnel(view, 1),
                         se ? se->IsActive(SE_GLARE) : 0, se ? se->GetIntensity(SE_GLARE) : 0.0f);
        }
    }
    if (se == nullptr || !se->IsActive(SE_GLARE)) {
        return;
    }
    // Corners (data 2..4) (5..7) (11..13) (8..10), uv (0,0) (1,0) (1,1) (0,1).
    static const int kCorner[4] = {2, 5, 11, 8};
    static const float kUV[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    PortPrelitVertex quad[4];
    for (int i = 0; i < 4; i++) {
        quad[i].x = se->GetDATA(SE_GLARE, kCorner[i]);
        quad[i].y = se->GetDATA(SE_GLARE, kCorner[i] + 1);
        quad[i].z = se->GetDATA(SE_GLARE, kCorner[i] + 2);
        quad[i].colour = 0xFFFFFFFFu;
        quad[i].u = kUV[i][0];
        quad[i].v = kUV[i][1];
    }
    PortMetalPrelitAddQuads(nullptr, PORT_PRELIT_ALPHA_ONLY, 0, quad, 1);
    if (trace) {
        static int n;
        if (n++ % 30 == 0) {
            std::fprintf(stderr, "[glare] intensity %.2f quad (%.1f %.1f %.1f) (%.1f %.1f %.1f) (%.1f %.1f %.1f) (%.1f %.1f %.1f)\n",
                         se->GetIntensity(SE_GLARE), quad[0].x, quad[0].y, quad[0].z, quad[1].x, quad[1].y, quad[1].z, quad[2].x, quad[2].y,
                         quad[2].z, quad[3].x, quad[3].y, quad[3].z);
        }
    }
}

// sub_6DBB20. NFSMW_NO_MOTION_BLUR=1 leaves it out; NFSMW_DEBUG_RADIAL_BLUR=<v>
// forces the treatment's radial blur (0x982C74) to v, to see the boost
// without NOS or the pursuit breaker.
void PortMotionBlurFrame(eView *view) {
    static const bool off = EnvOn("NFSMW_NO_MOTION_BLUR");
    static const char *forced = std::getenv("NFSMW_DEBUG_RADIAL_BLUR");
    if (off || gPCGraphicsOptions.MotionBlurEnable == 0) {
        return;
    }
    Camera *camera = view->GetCamera();
    if (camera == nullptr) {
        return;
    }
    const float radial = forced != nullptr ? static_cast<float>(std::atof(forced)) : gPortRadialBlur;
    // The boost eases toward 0.2 x the radial blur (0 while the accumulation
    // buffer is flushed): +0.02 (0x8F9B20) / -0.01 (0x8F9B24) a frame until
    // within 1/255. TODO(port): the target is also 0 while the byte 0x8F9B28
    // (Ghidra: ____icemanager_isplayingsomething; 1 at start, set by the
    // ICE / NIS code) is 0; it's taken as 1 here.
    float target = QueryFlushAccumulationBuffer() == 0 ? radial * 0.2f : 0.0f;
    if (std::fabs(gMotionBlurBoost - target) > 1.0f / 255.0f) {
        if (gMotionBlurBoost < target) {
            gMotionBlurBoost += 0.02f;
        } else {
            gMotionBlurBoost -= 0.01f;
        }
    }
    // The camera's velocity (Camera +0x200, VelocityKey.Position) into
    // camera space (D3DXVec4Transform with w 0 by the camera matrix, +0).
    const bVector3 &v = *camera->GetVelocityPosition();
    const bMatrix4 &m = *camera->GetCameraMatrix();
    const float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    float c[4] = {v.x * m.v0.x + v.y * m.v1.x + v.z * m.v2.x, v.x * m.v0.y + v.y * m.v1.y + v.z * m.v2.y,
                  v.x * m.v0.z + v.y * m.v1.z + v.z * m.v2.z, v.x * m.v0.w + v.y * m.v1.w + v.z * m.v2.w};
    float o[4] = {0, 0, 0, 0};
    if (speed > 1e-5f) { // 0x8910CC
        // bNormalize (bVector4), then the rect offsets: x, y shifted by +-z.
        float len = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2] + c[3] * c[3]);
        float n[3] = {0, 0, 0};
        if (len != 0.0f) {
            for (int i = 0; i < 3; i++) {
                n[i] = c[i] / len;
            }
        }
        o[0] = n[0] + n[2];
        o[1] = n[1] + n[2];
        o[2] = n[0] - n[2];
        o[3] = n[1] - n[2];
    }
    // From 25 (0x8F9B10) up to 125 (0x8F9B14) the blur ramps in, plus 1.1 x
    // the boost, at most 0.65 (0x8F9B18); under 1/255 nothing is drawn.
    const float lo = 25.0f, hi = 125.0f;
    if (!(speed > lo)) {
        return;
    }
    float t = speed < hi ? (speed - lo) / (hi - lo) : 1.0f;
    t = static_cast<float>(static_cast<double>(gMotionBlurBoost) * 1.1 + t);
    if (t > 0.65f) {
        t = 0.65f;
    }
    if (!(t >= 1.0f / 255.0f)) {
        return;
    }
    // Offsets scaled by (0.5 boost + 1) t / 160 (0x8F9B1C) x 1.1 (0x8910C4).
    const float k = (gMotionBlurBoost * 0.5f + 1.0f) * t * 0.00625f;
    PortMotionBlur blur;
    for (int i = 0; i < 4; i++) {
        blur.offsets[i] = o[i] * k * 1.1f;
    }
    // D3DRS_BLENDFACTOR: __ftol2(min(t, 1) x 255) in every channel.
    blur.factor = static_cast<float>(static_cast<int>((t > 1.0f ? 1.0f : t) * 255.0f)) / 255.0f;
    static const bool trace = EnvOn("NFSMW_TRACE_MOTION_BLUR");
    if (trace) {
        static int n;
        if (n++ % 60 == 0) {
            std::fprintf(stderr, "[motionblur] speed %.1f radial %.2f boost %.3f t %.3f factor %.3f offsets (%.4f %.4f %.4f %.4f)\n", speed,
                         radial, gMotionBlurBoost, t, blur.factor, blur.offsets[0], blur.offsets[1], blur.offsets[2], blur.offsets[3]);
        }
    }
    PortMetal3DSetMotionBlur(&blur);
}
