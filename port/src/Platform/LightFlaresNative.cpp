// Light flares (car lights, the sun, scenery light markers): speed.exe 1.3
// eRenderLightFlare (0x505380), UpdateLightFlareParameters (0x502640) and
// their tables, ePoly::ePoly (0x6C0A30) and
// eViewPlatInterface::GetScreenPosition (0x6BE940). The quads go through
// eViewPlatInterface::Render(ePoly *) (ParticlesNative.cpp).

#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/EcstasyData.hpp"
#include "Speed/Indep/Src/Ecstasy/eLight.hpp"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/light_flares_cg.h"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#include "Speed/Indep/Src/World/Rain.hpp"
#include "Speed/Indep/Src/World/WCollisionMgr.h"
#include "Speed/PC/Src/PCGraphicsOptions.hpp"

#include "../Mac/MetalBridge.h"

#include <cmath>

extern int32 DrawLightFlares;
extern TextureInfo *eLightFlareTextureInfos[3];

namespace {

// SingleLightFlareParameters (0x8F2498): 0x2C bytes per entry, written from
// the light_flares_cg attributes by UpdateLightFlareParameters; the
// initialisers are the exe's (used until the attributes are read).
struct FlareParameters {
    float min_size, max_size;   // MinSize, MaxSize (lerped by intensity)
    float colour[4];            // colour x 255
    float power;                // Power (flag 4: facing^Power)
    float z_bias;               // ZBias: towards the camera
    float min_scale, max_scale; // MinScale, Maxscale: clamp of view depth / H
    uint32 texture;             // flare_texture: eLightFlareTextureInfos index
};
FlareParameters gFlareParameters[22] = {
    {0, 64, {100, 120, 170, 80}, 1.45f, 0.8f, 0.01f, 0.048f, 0},
    {28, 28, {100, 120, 140, 20}, 3, 0.8f, 0.01f, 0.015f, 0},
    {20, 30, {128, 25, 25, 120}, 0.45f, 0.2f, 0.01f, 0.03f, 0},
    {20, 30, {128, 25, 25, 120}, 0.45f, 0.2f, 0.01f, 0.03f, 0},
    {20, 20, {255, 255, 255, 128}, 1.2f, 0.2f, 0.005f, 0.01f, 0},
    {20, 20, {128, 128, 128, 128}, 1, 0.2f, 0.005f, 0.01f, 0},
    {40, 50, {100, 20, 20, 150}, 1, 1, 0.02f, 0.04f, 0},
    {40, 50, {12, 12, 150, 180}, 1, 1, 0.03f, 0.04f, 0},
    {30, 40, {100, 100, 100, 128}, 1, 1, 0.01f, 0.02f, 0},
    {40, 50, {128, 0, 0, 150}, 1, 1, 0.01f, 0.02f, 0},
    {40, 50, {0, 0, 128, 150}, 1, 1, 0.01f, 0.02f, 0},
    {30, 40, {128, 128, 128, 128}, 1, 1, 0.01f, 0.02f, 0},
    {30, 30, {110, 100, 75, 110}, 1, 4, 0.001f, 0.1f, 0},
    {1, 4, {100, 120, 140, 16}, 1, 0, 0, 0.1f, 0},
    {10, 10, {110, 75, 0, 150}, 1, 2.7f, 0.001f, 0.1f, 0},
    {10, 10, {110, 0, 0, 150}, 1, 4, 0.001f, 0.1f, 0},
    {10, 10, {0, 0, 120, 150}, 1, 4, 0.001f, 0.1f, 0},
    {30, 30, {110, 75, 0, 150}, 1, 4, 0.001f, 0.1f, 0},
    {30, 30, {110, 10, 0, 150}, 1, 4, 0.001f, 0.1f, 0},
    {1, 10, {0, 100.5f, 20, 70}, 1, 1, 0.07f, 0.07f, 0},
    {1, 10, {105, 26.5f, 0, 70}, 1, 1, 0.07f, 0.07f, 0},
    {1, 20, {255, 255.5f, 255, 255}, 1, 5, 0.8f, 1, 0},
};
// The light_flares_cg collection of each entry (UpdateLightFlareParameters).
const uint32 kFlareParameterKeys[22] = {
    0x356f692a, 0xa95ab5ad, 0xb60dac69, 0xd364e8ac, 0x30ea58b4, 0x35ce359c, 0xf51bf17f, 0xdde4b816, 0x44c51b58, 0x93007a08, 0x0299e454,
    0x47cb2ad7, 0xe9faf022, 0x4ff2b81c, 0x96147a22, 0x37b7c081, 0x8412720e, 0x1e7fdc6e, 0x20fc956e, 0xd06fc6bb, 0xbf79f943, 0x55eaa808,
};
// LightFlareParameterIndicies (0x8F2868): up to two entries per flare type.
const signed char kFlareParameterIndices[22][2] = {
    {0, 1},   {2, -1},  {3, -1},  {4, -1},  {5, -1},  {6, -1},  {7, -1},  {8, -1},  {0, 1},   {0, 1},   {9, -1},
    {10, -1}, {11, -1}, {12, -1}, {14, -1}, {15, -1}, {16, -1}, {17, -1}, {18, -1}, {19, -1}, {20, -1}, {21, -1},
};
bool gFlareParametersNeedUpdating = true; // LightFlareParametersNeedUpdating (0x8F2860)

const float kReflectionStretchMin = 1.2f;  // MINreflectionStretch (0x8F23F0)
const float kReflectionStretchMax = 10.0f; // 0x8F23F4
float gReflectionStretch = 10.0f;          // reflectionStretch (0x8F23EC)
const float kEnvmapExpandSize = 2.0f;      // LightFlareEnvmapExpandSize (0x8F291C)
const float kFlareSweep = 240.0f;          // FlareSweep (0x8F2920)
const float kRainInTheHeadlights = 0.96f;  // RainInTheHeadlights (0x8F2924)
const float kNoReflection = 999.0f;        // ReflectPosZ "unset" (0x4479C000)

bVector3 Transform(const bMatrix4 &m, const bVector3 &p) {
    return bVector3(p.x * m.v0.x + p.y * m.v1.x + p.z * m.v2.x + m.v3.x, p.x * m.v0.y + p.y * m.v1.y + p.z * m.v2.y + m.v3.y,
                    p.x * m.v0.z + p.y * m.v1.z + p.z * m.v2.z + m.v3.z);
}

bVector3 Rotate(const bMatrix4 &m, const bVector3 &p) {
    return bVector3(p.x * m.v0.x + p.y * m.v1.x + p.z * m.v2.x, p.x * m.v0.y + p.y * m.v1.y + p.z * m.v2.y,
                    p.x * m.v0.z + p.y * m.v1.z + p.z * m.v2.z);
}

float Dot(const bVector3 &a, const bVector3 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// The PC back buffer width (0x982BE4) for GetPixelWidth / GetScreenPosition.
int RenderWidth(int *height = nullptr) {
    int w = 640, h = 480;
    PCGetResolution(gPCGraphicsOptions.RacingResolution, &w, &h);
    if (height) {
        *height = h;
    }
    return w;
}

} // namespace

// 0x502640: from the light_flares_cg attributes once the database exists.
void UpdateLightFlareParameters() {
    // (PC also waits for Attrib::Database::sThis; it exists before any frame.)
    if (!gFlareParametersNeedUpdating) {
        return;
    }
    gFlareParametersNeedUpdating = false;
    for (int i = 0; i < 22; i++) {
        Attrib::Gen::light_flares_cg attribs(kFlareParameterKeys[i], 0, nullptr);
        FlareParameters &p = gFlareParameters[i];
        const UMath::Vector4 &c = attribs.colour();
        p.colour[0] = c.x * 255.0f;
        p.colour[1] = c.y * 255.0f;
        p.colour[2] = c.z * 255.0f;
        p.colour[3] = c.w * 255.0f;
        p.min_size = attribs.MinSize();
        p.max_size = attribs.MaxSize();
        p.power = attribs.Power();
        p.z_bias = attribs.ZBias();
        p.min_scale = attribs.MinScale();
        p.max_scale = attribs.Maxscale();
        p.texture = attribs.flare_texture();
    }
}

ePoly::ePoly() {
    for (int i = 0; i < 4; i++) {
        Vertices[i] = bVector3(0.0f, 0.0f, 0.0f);
        *reinterpret_cast<uint32 *>(Colours[i]) = 0x80808080;
    }
    Vertices[1].x = 64.0f;
    Vertices[2].x = 64.0f;
    Vertices[2].y = 64.0f;
    Vertices[3].y = 64.0f;
    const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; i++) {
        UVs[i][0] = uvs[i][0];
        UVs[i][1] = uvs[i][1];
    }
    flags = 0;
}

// The view's world -> clip transform (PlatInfo +0x80) to pixels of the
// back buffer: x = (ndc.x + 1) w / 2, y = (1 - ndc.y) h / 2, z = depth / w.
// Natively the projection is SceneNative's ViewProjectionFor.
void eViewPlatInterface::GetScreenPosition(bVector3 *screen_position, const bVector3 *world_position) {
    eView *view = static_cast<eView *>(this);
    Camera *camera = view->GetCamera();
    int height;
    int width = RenderWidth(&height);
    *screen_position = bVector3(0.0f, 0.0f, 0.0f);
    if (camera == nullptr) {
        return;
    }
    bVector3 c = Transform(*camera->GetWorldToCameraMatrix(), *world_position);
    float fov = camera->GetFieldOfView() * (2.0f * 3.1415927f / 65536.0f);
    if (fov <= 0.0f || c.z == 0.0f) {
        return;
    }
    float sy = (1.0f / std::tan(fov * 0.5f)) * (4.0f / 3.0f);
    float sx = sy / PortMetalViewAspect();
    float ndc_x = sx * c.x / c.z;
    float ndc_y = -sy * c.y / c.z;
    screen_position->x = (ndc_x + 1.0f) * static_cast<float>(width) * 0.5f;
    screen_position->y = (ndc_y - 1.0f) * static_cast<float>(height) * -0.5f;
    screen_position->z = 1.0f / c.z;
}

// 0x505380. Returns 1 if the flare was in range (drawn or not).
int eRenderLightFlare(eView *view, eLightFlare *light_flare, bMatrix4 *local_world, float intensity_scale, eLightReflexionType reflexion,
                      flareType type, float reflection_override, uint32 colour_override, float size_scale) {
    if (DrawLightFlares == 0) {
        return 0;
    }
    if (type == FLARE_REFLECT) {
        float wet = view->Precipitation ? view->Precipitation->GetRoadDampness() : 0.0f; // Rain +0x36A0
        gReflectionStretch = (kReflectionStretchMax - kReflectionStretchMin) * wet + kReflectionStretchMin;
    }
    Camera *camera = view->GetCamera();
    if (light_flare == nullptr || local_world == nullptr || camera == nullptr) {
        return 0;
    }
    const int flare_type = light_flare->Type;
    const int flare_flags = light_flare->Flags;
    const bVector3 local_pos(light_flare->PositionX, light_flare->PositionY, light_flare->PositionZ);
    // The reflection's height: mirrored about the override, or about the
    // ground below (racing; the flare's own height otherwise) when unset or
    // for REF_TOPO.
    if (reflection_override != 0.0f) {
        bVector3 w = Transform(*local_world, local_pos);
        light_flare->ReflectPosZ = reflection_override - (w.z - reflection_override);
    } else if (light_flare->ReflectPosZ == kNoReflection || reflexion == REF_TOPO) {
        bVector3 w = Transform(*local_world, local_pos);
        if (TheGameFlowManager.GetState() == GAMEFLOW_STATE_RACING) {
            UMath::Vector3 sim_pos;
            sim_pos.x = -w.y;
            sim_pos.y = w.z;
            sim_pos.z = w.x;
            float h = 0.0f;
            WCollisionMgr(0, 3).GetWorldHeightAtPointRigorous(sim_pos, h, nullptr);
            light_flare->ReflectPosZ = h - (w.z - h);
        } else {
            light_flare->ReflectPosZ = w.z;
        }
    }
    bMatrix4 world = *local_world;
    // Where the flare is drawn: its world position (the reflection: at
    // ReflectPosZ).
    bVector3 world_pos = Transform(world, local_pos);
    bVector3 direction;
    if (type == FLARE_REFLECT) {
        if (light_flare->ReflectPosZ == kNoReflection || light_flare->ReflectPosZ == 10000.0f) {
            return 0;
        }
        world_pos.z = light_flare->ReflectPosZ;
        bVector3 bbox_min, bbox_max;
        bInitializeBoundingBox(&bbox_min, &bbox_max, &world_pos);
        bExpandBoundingBox(&bbox_min, &bbox_max, &world_pos, gReflectionStretch);
        if (view->GetVisibleStateSB(&bbox_min, &bbox_max, nullptr) == EVISIBLESTATE_NOT) {
            return 0;
        }
        direction = bVector3(light_flare->DirectionX, light_flare->DirectionY, -light_flare->DirectionZ);
    } else if (type == FLARE_ENV) {
        bVector3 bbox_min, bbox_max;
        bInitializeBoundingBox(&bbox_min, &bbox_max, &world_pos);
        bExpandBoundingBox(&bbox_min, &bbox_max, &world_pos, kEnvmapExpandSize);
        if (view->GetVisibleStateSB(&bbox_min, &bbox_max, nullptr) == EVISIBLESTATE_NOT) {
            return 0;
        }
        direction = bVector3(light_flare->DirectionX, light_flare->DirectionY, light_flare->DirectionZ);
    } else {
        // TODO(port): 0x505694, for FLARE_CAT_P1/P2 (pool flares not of
        // types 17..20): with the view's player headlights (sub_410D40 /
        // vtable +0x10), fades flares outside their beam (cos < 0.96,
        // RainInTheHeadlights) -- the rain-in-the-headlights effect.
        (void)kRainInTheHeadlights;
        if (view->GetVisibleStateSB(&world_pos, nullptr) == EVISIBLESTATE_NOT) {
            return 0;
        }
        direction = bVector3(light_flare->DirectionX, light_flare->DirectionY, light_flare->DirectionZ);
    }
    // Fade by squared distance (env map: 3900..4000, else 32000..40000).
    const bVector3 &eye = *camera->GetPosition();
    bVector3 to_camera(eye.x - world_pos.x, eye.y - world_pos.y, eye.z - world_pos.z);
    const float d2 = Dot(to_camera, to_camera);
    const float fade_start = type == FLARE_ENV ? 3900.0f : 32000.0f;
    const float fade_end = type == FLARE_ENV ? 4000.0f : 40000.0f;
    if (d2 > fade_end) {
        return 0;
    }
    float fade = 1.0f;
    if (d2 > fade_start) {
        fade = 1.0f - (d2 - fade_start) / (fade_end - fade_start);
    }
    bNormalize(&to_camera, &to_camera);
    // How much the flare faces the camera (flag 1: both ways).
    float facing = Dot(to_camera, Rotate(world, direction));
    if (flare_flags & 1) {
        facing = std::fabs(facing);
    } else if (facing < 0.0f) {
        facing = 0.0f;
    }
    const bMatrix4 &cam = *camera->GetCameraMatrix();
    bVector3 right(cam.v0.x, cam.v1.x, cam.v2.x);
    bVector3 up(cam.v0.y, cam.v1.y, cam.v2.y);
    bVector3 forward(cam.v0.z, cam.v1.z, cam.v2.z);
    // View depth over H (eView +0xC), the size scale.
    const float depth_scale = Transform(cam, world_pos).z / view->H;

    if (flare_type < 0 || flare_type >= 22) {
        return 1;
    }
    for (int k = 0; k < 2; k++) {
        int index = kFlareParameterIndices[flare_type][k];
        if (index < 0) {
            continue;
        }
        const FlareParameters &p = gFlareParameters[index];
        TextureInfo *texture;
        if (type == FLARE_REFLECT) {
            texture = GetTextureInfo(0x3394fe62, 0, 0);
        } else {
            texture = p.texture < 3 ? eLightFlareTextureInfos[p.texture] : nullptr;
        }
        if (texture == nullptr) {
            continue;
        }
        float intensity = intensity_scale; // _intensity (0x91599C)
        if (flare_flags & 4) {
            intensity = std::pow(facing, p.power) * intensity_scale;
            if (intensity > 1.0f) {
                intensity = 1.0f;
            }
        }
        float size = (p.max_size - p.min_size) * intensity + p.min_size;
        if (!(size > 0.0f)) {
            continue;
        }
        bVector3 centre(world_pos.x + to_camera.x * p.z_bias, world_pos.y + to_camera.y * p.z_bias, world_pos.z + to_camera.z * p.z_bias);
        float scale = depth_scale;
        if (scale < p.min_scale) {
            scale = p.min_scale;
        }
        if (scale > p.max_scale) {
            scale = p.max_scale;
        }
        size = size * scale * size_scale;
        ePoly poly;
        const float r = intensity * p.colour[0], g = intensity * p.colour[1], b = intensity * p.colour[2];
        float a = intensity * p.colour[3] * fade;
        a = a + a;
        if (a > 255.0f) {
            a = 255.0f;
        }
        bVector3 r_axis = right, u_axis = up;
        if (type == FLARE_NORM) {
            // Spun about the view axis by the screen position.
            bVector3 axis = forward;
            bNormalize(&axis, &axis);
            bVector3 screen;
            view->GetScreenPosition(&screen, &world_pos);
            float flare_rot = screen.x / static_cast<float>(RenderWidth()) * kFlareSweep; // FlareRot (0x916038)
            bMatrix4 rot;
            eCreateAxisRotationMatrix(&rot, axis, bDegToAng(flare_rot)); // bDegToShort
            bVector4 rv(r_axis.x, r_axis.y, r_axis.z, 0.0f), uv(u_axis.x, u_axis.y, u_axis.z, 0.0f);
            eMulVector(&rv, &rot, &rv);
            eMulVector(&uv, &rot, &uv);
            r_axis = bVector3(rv.x, rv.y, rv.z);
            u_axis = bVector3(uv.x, uv.y, uv.z);
        }
        r_axis *= size;
        u_axis *= size;
        if (type == FLARE_REFLECT) {
            float stretch = gReflectionStretch;
            if (light_flare->PositionZ - light_flare->ReflectPosZ < 4.0f) {
                stretch *= 0.5f;
            }
            u_axis *= stretch;
        }
        uint32 colour;
        if (colour_override == 0) {
            colour = static_cast<uint32>(static_cast<int>(a)) << 24 | static_cast<uint32>(static_cast<int>(b)) << 16 |
                     static_cast<uint32>(static_cast<int>(g)) << 8 | static_cast<uint32>(static_cast<int>(r));
        } else {
            colour = static_cast<uint32>(static_cast<int>(a)) << 24 | colour_override;
        }
        for (int i = 0; i < 4; i++) {
            *reinterpret_cast<uint32 *>(poly.Colours[i]) = colour;
        }
        poly.Vertices[0] = bVector3(centre.x - r_axis.x - u_axis.x, centre.y - r_axis.y - u_axis.y, centre.z - r_axis.z - u_axis.z);
        poly.Vertices[1] = bVector3(centre.x + r_axis.x - u_axis.x, centre.y + r_axis.y - u_axis.y, centre.z + r_axis.z - u_axis.z);
        poly.Vertices[2] = bVector3(centre.x + r_axis.x + u_axis.x, centre.y + r_axis.y + u_axis.y, centre.z + r_axis.z + u_axis.z);
        poly.Vertices[3] = bVector3(centre.x - r_axis.x + u_axis.x, centre.y - r_axis.y + u_axis.y, centre.z - r_axis.z + u_axis.z);
        view->Render(&poly, texture, eGetIdentityMatrix(), 0, 0.0f);
    }
    return 1;
}
