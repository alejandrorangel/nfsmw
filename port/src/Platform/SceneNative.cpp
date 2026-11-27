// Native 3D scene submission (the world/frontend views of the PC eDisplayFrame).
//
// For now this only hosts the model smoke test (NFSMW_SMOKE_CAR /
// NFSMW_SMOKE_SOLIDS, below), drawn over the frontend with an orbiting camera.
// It checks solid loading, textures and the Metal 3D path before the car
// viewer (which places the real frontend car) is ported.

#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/EcstasyData.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Misc/ResourceLoader.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include "../Mac/MetalBridge.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

TextureInfo *PortSolidTexture(eSolid *solid, int index); // SolidNative.cpp

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

void DrawSolid(eSolid *solid, const M44 world) {
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
        d.num_indices = group.NumIndices;
        if (texture && texture->AlphaBlendType == TEXBLEND_ADDATIVE) {
            d.flags = PORT_3D_ADDITIVE;
        } else if (texture && texture->AlphaUsageType == TEXUSAGE_MODULATED) {
            d.flags = PORT_3D_ALPHA_BLEND;
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

// Builds this frame's 3D scene (published with the frontend).
void PortRenderScene(int frame) {
    M44 view_proj;
    Identity(view_proj);
    bool smoke = SmokeSetup(frame, view_proj);
    PortMetal3DBeginFrame(view_proj);
    if (smoke) {
        PortMetal3DSetOverlay(true);
        SmokeDraw();
    }
}
