// Native 3D scene submission (the world/frontend views of the PC eDisplayFrame).
//
// For now this only hosts the model smoke test (NFSMW_SMOKE_CAR /
// NFSMW_SMOKE_SOLIDS, below), drawn over the frontend with an orbiting camera.
// It checks solid loading, textures and the Metal 3D path before the car
// viewer (which places the real frontend car) is ported.

#include "Speed/PC/Src/PCGraphicsOptions.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/EcstasyData.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Ecstasy/eLight.hpp"
#include "Speed/Indep/Src/Ecstasy/eEnvMap.hpp"
#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Frontend/FECarLoader.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Safehouse/FEPkg_GarageMain.hpp"
#include "Speed/Indep/Src/World/Car.hpp"
#include "Speed/Indep/Src/Misc/ResourceLoader.hpp"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#include "Speed/Indep/Src/World/Scenery.hpp"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/World/VehicleRenderConn.h"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/effects.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/timeofdaylighting.h"
#include "Speed/Indep/Src/World/Rain.hpp"
#include "Speed/Indep/Src/World/SkyRender.hpp"
#include "Speed/Indep/Src/Misc/Timer.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include "../Mac/MetalBridge.h"
#include "../Mac/MetalPrelitBridge.h"
#include "ParticlesNative.h"
#include "ScreenEffectsNative.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

TextureInfo *PortSolidTexture(eSolid *solid, int index); // SolidNative.cpp
void PortRainDropsFrame();                                 // RainNative.cpp
void PortRenderShadowMapMeshes();                          // ShadowMapMeshNative.cpp
extern bTList<FrontEndRenderingCar> FrontEndRenderingCarList; // CarRender.cpp
extern bTList<eLightMaterial> LightMaterialList;               // eLight.cpp

namespace {

typedef float M44[16]; // row-major, row vectors (p' = p * M)

void Identity(M44 m) {
    std::memset(m, 0, sizeof(M44));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void Multiply(M44 out, const M44 a, const M44 b) {
    M44 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] + a[i * 4 + 2] * b[2 * 4 + j] + a[i * 4 + 3] * b[3 * 4 + j];
        }
    }
    std::memcpy(out, r, sizeof(M44));
}

struct V3 {
    float x, y, z;
};
V3 Sub(V3 a, V3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
float Dot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 Cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 Normalize(V3 a) {
    float l = std::sqrt(Dot(a, a));
    return {a.x / l, a.y / l, a.z / l};
}

// World (z up) -> view (x right, y up, z forward) -> Metal clip (z 0..1).
void ViewProjection(M44 out, V3 eye, V3 target, float fov_y, float aspect, float znear, float zfar) {
    V3 f = Normalize(Sub(target, eye));
    V3 r = Normalize(Cross(f, V3{0, 0, 1}));
    V3 u = Cross(r, f);
    M44 view = {r.x, u.x, f.x, 0, r.y, u.y, f.y, 0, r.z, u.z, f.z, 0, -Dot(eye, r), -Dot(eye, u), -Dot(eye, f), 1};
    float sy = 1.0f / std::tan(fov_y * 0.5f);
    float sx = sy / aspect;
    float a = zfar / (zfar - znear);
    float b = -znear * zfar / (zfar - znear);
    M44 proj = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, a, 1, 0, 0, b, 0};
    Multiply(out, view, proj);
}

bool EndsWith(const char *s, const char *suffix) {
    size_t n = std::strlen(s), m = std::strlen(suffix);
    return n >= m && std::strcmp(s + n - m, suffix) == 0;
}

void *MetalTextureOf(TextureInfo *t) {
    if (t == nullptr || t->GetPlatInfo() == nullptr) {
        return nullptr;
    }
    return t->GetPlatInfo()->MetalTexture;
}

// TimeOfDay +0x60 (the blended 'timeofdaylighting' CarSpecScale, set by
// SetTimeOfDayLighting); scales the car's specular colours.
float gCarSpecScale = 1.0f;

// Car effect constants from a light material (see Port3DDraw::material), as
// speed.exe 1.3 sub_6C76E0 sets them for the shader techniques: DiffuseRange.w
// = (MaxA - MinA) * 0.25, specular min/max scales times TimeOfDay CarSpecScale,
// envmap min/range from the red channel times the scale (only .x is read).
// TODO(port): sub_6C76E0 zeroes diffuse and specular for render flag 0x100
// (without 0x80) while the env map is on; no ported caller sets 0x100 yet.
void MaterialConstants(const eLightMaterial *light_material, float out[20]) {
    if (light_material == nullptr) {
        const float defaults[20] = {1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 8};
        std::memcpy(out, defaults, sizeof(defaults));
        return;
    }
    const eLightMaterialData &d = light_material->feData;
    const float spec_min_scale = gCarSpecScale * d.SpecularMinScale, spec_max_scale = gCarSpecScale * d.SpecularMaxScale;
    float dmin[4] = {d.DiffuseMinScale * d.DiffuseMinR, d.DiffuseMinScale * d.DiffuseMinG, d.DiffuseMinScale * d.DiffuseMinB, d.DiffuseMinA};
    float dmax[4] = {d.DiffuseMaxScale * d.DiffuseMaxR, d.DiffuseMaxScale * d.DiffuseMaxG, d.DiffuseMaxScale * d.DiffuseMaxB, d.DiffuseMaxA};
    float smin[4] = {spec_min_scale * d.SpecularMinR, spec_min_scale * d.SpecularMinG, spec_min_scale * d.SpecularMinB, 0};
    float smax[4] = {spec_max_scale * d.SpecularMaxR, spec_max_scale * d.SpecularMaxG, spec_max_scale * d.SpecularMaxB, 0};
    for (int i = 0; i < 4; i++) {
        out[i] = dmin[i];
        out[4 + i] = dmax[i] - dmin[i];
        out[8 + i] = smin[i];
        out[12 + i] = smax[i] - smin[i];
    }
    out[7] = (d.DiffuseMaxA - d.DiffuseMinA) * 0.25f;
    out[16] = d.EnvmapMinR * d.EnvmapMinScale;
    out[17] = d.EnvmapMaxR * d.EnvmapMaxScale - out[16];
    out[18] = d.EnvmapPower;   // x flt_9018DC (1)
    out[19] = d.SpecularPower; // x flt_9018E0 (1)
}

// A solid's texture table entry looked up by name each time (the normal and
// specular maps can come from packs that unload while the solid stays, so
// PortSolidTexture's cached pointer could dangle).
TextureInfo *SolidTextureUncached(eSolid *solid, int index) {
    if (index < 0 || index >= solid->NumTextureTableEntries || !solid->pTextureTable) {
        return nullptr;
    }
    return GetTextureInfo(solid->pTextureTable[index].NameHash, 0, 0);
}

// The GLOSSYWINDOW effect's WindowReflection: the WINDOWREFLECTION texture
// (looked up by sub_6D7400 when the effect begins).
void *WindowReflectionTexture() {
    static const unsigned int hash = bStringHash("WINDOWREFLECTION");
    return MetalTextureOf(GetTextureInfo(hash, 1, 0));
}

// The player view's world -> clip matrix while its models are submitted
// (render target type 1), for the per-draw shadow level; null otherwise
// (env-map faces, the shadow map: level 0).
const float *gShadowLevelClip = nullptr;

// speed.exe 1.3 sub_6E0AB0, in the player view: the shadow level of a
// WORLD/WORLDREFLECT/WORLDNORMALMAP/CAR/GLOSSYWINDOW (or PARTICLES) draw from
// the clip-space z of its group's bound centre (TransformBound, then the
// view's projection; about the view depth): 0 with Shadow Detail (0x901830)
// off or beyond 400 (0x8FAE60); below 100 (0x8FAE58) the effect's highest
// level up to 0x982C3C (2: CAR/WORLD/... have levels 1..3 on the NVIDIA
// path, sub_6D5C10, capped at 2 by sub_6D2100); 100..400 level 1 (the
// min(..., 0x982C3C - 1 or - 2) results are raised to 1).
uint32_t ShadowLevel(const eSolidPlatGroup &group, const M44 world) {
    if (gShadowLevelClip == nullptr || gPCGraphicsOptions.ShadowDetail < 1) {
        return 0;
    }
    switch (group.Effect) {
    case PORT_EFFECT_WORLD:
    case PORT_EFFECT_WORLDREFLECT:
    case PORT_EFFECT_WORLDNORMALMAP:
    case PORT_EFFECT_CAR:
    case PORT_EFFECT_GLOSSYWINDOW:
        break;
    default:
        return 0;
    }
    float c[3] = {(group.AABBMin[0] + group.AABBMax[0]) * 0.5f, (group.AABBMin[1] + group.AABBMax[1]) * 0.5f,
                  (group.AABBMin[2] + group.AABBMax[2]) * 0.5f};
    float w[3];
    for (int j = 0; j < 3; j++) {
        w[j] = c[0] * world[0 * 4 + j] + c[1] * world[1 * 4 + j] + c[2] * world[2 * 4 + j] + world[3 * 4 + j];
    }
    const float *m = gShadowLevelClip;
    float z = w[0] * m[2] + w[1] * m[6] + w[2] * m[10] + m[14];
    if (z > 400.0f) {
        return 0;
    }
    if (z < 100.0f) {
        return 2;
    }
    return z == 100.0f ? 0 : 1;
}

// The view being built: its camera's world position and world -> camera
// matrix (the eViews entry sub_6C8000 reads for LocalEyePos and WorldView).
float gSceneCameraPos[3];
M44 gSceneWorldToCamera = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void SetSceneCamera(Camera *camera) {
    const bVector3 *p = camera->GetPosition();
    gSceneCameraPos[0] = p->x;
    gSceneCameraPos[1] = p->y;
    gSceneCameraPos[2] = p->z;
    std::memcpy(gSceneWorldToCamera, camera->GetWorldToCameraMatrix(), sizeof(M44));
}

// render_flags: eViewPlatInterface::Render flags (bit 0: CarRender's
// disable_env_flag, no environment map).
void DrawSolid(eSolid *solid, const M44 world, const eDynamicLightContext *lights = nullptr, unsigned int render_flags = 0) {
    eSolidPlatInfo *info = solid->GetPlatInfo();
    if (info == nullptr) {
        return;
    }
    for (int g = 0; g < info->NumGroups; g++) {
        const eSolidPlatGroup &group = info->Groups[g];
        TextureInfo *texture = PortSolidTexture(solid, group.TextureSlots[0]);
        Port3DDraw d = {};
        d.vertex_buffer = info->VertexBuffers[group.VertexBuffer];
        d.vertex_stride = info->VertexStrides[group.VertexBuffer];
        d.index_buffer = info->IndexBuffer;
        d.texture = MetalTextureOf(texture);
        d.first_index = group.FirstIndex;
        d.effect = group.Effect;
        if (group.Effect == PORT_EFFECT_GLOSSYWINDOW) {
            d.texture2 = WindowReflectionTexture();
        } else if (group.Effect == PORT_EFFECT_WORLDNORMALMAP || group.Effect == PORT_EFFECT_WORLDREFLECT) {
            // sub_6C6D10: texture slots 1 (NormalMapTexture) and 3 (SpecularMapTexture).
            d.texture2 = MetalTextureOf(SolidTextureUncached(solid, group.TextureSlots[1]));
            d.texture3 = MetalTextureOf(SolidTextureUncached(solid, group.TextureSlots[3]));
        }
        int light_index = group.TextureSlots[5];
        const eLightMaterial *light_material = nullptr;
        if (light_index < solid->NumLightMaterials && solid->LightMaterialTable) {
            light_material = solid->LightMaterialTable[light_index].LightMaterial;
        }
        MaterialConstants(light_material, d.material);
        if (lights && lights->NumLights > 0) {
            const float *dm = reinterpret_cast<const float *>(&lights->LocalDirectionMatrix);
            const float *cm = reinterpret_cast<const float *>(&lights->LocalColourMatrix);
            for (int i = 0; i < 3; i++) {
                d.lights[i * 4 + 0] = dm[0 * 4 + i]; // directions: columns
                d.lights[i * 4 + 1] = dm[1 * 4 + i];
                d.lights[i * 4 + 2] = dm[2 * 4 + i];
                d.lights[12 + i * 4 + 0] = cm[i * 4 + 0]; // colours: rows
                d.lights[12 + i * 4 + 1] = cm[i * 4 + 1];
                d.lights[12 + i * 4 + 2] = cm[i * 4 + 2];
            }
            d.flags |= PORT_3D_LIT;
            if (!(render_flags & 1)) {
                d.flags |= PORT_3D_ENVMAP;
            }
        }
        if (group.Effect == PORT_EFFECT_CAR) {
            // sub_6C8000 (speed.exe 1.3), not the light context: LocalEyePos =
            // the view's camera position x inverse(local_world)
            // (eInvertTransformationMatrix 0x6BDE60, D3DXVec4Transform), and
            // WorldView = local_world x the view matrix. (The context's
            // LocalEyePosition, clamped to 2.78..8 by elSetupEnvMap, and its
            // EnvMapMatrix are never handed to the shader.)
            bMatrix4 local_world, world_local;
            std::memcpy(&local_world, world, sizeof(M44));
            eInvertTransformationMatrix(&world_local, &local_world);
            bVector4 eye(gSceneCameraPos[0], gSceneCameraPos[1], gSceneCameraPos[2], 1.0f);
            eMulVector(&eye, &world_local, &eye);
            d.lights[24] = eye.x;
            d.lights[25] = eye.y;
            d.lights[26] = eye.z;
            M44 world_view;
            Multiply(world_view, world, gSceneWorldToCamera);
            for (int r = 0; r < 3; r++) {
                for (int k = 0; k < 4; k++) {
                    d.envmap[r * 4 + k] = k < 3 ? world_view[r * 4 + k] : 0.0f;
                }
            }
        }
        d.num_indices = group.NumIndices;
        d.shadow_level = ShadowLevel(group, world);
        {
            static const bool trace = std::getenv("NFSMW_TRACE_CARMAT") != nullptr;
            static int n;
            if (trace && group.Effect == PORT_EFFECT_CAR && n < 400) {
                n++;
                const float *m = d.material, *l = d.lights;
                std::fprintf(stderr,
                             "[carmat] %s g%d lm %s dmin(%.3f %.3f %.3f %.3f) drange(%.3f %.3f %.3f %.3f) smin(%.3f %.3f %.3f) srange(%.3f %.3f %.3f) "
                             "env %.3f+%.3f pow %.2f spow %.2f lit %d sl %u\n",
                             solid->Name, g, light_material ? light_material->Name : "-", m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8],
                             m[9], m[10], m[12], m[13], m[14], m[16], m[17], m[18], m[19], (d.flags & PORT_3D_LIT) != 0, d.shadow_level);
                static int nl;
                if (world[10] > 0 && nl++ < 3) {
                    std::fprintf(stderr, "[carmat]  L0 (%.3f %.3f %.3f) c(%.3f %.3f %.3f)  L1 (%.3f %.3f %.3f) c(%.3f %.3f %.3f)  L2 (%.3f %.3f %.3f) c(%.3f %.3f %.3f) eye (%.3f %.3f %.3f)\n",
                                 l[0], l[1], l[2], l[12], l[13], l[14], l[4], l[5], l[6], l[16], l[17], l[18], l[8], l[9], l[10], l[20], l[21], l[22],
                                 l[24], l[25], l[26]);
                    std::fprintf(stderr, "[carmat]  world (%.3f %.3f %.3f)(%.3f %.3f %.3f)(%.3f %.3f %.3f) t(%.3f %.3f %.3f)\n", world[0], world[1],
                                 world[2], world[4], world[5], world[6], world[8], world[9], world[10], world[12], world[13], world[14]);
                }
            }
        }
        // sub_6C68B0: alpha test when the texture is punch-through (ref
        // 0x5F); blending only by AlphaBlendType -- SRCCOPY draws opaque even
        // when the alpha is MODULATED (car skins: alpha is a gloss mask).
        // BLEND/DEST_BLEND: SrcAlpha/InvSrcAlpha (DEST_* uses dest alpha on
        // PC); ADDATIVE/DEST_ADDATIVE: SrcAlpha/One. TODO(port):
        // SUBTRACTIVE (Zero/InvSrcColor) is drawn as an alpha blend.
        if (texture) {
            switch (texture->AlphaBlendType) {
                case TEXBLEND_SRCCOPY:
                    break;
                case TEXBLEND_ADDATIVE:
                case TEXBLEND_DEST_ADDATIVE:
                    d.flags |= PORT_3D_ADDITIVE;
                    break;
                default:
                    d.flags |= PORT_3D_ALPHA_BLEND;
                    break;
            }
            if (texture->AlphaUsageType == TEXUSAGE_PUNCHTHRU) {
                d.flags |= PORT_3D_ALPHA_TEST;
            }
        }
        std::memcpy(d.world, world, sizeof(M44));
        PortMetal3DAdd(&d);
    }
}

// NFSMW_SMOKE_CAR=<car>: loads CARS\<car>\{TEXTURES,GEOMETRY}.BIN and draws the
// car's top-detail parts. NFSMW_SMOKE_SOLIDS=<text>: draws every loaded solid
// whose name contains <text> (e.g. BACKDROP_CRIB, the frontend garage).
const char *SmokeCar() {
    static const char *car = std::getenv("NFSMW_SMOKE_CAR");
    return car;
}

const char *SmokeSolids() {
    static const char *solids = std::getenv("NFSMW_SMOKE_SOLIDS");
    return solids;
}

bool SmokeWants(const char *name) {
    if (const char *car = SmokeCar()) {
        size_t n = std::strlen(car);
        // Top detail only, one body kit, no damage/decal variants.
        if (std::strncmp(name, car, n) == 0 && name[n] == '_' && EndsWith(name, "_A") &&
            !(std::strstr(name, "_KIT") && !std::strstr(name, "_KIT00_")) && !std::strstr(name, "DAMAGE") && !std::strstr(name, "DECAL")) {
            return true;
        }
    }
    if (const char *solids = SmokeSolids()) {
        return std::strstr(name, solids) != nullptr;
    }
    return false;
}

// Requests the car's files once and fits an orbit camera to what's drawn.
bool SmokeSetup(int frame, M44 view_proj) {
    if (SmokeCar() == nullptr && SmokeSolids() == nullptr) {
        return false;
    }
    static bool requested = false;
    if (const char *car = SmokeCar(); car && !requested && frame > 120) {
        requested = true;
        char path[128];
        std::snprintf(path, sizeof(path), "CARS\\%s\\TEXTURES.BIN", car);
        LoadResourceFile(path, RESOURCE_FILE_CAR, 0);
        std::snprintf(path, sizeof(path), "CARS\\%s\\GEOMETRY.BIN", car);
        LoadResourceFile(path, RESOURCE_FILE_CAR, 0);
    }

    V3 lo = {1e9f, 1e9f, 1e9f}, hi = {-1e9f, -1e9f, -1e9f};
    for (eSolid *s = SolidList.GetHead(); s != SolidList.EndOfList(); s = s->GetNext()) {
        if (SmokeWants(s->Name)) {
            lo = {std::fmin(lo.x, s->AABBMinX), std::fmin(lo.y, s->AABBMinY), std::fmin(lo.z, s->AABBMinZ)};
            hi = {std::fmax(hi.x, s->AABBMaxX), std::fmax(hi.y, s->AABBMaxY), std::fmax(hi.z, s->AABBMaxZ)};
        }
    }
    V3 centre = {0, 0, 0.6f};
    float radius = 3.0f;
    if (lo.x <= hi.x) {
        centre = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        radius = 0.5f * std::sqrt(Dot(Sub(hi, lo), Sub(hi, lo)));
    }
    float angle = frame * 0.01f;
    float dist = radius * 2.2f;
    V3 eye = {centre.x + dist * std::cos(angle), centre.y + dist * std::sin(angle), centre.z + radius * 0.6f};
    ViewProjection(view_proj, eye, centre, 0.75f, PortMetalViewAspect(), radius * 0.02f, radius * 10.0f);
    return true;
}

void SmokeDraw() {
    M44 world;
    Identity(world);
    static int logged = -1;
    int drawn = 0;
    for (eSolid *s = SolidList.GetHead(); s != SolidList.EndOfList(); s = s->GetNext()) {
        if (SmokeWants(s->Name)) {
            DrawSolid(s, world);
            drawn++;
        }
    }
    if (drawn != logged) {
        logged = drawn;
        std::fprintf(stderr, "[smoke] drawing %d solids\n", drawn);
    }
}

} // namespace

void RenderFrontEndCars(eView *view, int reflection); // CarRender.cpp
void UpdateEnvironmentMapCameras(); // CarRender.cpp
void StuffSkyLayer(eView *view, SKY_LAYER layer); // SkyRender.cpp
int PortEnvMapQuality();            // EnvMapNative.cpp
void *PortFEEnvMapCube();           // EnvMapNative.cpp


namespace {

// speed.exe 1.3's TimeOfDay (TimeOfDayPointer, built by 0x770D70): sun
// speed +0x0 (1), direction +0x4 (-1), position +0x8 (0.45), and the two
// angles +0xC (3.28) and +0x10 (0.5). sub_769360, called every frame by
// ___eDisplayFrame, moves the position and stores the normalised sun
// direction at +0x180 (the world effect's LocalLightVec, in model space).
struct TimeOfDaySun {
    float speed = 1.0f;
    int direction = -1;
    float position = 0.45f;
    float angle_a = 3.28f;
    float angle_b = 0.5f;
};
TimeOfDaySun gSun;

V3 TickSunDirection() {
    // Online races take the position from the server (sub_735A30); not here.
    float lo = TheGameFlowManager.GetState() == GAMEFLOW_STATE_IN_FRONTEND ? 0.35f : 0.05f;
    TimeOfDaySun &t = gSun;
    t.position += RealTimeElapsed * (1.0f / 60.0f) * static_cast<float>(t.direction) * t.speed * 0.05f;
    if (!(t.position >= lo)) {
        t.position = lo;
        t.direction = -t.direction;
    } else if (t.position > 1.0f - lo) {
        t.position = 1.0f - lo;
        t.direction = -t.direction;
    }
    float p = std::fmin(std::fmax(t.position, lo), 1.0f - lo);
    float a = (p - 0.5f) * 3.1415927f;
    float b = t.angle_a;
    float c = t.angle_b;
    // bSin(x + pi/2) = cos(x); the 100000 scale is normalised away.
    V3 sun = {std::sin(a) * std::cos(b) + std::sin(b) * std::sin(c) * std::cos(a), std::sin(c) * std::cos(b) * std::cos(a) + std::sin(a) * std::sin(b),
              std::cos(c) * std::cos(a)};
    return Normalize(sun);
}

// sub_6D6CF0 (called by ___eDisplayFrame before the views draw): the world
// effects' scattering constants from the 'effects' collection 0xCE3B435B
// (byte 0x901934, which would take them from the TimeOfDay, is never set).
// Rayleigh and Mie strengths map from 0..100 onto 0.0001..0.01 (globals
// 0x901938/0x90193C).
void SetWorldFog(V3 sun, PortWorldFog &f) {
    Attrib::Gen::effects fx(0xce3b435b, 0, nullptr);
    const float lo = 0.0001f, range = 0.01f - 0.0001f;
    float bm_multiply = fx.BmMultiply() * 0.01f;
    float br_multiply = fx.BrMultiply() * 0.01f;
    const UMath::Vector4 &bm_colour = fx.ConstantBm();
    const UMath::Vector4 &br_colour = fx.ConstantBr();
    float g = fx.FogG();
    float inscatter = fx.InScatterMulitply();
    bool enable = fx.FogEnable();
    float mie = bm_multiply > 0.0f ? bm_multiply * range + lo : 0.0f; // else dbl_8942E0 (0)
    float rayleigh = br_multiply * range + lo;
    float br[3] = {rayleigh * br_colour.x, rayleigh * br_colour.y, rayleigh * br_colour.z};
    float bm[3] = {mie * bm_colour.x, mie * bm_colour.y, mie * bm_colour.z};

    f.enabled = enable ? 1 : 0;
    if (enable) {
        for (int i = 0; i < 3; i++) {
            f.br_plus_bm[i] = br[i] + bm[i];
            f.one_over_brbm[i] = f.br_plus_bm[i] != 0.0f ? 1.0f / f.br_plus_bm[i] : 0.0f;
            f.const1[i] = br[i] * 0.059683103f; // 3/(16 pi), 0x8AFB5C
            f.const2[i] = bm[i] * 0.079577468f; // 1/(4 pi), 0x8AFB58
        }
    }
    f.const3[0] = (1.0f - g) * (1.0f - g);
    f.const3[1] = g * g + 1.0f;
    f.const3[2] = g * -2.0f;
    f.const3[3] = enable ? inscatter : 0.0f;
    f.fog_value = 1.0f; // FOGVALUE: 1 unless the TimeOfDay source is used
    f.light_dir[0] = sun.x;
    f.light_dir[1] = sun.y;
    f.light_dir[2] = sun.z;
    static const bool trace = std::getenv("NFSMW_TRACE_FOG") != nullptr;
    static int frame;
    if (trace && frame++ % 300 == 0) {
        std::fprintf(stderr,
                     "[fog] enable %d BrMul %.3f BmMul %.3f Br (%.3f %.3f %.3f %.3f) Bm (%.3f %.3f %.3f %.3f) g %.3f inscatter %.3f sun (%.3f %.3f %.3f) pos %.3f\n",
                     enable, fx.BrMultiply(), fx.BmMultiply(), br_colour.x, br_colour.y, br_colour.z, br_colour.w, bm_colour.x, bm_colour.y, bm_colour.z,
                     bm_colour.w, g, inscatter, sun.x, sun.y, sun.z, gSun.position);
    }
}

// TimeOfDay lighting, sub_769580 (called by ___eDisplayFrame with the view's
// Rain cloud intensity, PC Rain +0x290): two pairs of 'timeofdaylighting'
// collections (TimeOfDay +0xC0..+0x14C) are each blended by the cloud
// intensity (sub_768EE0), then mixed by sun height (sub_769190) into
// TimeOfDay +0x20, weight 1 - (clamp(sun.z, 0.25, 0.5) - 0.25) * 4 for the
// low-sun pair. With the visual treatment on (0x901828, sub_6C0A20) the pairs
// are 0xB902CFAD..0xD835D0C8 (low sun) and 0x3DCFA673..0x59C373C3; otherwise
// 0x0E1C797C..0x919B6689 and 0xA01A3942..0x94ABDFF0. The effect-begin code
// (0x6D6AA0) then sets DiffuseColour = (min(diffuse.rgb, 1), 1) (0x6C6300,
// scale 0x9018C4 = 1) and AmbientColour = (min(ambient.rgb, 1), 4)
// (0x6C6450; w = TimeOfDay +0x3C, always 4).
void SetTimeOfDayLighting(V3 sun, float cloud, PortWorldFog &f) {
    const bool visual_treatment = gPCGraphicsOptions.VisualTreatment != 0; // sub_6C0A20
    unsigned int low_a = visual_treatment ? 0xb902cfad : 0x0e1c797c, low_b = visual_treatment ? 0xd835d0c8 : 0x919b6689;
    unsigned int high_a = visual_treatment ? 0x3dcfa673 : 0xa01a3942, high_b = visual_treatment ? 0x59c373c3 : 0x94abdff0;
    Attrib::Gen::timeofdaylighting la(low_a, 0, nullptr), lb(low_b, 0, nullptr), ha(high_a, 0, nullptr), hb(high_b, 0, nullptr);
    float z = sun.z > 0.25f ? (sun.z > 0.5f ? 0.5f : sun.z) : 0.25f;
    float low_weight = 1.0f - (z - 0.25f) * 4.0f;
    auto blend = [&](const UMath::Vector4 &a0, const UMath::Vector4 &a1, const UMath::Vector4 &b0, const UMath::Vector4 &b1, float out[4]) {
        const float lo[3] = {a0.x + (a1.x - a0.x) * cloud, a0.y + (a1.y - a0.y) * cloud, a0.z + (a1.z - a0.z) * cloud};
        const float hi[3] = {b0.x + (b1.x - b0.x) * cloud, b0.y + (b1.y - b0.y) * cloud, b0.z + (b1.z - b0.z) * cloud};
        for (int i = 0; i < 3; i++) {
            out[i] = std::fmin(hi[i] + (lo[i] - hi[i]) * low_weight, 1.0f);
        }
    };
    blend(la.DiffuseColour(), lb.DiffuseColour(), ha.DiffuseColour(), hb.DiffuseColour(), f.diffuse);
    blend(la.AmbientColour(), lb.AmbientColour(), ha.AmbientColour(), hb.AmbientColour(), f.ambient);
    f.diffuse[3] = 1.0f;
    f.ambient[3] = 4.0f;
    {
        // SpecularColour (TimeOfDay +0x40, not clamped); w is set by
        // SetupFrameLighting (Rain +0x36A0, the road reflection strength).
        const UMath::Vector4 &a0 = la.SpecularColour(), &a1 = lb.SpecularColour(), &b0 = ha.SpecularColour(), &b1 = hb.SpecularColour();
        const float lo[3] = {a0.x + (a1.x - a0.x) * cloud, a0.y + (a1.y - a0.y) * cloud, a0.z + (a1.z - a0.z) * cloud};
        const float hi[3] = {b0.x + (b1.x - b0.x) * cloud, b0.y + (b1.y - b0.y) * cloud, b0.z + (b1.z - b0.z) * cloud};
        for (int i = 0; i < 3; i++) {
            f.specular[i] = hi[i] + (lo[i] - hi[i]) * low_weight;
        }
        f.specular[3] = 0.0f;
        f.specular_power = 11.0f; // 0x8FADC0
    }
    {
        float lo = la.CarSpecScale() + (lb.CarSpecScale() - la.CarSpecScale()) * cloud;
        float hi = ha.CarSpecScale() + (hb.CarSpecScale() - ha.CarSpecScale()) * cloud;
        gCarSpecScale = hi + (lo - hi) * low_weight;
    }
    f.lit = 1;
    static const bool trace = std::getenv("NFSMW_TRACE_FOG") != nullptr;
    static int frame;
    if (trace && frame++ % 300 == 0) {
        std::fprintf(stderr, "[tod] cloud %.2f low weight %.2f diffuse (%.3f %.3f %.3f) ambient (%.3f %.3f %.3f) x4\n", cloud, low_weight, f.diffuse[0],
                     f.diffuse[1], f.diffuse[2], f.ambient[0], f.ambient[1], f.ambient[2]);
    }
}

bool gHaveSun = false;
V3 gSunDirection;

// sub_6C60F0, the IDI_SKYBOX_FX begin: the three SKY_TEXTURE_MISC textures,
// CloudScroll (0x988548: once per world frame += TimeOfDay sun speed (+0x0)
// x 0.5 x WorldTimeElapsed x 0.04 (0x8ADFDC)) and CloudIntensity from the
// view's Rain cloud intensity c (+0x290; 0 counts as 0.1):
// (min(3.333 c, 1), min(2 c^2, 0.8), min(c^2, 0.8)), each at least 0.
float gCloudScroll;

void SetSky(float cloud) {
    static const char *const kNames[3] = {"SKY_NEXGEN_CAP_A", "SKY_NEXGEN_CLOUDS_B", "SKY_NEXGEN_CAP_B"};
    static unsigned int hashes[3];
    PortSky sky = {};
    for (int i = 0; i < 3; i++) {
        if (hashes[i] == 0) {
            hashes[i] = bStringHash(kNames[i]);
        }
        sky.misc[i] = MetalTextureOf(GetTextureInfo(hashes[i], 0, 0));
    }
    gCloudScroll += gSun.speed * 0.5f * WorldTimeElapsed * 0.04f;
    sky.cloud_scroll = gCloudScroll;
    float c = cloud == 0.0f ? 0.1f : cloud;
    auto clamp = [](float v, float hi) { return v > 0.0f ? (v < hi ? v : hi) : 0.0f; };
    sky.cloud_intensity[0] = clamp(3.3333333f * c, 1.0f);
    sky.cloud_intensity[1] = clamp(2.0f * c * c, 0.8f);
    sky.cloud_intensity[2] = clamp(c * c, 0.8f);
    sky.cloud_intensity[3] = 0.0f;
    PortMetal3DSetSky(&sky);
}

// Rain +0x36A0 (RoadDampness), the road reflection strength (SpecularColour.w
// of the world effects, sub_6D6AA0 / sub_6D71F0): Rain::UpdateAndRender
// (RainNative.cpp) eases it while it rains; the effect begins read the
// current view's (view 1's in the player and reflection views).
float RoadDampness(eView *view) {
    return view != nullptr && view->Precipitation != nullptr ? view->Precipitation->GetRoadDampness() : 0.0f;
}

void SetupFrameLighting(eView *view) {
    V3 sun = TickSunDirection();
    gSunDirection = sun;
    gHaveSun = true;
    PortWorldFog f = {};
    SetWorldFog(sun, f);
    float cloud = view && view->Precipitation ? view->Precipitation->GetCloudIntensity() : 0.0f;
    SetTimeOfDayLighting(sun, cloud, f);
    // WORLDREFLECT's specular rgb is also scaled by 1 - the Rain +0x290
    // cloud intensity (sub_6D71F0).
    f.specular[3] = RoadDampness(view);
    f.reflect_specular_scale = 1.0f - cloud;
    PortMetal3DSetFog(&f);
    SetSky(cloud);
}

} // namespace

// The time-of-day sun direction for elSetupLights' sun lights (PC reads
// TimeOfDayPointer +0x180); false until the first world frame.
bool PortGetSunDirection(float out[3]) {
    if (!gHaveSun) {
        return false;
    }
    out[0] = gSunDirection.x;
    out[1] = gSunDirection.y;
    out[2] = gSunDirection.z;
    return true;
}

namespace {

// The view's camera: world -> camera (D3D-style, camera y down) times a
// projection whose field of view is horizontal at 4:3 and kept vertical for
// wider windows (Hor+), into Metal clip space.
bool ViewProjectionFor(eView *view, M44 out) {
    Camera *camera = view->GetCamera();
    if (camera == nullptr) {
        return false;
    }
    float fov_h = camera->GetFieldOfView() * (2.0f * 3.1415927f / 65536.0f);
    if (fov_h <= 0.0f) {
        return false;
    }
    float znear = camera->GetNearZ() > 0.0f ? camera->GetNearZ() : 0.5f;
    float zfar = camera->GetFarZ() > znear ? camera->GetFarZ() : 12000.0f;
    float sx43 = 1.0f / std::tan(fov_h * 0.5f);
    float sy = sx43 * (4.0f / 3.0f);
    float sx = sy / PortMetalViewAspect();
    float a = zfar / (zfar - znear);
    float b = -znear * zfar / (zfar - znear);
    M44 proj = {sx, 0, 0, 0, 0, -sy, 0, 0, 0, 0, a, 1, 0, 0, b, 0};
    M44 world_to_camera;
    std::memcpy(world_to_camera, camera->GetWorldToCameraMatrix(), sizeof(M44));
    Multiply(out, world_to_camera, proj);
    return true;
}

// An env-map face view: the camera's 90 degree field of view on a square
// target (the cube face).
bool SquareViewProjectionFor(eView *view, M44 out) {
    Camera *camera = view->GetCamera();
    if (camera == nullptr) {
        return false;
    }
    float fov = camera->GetFieldOfView() * (2.0f * 3.1415927f / 65536.0f);
    if (fov <= 0.0f) {
        return false;
    }
    float znear = camera->GetNearZ() > 0.0f ? camera->GetNearZ() : 0.5f;
    float zfar = camera->GetFarZ() > znear ? camera->GetFarZ() : 100.0f;
    float s = 1.0f / std::tan(fov * 0.5f);
    float a = zfar / (zfar - znear);
    float b = -znear * zfar / (zfar - znear);
    M44 proj = {s, 0, 0, 0, 0, -s, 0, 0, 0, 0, a, 1, 0, 0, b, 0};
    M44 world_to_camera;
    std::memcpy(world_to_camera, camera->GetWorldToCameraMatrix(), sizeof(M44));
    Multiply(out, world_to_camera, proj);
    return true;
}

// Each view's world -> clip matrix for the frame (visibility tests).
const int kMaxViews = 22;
M44 gViewClip[kMaxViews];
bool gViewClipValid[kMaxViews];

void SetViewClip(eView *view, const M44 view_proj) {
    if (view->ID >= 0 && view->ID < kMaxViews) {
        std::memcpy(gViewClip[view->ID], view_proj, sizeof(M44));
        gViewClipValid[view->ID] = true;
    }
    // Focal length in pixels for eView::GetPixelSize (car/scenery LOD
    // choice): half the PC render width over tan(fov/2), at the video
    // options' resolution as PC renders at it (not the 640-wide FE screen,
    // which put the front-end car at ~100 px, below the 120 px LOD A swap).
    Camera *camera = view->GetCamera();
    float fov_h = camera->GetFieldOfView() * (2.0f * 3.1415927f / 65536.0f);
    int width = 640, height = 480;
    PCGetResolution(gPCGraphicsOptions.RacingResolution, &width, &height);
    view->H = (0.5f * static_cast<float>(width)) / std::tan(fov_h * 0.5f);
}

// Frustum test of local-space points against a view: all outside one clip
// plane -> not visible, all inside -> fully, otherwise partially.
eVisibleState ClassifyPoints(int view_id, const float (*points)[3], int count, const bMatrix4 *local_world) {
    if (view_id < 0 || view_id >= kMaxViews || !gViewClipValid[view_id]) {
        return EVISIBLESTATE_PARTIAL;
    }
    M44 world, mvp;
    if (local_world) {
        std::memcpy(world, local_world, sizeof(M44));
    } else {
        Identity(world);
    }
    Multiply(mvp, world, gViewClip[view_id]);
    unsigned outside_all = 0x3F, outside_any = 0;
    for (int i = 0; i < count; i++) {
        float c[4];
        for (int j = 0; j < 4; j++) {
            c[j] = points[i][0] * mvp[0 * 4 + j] + points[i][1] * mvp[1 * 4 + j] + points[i][2] * mvp[2 * 4 + j] + mvp[3 * 4 + j];
        }
        unsigned out = 0;
        out |= c[0] < -c[3] ? 1 : 0;
        out |= c[0] > c[3] ? 2 : 0;
        out |= c[1] < -c[3] ? 4 : 0;
        out |= c[1] > c[3] ? 8 : 0;
        out |= c[2] < 0.0f ? 16 : 0;
        out |= c[2] > c[3] ? 32 : 0;
        outside_all &= out;
        outside_any |= out;
    }
    if (outside_all != 0) {
        return EVISIBLESTATE_NOT;
    }
    return outside_any == 0 ? EVISIBLESTATE_FULL : EVISIBLESTATE_PARTIAL;
}

eVisibleState ClassifyBox(int view_id, const bVector3 *aabb_min, const bVector3 *aabb_max, const bMatrix4 *local_world) {
    float corners[8][3];
    for (int i = 0; i < 8; i++) {
        corners[i][0] = (i & 1) ? aabb_max->x : aabb_min->x;
        corners[i][1] = (i & 2) ? aabb_max->y : aabb_min->y;
        corners[i][2] = (i & 4) ? aabb_max->z : aabb_min->z;
    }
    return ClassifyPoints(view_id, corners, 8, local_world);
}

// ---------------------------------------------------------------------------
// PC shadow map (speed.exe 1.3, NVIDIA depth-texture path, shadow mode
// 0x982C08 = 1). ___eDisplayFrame calls sub_6E54E0 for the player view with
// Shadow Detail (0x901830) > 0; sub_6E44C0 then aims the shadow view (PC
// view 15, 0x919C70; here EVIEW_SHADOWMAP1) from the sun and fits a
// light-space perspective projection to the near part of the player's view.
// ---------------------------------------------------------------------------

void Transform3(const M44 m, const float p[3], float out[3], bool point) {
    for (int j = 0; j < 3; j++) {
        out[j] = p[0] * m[0 * 4 + j] + p[1] * m[1 * 4 + j] + p[2] * m[2 * 4 + j] + (point ? m[3 * 4 + j] : 0.0f);
    }
}

// D3DXMatrixPerspectiveLH / D3DXMatrixOrthoLH.
void PerspectiveLH(M44 m, float w, float h, float zn, float zf) {
    std::memset(m, 0, sizeof(M44));
    m[0] = 2.0f * zn / w;
    m[5] = 2.0f * zn / h;
    m[10] = zf / (zf - zn);
    m[11] = 1.0f;
    m[14] = zn * zf / (zn - zf);
}

void OrthoLH(M44 m, float w, float h, float zn, float zf) {
    std::memset(m, 0, sizeof(M44));
    m[0] = 2.0f / w;
    m[5] = 2.0f / h;
    m[10] = 1.0f / (zf - zn);
    m[14] = zn / (zn - zf);
    m[15] = 1.0f;
}

// bSin/bCos of an angle in 65536ths of a turn (the ftol'd argument).
float BangTan(float bangs) {
    unsigned short a = static_cast<unsigned short>(static_cast<int>(bangs));
    return std::tan(a * (2.0f * 3.1415927f / 65536.0f));
}

// sub_6E44C0's matrices: view_proj (world -> shadow clip, the shadow view's
// projection) and texture (view_proj times the texture bias, the effects'
// ShadowTransform before the model's world matrix).
//
// The shadow camera sits 2000 (0x8FAEB8) towards the sun from the player
// camera, looking at it (eCreateLookAtMatrix, z up); sub_6D7950 gives it a
// 10 degree (0x8FAEE0) field of view and near/far 1650/2250 (0x8FAEC0/C4).
// The focus volume is the player camera's frustum with near 0.01 and far 50
// (0x901AAC/0x901AB0), horizontal half-angle 0.75 fov / 2 (0x901AB4) and
// vertical fov h/w / 2 (0x901AB8 = 1); sub_6C8CD0 builds its planes and
// corners in camera space. In camera space, with L the sun direction
// (normalised), B = normalize(L x (0, 0, -1)) and C = normalize(L x B), the
// corners are rotated into (B, L, C); the eye goes 20.1 (0x901AA0 + 0.1)
// behind their near z, centred in x and y; a perspective along C with
// width 2 * max|x/z| * 20.1 * 1.5 (0x901A94), height 2 * max|y/z| * 20.1 *
// 10 (0x901A98), near 20.1, far max z * 10 (0x901A9C) follows, then the
// fixed swap (x, z - 0.5w, 1.5w - y) and D3DXMatrixOrthoLH(2, 1, 0.5, 2.5)
// (0x901A84..90) so the light direction becomes the map's depth. The bias is
// (0.5x + 0.5w, -0.5y + 0.5w, z) (PC adds 1/2048 to both for D3D9's
// half-texel; Metal samples texel centres without it).
bool ComputeShadowMatrices(eView *view, V3 sun, M44 view_proj, M44 texture) {
    Camera *camera = view->GetCamera();
    if (camera == nullptr) {
        return false;
    }
    // The shadow camera (culling and LOD use it, as on PC).
    eView *shadow_view = eGetView(EVIEW_SHADOWMAP1, false);
    Camera *shadow_camera = shadow_view ? shadow_view->GetCamera() : nullptr;
    if (shadow_camera == nullptr) {
        return false;
    }
    bVector3 *centre = camera->GetPosition();
    bVector3 eye(centre->x + sun.x * 2000.0f, centre->y + sun.y * 2000.0f, centre->z + sun.z * 2000.0f);
    bVector3 target(centre->x, centre->y, centre->z);
    bVector3 up(0.0f, 0.0f, 1.0f);
    bMatrix4 look;
    eCreateLookAtMatrix(&look, eye, target, up);
    shadow_camera->SetCameraMatrix(look, 0.0f);
    shadow_camera->SetFieldOfView(static_cast<unsigned short>(static_cast<int>(10.0f * 65536.0f) / 360));
    shadow_camera->SetNearZ(1650.0f);
    shadow_camera->SetFarZ(2250.0f);

    float fov = camera->GetFieldOfView();
    float h_over_w = 1.0f / PortMetalViewAspect();
    float tan_x = BangTan(fov * 0.75f * 0.5f);
    float tan_y = BangTan(fov * h_over_w * 0.5f);
    const float znear = 0.01f, zfar = 50.0f;
    M44 v;
    std::memcpy(v, camera->GetWorldToCameraMatrix(), sizeof(M44));

    float l[3];
    const float s[3] = {sun.x, sun.y, sun.z};
    Transform3(v, s, l, false);
    V3 L = {l[0], l[1], l[2]};
    if (Dot(L, L) < 1e-12f) {
        return false;
    }
    L = Normalize(L);
    V3 B = {-L.y, L.x, 0.0f}; // L x (0, 0, -1)
    if (Dot(B, B) < 1e-12f) {
        return false; // sun along the view axis (PC doesn't guard)
    }
    B = Normalize(B);
    V3 C = Normalize(Cross(L, B));
    M44 r = {B.x, L.x, C.x, 0, B.y, L.y, C.y, 0, B.z, L.z, C.z, 0, 0, 0, 0, 1};

    float pts[8][3];
    for (int i = 0; i < 8; i++) {
        float z = (i & 4) ? zfar : znear;
        float p[3] = {(i & 1 ? 1.0f : -1.0f) * z * tan_x, (i & 2 ? 1.0f : -1.0f) * z * tan_y, z};
        Transform3(r, p, pts[i], true);
    }
    float lo[3] = {3.4e38f, 3.4e38f, 3.4e38f}, hi[3] = {-3.4e38f, -3.4e38f, -3.4e38f};
    for (auto &p : pts) {
        for (int j = 0; j < 3; j++) {
            lo[j] = std::fmin(lo[j], p[j]);
            hi[j] = std::fmax(hi[j], p[j]);
        }
    }
    const float back = 20.0f + 0.1f;
    float cx = (lo[0] + hi[0]) * 0.5f, cy = (lo[1] + hi[1]) * 0.5f, ez = lo[2] - back;
    float max_x = 0.0f, max_y = 0.0f, max_z = 0.0f;
    for (auto &p : pts) {
        float dz = p[2] - ez;
        if (dz > 0.0f) {
            max_x = std::fmax(max_x, std::fabs((p[0] - cx) / dz));
            max_y = std::fmax(max_y, std::fabs((p[1] - cy) / dz));
            max_z = std::fmax(max_z, dz);
        }
    }
    if (max_x <= 0.0f || max_y <= 0.0f || max_z <= back) {
        return false;
    }
    M44 t = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -cx, -cy, -ez, 1};
    M44 persp, ortho, swap = {1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, -0.5f, 1.5f, 1};
    PerspectiveLH(persp, 2.0f * max_x * back * 1.5f, 2.0f * max_y * back * 10.0f, back, max_z * 10.0f);
    OrthoLH(ortho, 2.0f, 1.0f, 0.5f, 2.5f);
    M44 m;
    Multiply(m, swap, ortho);
    Multiply(r, r, t);
    Multiply(r, r, persp);
    Multiply(r, r, m);
    Multiply(view_proj, v, r);
    M44 bias = {0.5f, 0, 0, 0, 0, -0.5f, 0, 0, 0, 0, 1, 0, 0.5f, 0.5f, 0, 1};
    Multiply(texture, view_proj, bias);
    return true;
}

// ___eDisplayFrame 0x6DE33E: InvShadowStrength fades in (0.3/s, 0x8F9340)
// while the player view is in a tunnel and out (2/s, 0x8F9344) after.
float gTunnelShadowFade;

void TickTunnelShadowFade(eView *view) {
    if (AmIinATunnel(view, 1) == 0) {
        gTunnelShadowFade = std::fmax(gTunnelShadowFade - 2.0f * WorldTimeElapsed, 0.0f);
    } else {
        gTunnelShadowFade = std::fmin(gTunnelShadowFade + 0.3f * WorldTimeElapsed, 1.0f);
    }
}

// Frontend: the garage backdrop and the player's car (PC ___eDisplayFrame,
// game flow state 3: GarageMainScreen::HandleRender(1), RenderFrontEndCars).
int gSceneDraws; // NFSMW_TRACE_SCENE
int gSceneCalls, gSceneNoSolid;
bool gSceneLog;
M44 gSceneViewProj;

bool RenderFrontEndScene() {
    static const bool trace = std::getenv("NFSMW_TRACE_SCENE") != nullptr;
    static int frame;
    bool log = trace && (frame++ % 120 == 0);
    GarageMainScreen *garage = GarageMainScreen::GetInstance();
    eView *view = eGetView(1, false);
    if (log) {
        Camera *c = view->GetCamera();
        if (garage) {
            std::fprintf(stderr, "[scene] garage visible %d\n", garage->IsVisable());
        }
        std::fprintf(stderr, "[scene] garage %p camera %p fov %u near %.2f far %.1f pos (%.2f %.2f %.2f)\n", (void *)garage, (void *)c,
                     c ? c->GetFieldOfView() : 0, c ? c->GetNearZ() : 0.0f, c ? c->GetFarZ() : 0.0f, c ? c->GetPosition()->x : 0.0f,
                     c ? c->GetPosition()->y : 0.0f, c ? c->GetPosition()->z : 0.0f);
    }
    if (garage == nullptr) {
        return false;
    }
    M44 view_proj;
    if (!ViewProjectionFor(view, view_proj)) {
        return false;
    }
    PortMetal3DBeginFrame(view_proj);
    SetupFrameLighting(view);
    SetViewClip(view, view_proj);
    // eDisplayFrame 0x6E736A; the frontend cars use the garage cube.
    if (PortEnvMapQuality() != 0) {
        UpdateEnvironmentMapCameras();
        PortMetal3DSetEnvMap(PORT_ENVMAP_STATIC, PortFEEnvMapCube());
    }
    {
        bVector3 *eye = view->GetCamera()->GetPosition();
        float e[3] = {eye->x, eye->y, eye->z};
        PortMetal3DSetEye(e);
        SetSceneCamera(view->GetCamera());
    }
    gSceneLog = log;
    std::memcpy(gSceneViewProj, view_proj, sizeof(M44));
    if (log) {
        Camera *c = view->GetCamera();
        std::fprintf(stderr, "[scene] dir (%.2f %.2f %.2f) target (%.2f %.2f %.2f)\n", c->GetDirection()->x, c->GetDirection()->y,
                     c->GetDirection()->z, c->GetTarget()->x, c->GetTarget()->y, c->GetTarget()->z);
    }
    static const bool on_top = std::getenv("NFSMW_SCENE_ON_TOP") != nullptr;
    PortMetal3DSetOverlay(on_top);
    gSceneDraws = 0;
    gSceneCalls = gSceneNoSolid = 0;
    // The frontend's shadow map holds no casters (no scenery or race cars;
    // the garage car draws through RenderFrontEndCars), so the shadowed
    // techniques read lit: only their level matters here.
    gShadowLevelClip = view_proj;
    // eDisplayFrame 0x6DEF0F: the cars mirrored under the floor first (view
    // 1, reflection 1; none in the car lot), then the garage, whose
    // translucent floor shows them through, then the cars.
    RenderFrontEndCars(view, 1);
    garage->HandleRender(1);
    int backdrop = gSceneDraws;
    RenderFrontEndCars(view, 0);
    gShadowLevelClip = nullptr;
    // 0x6DF1CC: the motion blur runs in the frontend too (the garage camera
    // rarely moves fast enough for it).
    PortMotionBlurFrame(view);
    if (log) {
        std::fprintf(stderr, "[scene] draws: backdrop %d, cars %d (render calls %d, no solid %d)\n", backdrop, gSceneDraws - backdrop,
                     gSceneCalls, gSceneNoSolid);
        int wheels = 0;
        for (eSolid *sol = SolidList.GetHead(); sol != SolidList.EndOfList(); sol = sol->GetNext()) {
            if (std::strstr(sol->Name, "WHEEL") || std::strstr(sol->Name, "TIRE") || std::strstr(sol->Name, "RIM")) {
                if (wheels++ < 6) {
                    std::fprintf(stderr, "[scene]   wheel solid %s\n", sol->Name);
                }
            }
        }
        std::fprintf(stderr, "[scene] %d wheel/tire solids loaded\n", wheels);
        int materials = 0;
        for (eLightMaterial *m = LightMaterialList.GetHead(); m != LightMaterialList.EndOfList(); m = m->GetNext()) {
            materials++;
        }
        std::fprintf(stderr, "[scene] %d light materials\n", materials);
        TextureInfo *skin_loaded = GetTextureInfo(0x530b82b0, 0, 0);
        TextureInfo *skin_any = GetTextureInfo(0x530b82b0, 0, 1);
        std::fprintf(stderr, "[scene] DUMMY_SKIN1 loaded %p, known %p (%dx%d comp %d)\n", (void *)skin_loaded, (void *)skin_any,
                     skin_any ? skin_any->Width : 0, skin_any ? skin_any->Height : 0, skin_any ? skin_any->ImageCompressionType : -1);
        static bool dumped;
        if (!dumped) {
            dumped = true;
            int n = 0;
            for (eLightMaterial *m = LightMaterialList.GetHead(); m != LightMaterialList.EndOfList() && n < 12; m = m->GetNext(), n++) {
                const eLightMaterialData &d = m->feData;
                std::fprintf(stderr,
                             "[mat] %-24.28s diff %.2f*(%.2f %.2f %.2f)..%.2f*(%.2f %.2f %.2f) a %.2f..%.2f spec %.2f*(%.2f %.2f %.2f)..%.2f pow %.1f env %.2f..%.2f pow %.2f metal %.2f\n",
                             m->Name, d.DiffuseMinScale, d.DiffuseMinR, d.DiffuseMinG, d.DiffuseMinB, d.DiffuseMaxScale, d.DiffuseMaxR, d.DiffuseMaxG,
                             d.DiffuseMaxB, d.DiffuseMinA, d.DiffuseMaxA, d.SpecularMinScale, d.SpecularMinR, d.SpecularMinG, d.SpecularMinB,
                             d.SpecularMaxScale, d.SpecularPower, d.EnvmapMinScale, d.EnvmapMaxScale, d.EnvmapPower, d.MetallicScale);
            }
        }
        GarageCarLoader *loader = GetGarageCarLoader();
        std::fprintf(stderr, "[scene] garage loader: loading %d current %d switched %d\n", loader->IsThereALoadingRideInfo(),
                     loader->IsThereACurrentRideInfo(), loader->HasSwitched());
        for (FrontEndRenderingCar *c = FrontEndRenderingCarList.GetHead(); c != FrontEndRenderingCarList.EndOfList(); c = c->GetNext()) {
            std::fprintf(stderr, "[scene]   fe car %p render info %p visible %d\n", (void *)c, (void *)c->GetRenderInfo(), c->Visible);
        }
    }
    return true;
}

} // namespace

// eViewPlatInterface::Render(eModel *...): one model instance into the
// current 3D frame. Lighting and blending matrices come with the shaders.
void eViewPlatInterface::Render(eModel *model, bMatrix4 *local_to_world, eLightContext *light_context, uint32 flags,
                                bMatrix4 *blending_matricies) {
    gSceneCalls++;
    if (model == nullptr || model->GetSolid() == nullptr) {
        gSceneNoSolid++;
        return;
    }
    M44 world;
    if (local_to_world) {
        std::memcpy(world, local_to_world, sizeof(M44));
    } else {
        Identity(world);
    }
    // Per-instance texture swaps (car skins, decals, number plates): applied
    // to the solid's table for this draw and restored after.
    TextureInfo **replaced_storage[128];
    TextureInfo ***replaced = nullptr;
    int num_replacements = model->PortNumReplacementTextures();
    if (num_replacements > 0 && num_replacements <= 128) {
        replaced = replaced_storage;
        model->ApplyReplacementTextureTable(replaced);
    }
    const eDynamicLightContext *dynamic_lights = nullptr;
    if (light_context && light_context->Type == 0) {
        dynamic_lights = static_cast<const eDynamicLightContext *>(light_context);
    }
    DrawSolid(model->GetSolid(), world, dynamic_lights, flags);
    if (replaced) {
        model->RestoreReplacementTextureTable(replaced);
    }
    gSceneDraws++;
    if (gSceneLog && std::strstr(model->GetSolid()->Name, "TIRE")) {
        std::fprintf(stderr, "[scene]   %s world v3 (%.2f %.2f %.2f) w-col (%.2f %.2f %.2f %.2f)\n",
                     model->GetSolid()->Name, world[12], world[13], world[14], world[3], world[7], world[11], world[15]);
    }
    if (gSceneLog) {
        eSolid *s = model->GetSolid();
        float local[4] = {(s->AABBMinX + s->AABBMaxX) * 0.5f, (s->AABBMinY + s->AABBMaxY) * 0.5f, (s->AABBMinZ + s->AABBMaxZ) * 0.5f, 1.0f};
        M44 mvp;
        Multiply(mvp, world, gSceneViewProj);
        float clip[4];
        for (int j = 0; j < 4; j++) {
            clip[j] = local[0] * mvp[0 * 4 + j] + local[1] * mvp[1 * 4 + j] + local[2] * mvp[2 * 4 + j] + local[3] * mvp[3 * 4 + j];
        }
        std::fprintf(stderr, "[scene]   %s centre (%.1f %.1f %.1f) size %.1f -> clip (%.2f %.2f %.2f w %.2f)\n", s->Name, local[0], local[1], local[2],
                     s->AABBMaxX - s->AABBMinX, clip[0], clip[1], clip[2], clip[3]);
    }
}

eVisibleState eViewPlatInterface::GetVisibleStateGB(const bVector3 *aabb_min, const bVector3 *aabb_max, bMatrix4 *local_world) {
    return ClassifyBox(static_cast<eView *>(this)->ID, aabb_min, aabb_max, local_world);
}

eVisibleState eViewPlatInterface::GetVisibleStateSB(const bVector3 *aabb_min, const bVector3 *aabb_max, bMatrix4 *local_world) {
    return ClassifyBox(static_cast<eView *>(this)->ID, aabb_min, aabb_max, local_world);
}

eVisibleState eViewPlatInterface::GetVisibleStateSB(const bVector3 *position, bMatrix4 *local_world) {
    float point[1][3] = {{position->x, position->y, position->z}};
    return ClassifyPoints(static_cast<eView *>(this)->ID, point, 1, local_world);
}

// Builds this frame's 3D scene (published with the frontend).
void RenderWorldModels(eView *view, int exc_flag); // WorldModel.cpp
void DrawTestCars(eView *view, int reflection);     // CarRender.cpp
EVIEWMODE eGetCurrentViewMode();

namespace {

// The in-game world for player view 1, following speed.exe 1.3's frame:
// sub_6BFDD0 fills one SceneryCullInfo per active player view (sub_4FAE40:
// exclude flags; sub_6BFD00: facade matrix = rotation about Z by the view's
// heading, and the view) and runs GrandSceneryCullInfo::DoCulling; then
// ___eDisplayFrame (0x6DE300) stuffs the scenery (flags 0x20) and the world
// models for view 1. Sky, flares, particles and the post effects are not
// drawn yet.
GrandSceneryCullInfo gWorldCullInfo;

void SetupCullInfo(eView *view, SceneryCullInfo *info, unsigned int exclude) {
    int id = view->GetID();
    unsigned int flags = exclude | 0x10;
    if (id == 1 || id == 2) {
        flags = exclude | 0x12;
    }
    EVIEWMODE mode = eGetCurrentViewMode();
    if (mode == 4 || mode == 3) {
        flags |= 1;
    }
    if (id >= EVIEW_FIRST_ENVMAP && id <= EVIEW_LAST_ENVMAP) { // PC views 18..23
        flags |= 0x100;
    }
    if (flags & 0x1800) {
        flags |= 0x8000040;
    }
    if (flags & 0x1000) {
        flags |= 2;
    }
    if (id == 3) {
        flags |= 0x20;
    }
    // TODO(port): GRaceStatus::fObj race type 1 (drag) adds 4.
    bVector3 *dir = view->GetCamera()->GetDirection();
    bMatrix4 rot;
    eCreateRotationZ(&rot, bATan(dir->x, dir->y));
    info->FacadeMatrix = rot;
    info->pView = view;
    info->ExcludeFlags = flags;
}

// Fallback for when no camera mover drives view 1 (CameraAI only creates the
// chase camera once a local player with a simable exists): follow the first
// vehicle from behind, or sit behind the startup race's start grid
// (sim → world via eSwizzleWorldVector).
void PortDebugRaceCamera(eView *view) {
    static const bool off = std::getenv("NFSMW_NO_DEBUG_CAM") != nullptr;
    if (view->GetCameraMover() != nullptr) {
        return;
    }
    Camera *camera = view->GetCamera();
    if (!off && VehicleRenderConn::GetList().begin() != VehicleRenderConn::GetList().end()) {
        VehicleRenderConn *car = *VehicleRenderConn::GetList().begin();
        const bMatrix4 *m = car->GetBodyMatrix();
        const bVector4 *p = m != nullptr ? &m->v3 : nullptr;
        if (p != nullptr) {
            bVector3 fwd(m->v0.x, m->v0.y, 0.0f);
            float len = bLength(&fwd);
            if (len > 0.001f) {
                fwd.x /= len;
                fwd.y /= len;
            }
            bVector3 eye(p->x - fwd.x * 7.0f, p->y - fwd.y * 7.0f, p->z + 2.4f);
            bVector3 center(p->x + fwd.x * 4.0f, p->y + fwd.y * 4.0f, p->z + 0.8f);
            bVector3 up(0.0f, 0.0f, 1.0f);
            bMatrix4 cm;
            eCreateLookAtMatrix(&cm, eye, center, up);
            camera->SetCameraMatrix(cm, 0.0f);
            return;
        }
    }
    if (off || bLength(camera->GetPosition()) > 50.0f || !GRaceDatabase::Exists()) {
        return;
    }
    GRaceParameters *race = GRaceDatabase::Get().GetStartupRace();
    if (race == nullptr) {
        return;
    }
    UMath::Vector3 p, d;
    race->GetStartPosition(p);
    race->GetStartDirection(d);
    bVector3 sim_pos(p.x, p.y, p.z), sim_dir(d.x, d.y, d.z), pos, dir;
    eSwizzleWorldVector(sim_pos, pos);
    eSwizzleWorldVector(sim_dir, dir);
    bVector3 eye(pos.x - dir.x * 9.0f, pos.y - dir.y * 9.0f, pos.z + 3.0f);
    bVector3 center(pos.x + dir.x * 10.0f, pos.y + dir.y * 10.0f, pos.z + 1.0f);
    bVector3 up(0.0f, 0.0f, 1.0f);
    bMatrix4 m;
    eCreateLookAtMatrix(&m, eye, center, up);
    camera->SetCameraMatrix(m, 0.0f);
}

bool RenderWorldScene(int frame) {
    if (TheGameFlowManager.GetState() != GAMEFLOW_STATE_RACING) {
        return false;
    }
    static const bool trace = std::getenv("NFSMW_TRACE_SCENE") != nullptr;
    bool log = trace && (frame % 120 == 0);
    eView *view = eGetView(1, false);
    if (view == nullptr || view->GetCamera() == nullptr) {
        return false;
    }
    PortDebugRaceCamera(view);
    M44 view_proj;
    if (!ViewProjectionFor(view, view_proj)) {
        return false;
    }
    PortMetal3DBeginFrame(view_proj);
    SetupFrameLighting(view);
    SetViewClip(view, view_proj);
    bVector3 *eye = view->GetCamera()->GetPosition();
    float e[3] = {eye->x, eye->y, eye->z};
    PortMetal3DSetEye(e);
    SetSceneCamera(view->GetCamera());
    PortMetal3DSetOverlay(false);
    gSceneLog = log;
    std::memcpy(gSceneViewProj, view_proj, sizeof(M44));

    gWorldCullInfo.NumCullInfos = 0;
    SetupCullInfo(view, &gWorldCullInfo.SceneryCullInfos[gWorldCullInfo.NumCullInfos++], 0x6000);
    // The env-map views: sub_6BFDD0 culls each active one with 0x1100 at
    // Shadow Detail (0x901830) < 2, else 0x1000.
    const int shadow_detail = gPCGraphicsOptions.ShadowDetail;
    // The shadow view (sub_6BFDD0: exclude 0x4100 with Shadow Detail > 0).
    // PC culls it with last frame's matrices; here they're computed first.
    TickTunnelShadowFade(view);
    eView *shadow_view = shadow_detail > 0 ? eGetView(EVIEW_SHADOWMAP1, false) : nullptr;
    M44 shadow_proj, shadow_texture;
    if (shadow_view && !(gHaveSun && ComputeShadowMatrices(view, gSunDirection, shadow_proj, shadow_texture))) {
        shadow_view = nullptr;
    }
    if (shadow_view) {
        SetViewClip(shadow_view, shadow_proj);
        SetupCullInfo(shadow_view, &gWorldCullInfo.SceneryCullInfos[gWorldCullInfo.NumCullInfos++], 0x4100);
    }
    eEnvMap *envmap = PortEnvMapQuality() != 0 ? eGetEnvMap() : nullptr;
    if (envmap) {
        UpdateEnvironmentMapCameras(); // eDisplayFrame 0x6E736A
        PortMetal3DSetEnvMap(PORT_ENVMAP_DYNAMIC, nullptr);
        for (int i = 0; i < 6; i++) {
            eView *face = envmap->Views[i];
            M44 face_proj;
            if (face && face->IsActive() && SquareViewProjectionFor(face, face_proj)) {
                SetViewClip(face, face_proj);
                SetupCullInfo(face, &gWorldCullInfo.SceneryCullInfos[gWorldCullInfo.NumCullInfos++], shadow_detail < 2 ? 0x1100 : 0x1000);
            }
        }
    }
    // The road reflection view (PC view 4, sub_6BFDD0): culled with exclude
    // 0x800 (scenery mirrored about each instance's pivot, Scenery.cpp) when
    // Road Reflection Detail is on and the road is wet (Rain +0x36A0 > 0.01).
    const int road_reflection = gPCGraphicsOptions.RoadReflectionEnable;
    const bool wet = RoadDampness(view) > 0.01f; // 0x890DC4
    eView *reflection_view = road_reflection != 0 ? eGetView(EVIEW_PLAYER1_GLOW, false) : nullptr;
    if (reflection_view && !reflection_view->IsActive()) {
        reflection_view = nullptr;
    }
    if (reflection_view && wet) {
        SetViewClip(reflection_view, view_proj);
        SetupCullInfo(reflection_view, &gWorldCullInfo.SceneryCullInfos[gWorldCullInfo.NumCullInfos++], 0x800);
    }
    gWorldCullInfo.DoCulling();
    gSceneDraws = 0;
    gSceneCalls = gSceneNoSolid = 0;
    int reflection_draws = 0;
    if (reflection_view) {
        // ___eDisplayFrame 0x6DE3FA, in its render target 1 (sub_6BD3B0):
        // the reflected sky layer (StuffSkyLayer(view 4, 4, ...)) at every
        // level; above Road Reflection Detail 1, when wet, the scenery
        // (stuff flags 8: not the 0x80 instances) and the cars mirrored
        // (sub_750B10(view 4, 1)); at detail 3 in a race the target is
        // blurred (sub_6D4960), then the light flares are drawn into it
        // (eRenderWorldLightFlares type 2, RenderVehicleFlares,
        // eRenderLightFlarePool).
        PortMetal3DBeginReflection(view_proj);
        StuffSkyLayer(reflection_view, SKY_LAYER_REFLECTION);
        if (road_reflection > 1 && wet) {
            gWorldCullInfo.StuffScenery(reflection_view, 8);
            VehicleRenderConn::RenderAll(reflection_view, 1);
        }
        PortRenderViewFlares(reflection_view, PORT_PRELIT_PART_REFLECTION);
        PortMetal3DEndReflection();
        PortMetal3DSetReflection(1, road_reflection == 3 && wet);
        reflection_draws = gSceneDraws;
        gSceneDraws = 0;
    } else {
        PortMetal3DSetReflection(road_reflection != 0 ? 1 : 0, false);
    }
    int envmap_draws = 0;
    if (envmap) {
        // ___eDisplayFrame 0x6DE675: each active face gets the sky and the
        // scenery (0x6DE7A4): stuff flags 0 with Car Reflection Detail
        // (0x9017BC) on, else 0x1000 at Shadow Detail < 2, else none; then
        // its light flares.
        const int reflection_detail = gPCGraphicsOptions.CarEnvironmentMapUpdateData;
        for (int i = 0; i < 6; i++) {
            eView *face = envmap->Views[i];
            M44 face_proj;
            if (face && face->IsActive() && SquareViewProjectionFor(face, face_proj)) {
                bVector3 *p = face->GetCamera()->GetPosition();
                float face_eye[3] = {p->x, p->y, p->z};
                PortMetal3DBeginEnvFace(i, face_proj, face_eye);
                // 0x6DE741: StuffSkyLayer(face, SKY_LAYER_LOWREZ, ...).
                StuffSkyLayer(face, SKY_LAYER_LOWREZ);
                if (reflection_detail != 0) {
                    gWorldCullInfo.StuffScenery(face, 0);
                } else if (shadow_detail < 2) {
                    gWorldCullInfo.StuffScenery(face, 0x1000);
                }
                PortRenderViewFlares(face, PORT_PRELIT_PART_ENV_FACE + i);
                PortMetal3DEndEnvFace();
            }
        }
        envmap_draws = gSceneDraws;
        gSceneDraws = 0;
    }
    int shadow_draws = 0;
    if (shadow_view) {
        // sub_6E44C0: the world models, the scenery (stuff flags 0x2000 at
        // Shadow Detail 1, else 0x200; 0x8FAECC = 1), the cars (the view
        // anchor's, then every other loaded one: all of them) and the
        // sections' shadow-map meshes (0x6E535B, sub_6D7F70;
        // ShadowMapMeshNative.cpp). TODO(port): the caster test against the
        // narrower frustum and its sweep along the light (sub_6D7A40) is not
        // ported.
        PortMetal3DBeginShadow(shadow_proj, shadow_detail);
        RenderWorldModels(shadow_view, 0);
        gWorldCullInfo.StuffScenery(shadow_view, shadow_detail == 1 ? 0x2000 : 0x200);
        VehicleRenderConn::RenderAll(shadow_view, 0);
        PortRenderShadowMapMeshes();
        PortMetal3DEndShadow();
        PortMetal3DSetShadowTransform(shadow_texture, gTunnelShadowFade);
        shadow_draws = gSceneDraws;
        gSceneDraws = 0;
    }
    gShadowLevelClip = view_proj;
    gWorldCullInfo.StuffScenery(view, 0x20);
    int scenery = gSceneDraws;
    RenderWorldModels(view, 0);
    int models = gSceneDraws - scenery;
    PortRenderSkids(view);
    DrawTestCars(view, 0); // ___eDisplayFrame 0x6DEE44: sub_750B10 → VehicleRenderConn::RenderAll
    gShadowLevelClip = nullptr;
    // 0x6DEFCE / 0x6DF1CC: the tunnel glare quad, then the motion blur over
    // the frame so far.
    PortTunnelGlareFrame(view);
    PortMotionBlurFrame(view);
    PortRenderWorldEffects(view);
    // ___eDisplayFrame 0x6DF4F7: with the Particle option (0x9017EC) on, the
    // particles (EmitterSystem::Render, not ported) and then in a race, in
    // the IDI_PARTICLES_FX effect with z writes off, player 1's
    // Rain::UpdateAndRender: the weather, the rain streaks and the drops'
    // state (their pass: PortRainDropsFrame, sub_6D4620).
    if (gPCGraphicsOptions.ParticleSystemEnable != 0 && view->Precipitation != nullptr) {
        view->Precipitation->UpdateAndRender();
    }
    PortRainDropsFrame();
    if (log) {
        std::fprintf(stderr, "[world] cars %d, env map faces %d draws, shadow map %d, reflection %d (strength %.2f)\n", gSceneDraws - scenery - models,
                     envmap_draws, shadow_draws, reflection_draws, RoadDampness(view));
        std::fprintf(stderr, "[world] camera (%.1f %.1f %.1f) dir (%.2f %.2f %.2f) draws: scenery %d, models %d (calls %d, no solid %d)\n",
                     eye->x, eye->y, eye->z, view->GetCamera()->GetDirection()->x, view->GetCamera()->GetDirection()->y,
                     view->GetCamera()->GetDirection()->z, scenery, gSceneDraws - scenery, gSceneCalls, gSceneNoSolid);
    }
    return true;
}

} // namespace

void PortRenderScene(int frame) {
    static const bool trace = std::getenv("NFSMW_TRACE_SCENE") != nullptr;
    struct Timer {
        bool on;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        ~Timer() {
            static double sum;
            static int n;
            if (!on) {
                return;
            }
            sum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (++n == 120) {
                std::fprintf(stderr, "[scene] build %.2f ms/frame\n", sum / n);
                sum = 0;
                n = 0;
            }
        }
    } timer{trace};
    M44 view_proj;
    Identity(view_proj);
    if (SmokeSetup(frame, view_proj)) {
        PortMetal3DBeginFrame(view_proj);
        PortMetal3DSetOverlay(true);
        SmokeDraw();
        return;
    }
    if (RenderWorldScene(frame)) {
        return;
    }
    if (!RenderFrontEndScene()) {
        PortMetal3DBeginFrame(view_proj);
    }
}
