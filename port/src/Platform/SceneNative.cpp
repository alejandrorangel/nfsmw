// Native 3D scene submission (the world/frontend views of the PC eDisplayFrame).
//
// For now this only hosts the model smoke test (NFSMW_SMOKE_CAR /
// NFSMW_SMOKE_SOLIDS, below), drawn over the frontend with an orbiting camera.
// It checks solid loading, textures and the Metal 3D path before the car
// viewer (which places the real frontend car) is ported.

#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/EcstasyData.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Ecstasy/eLight.hpp"
#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Frontend/FECarLoader.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Safehouse/FEPkg_GarageMain.hpp"
#include "Speed/Indep/Src/World/Car.hpp"
#include "Speed/Indep/Src/Misc/ResourceLoader.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include "../Mac/MetalBridge.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

TextureInfo *PortSolidTexture(eSolid *solid, int index); // SolidNative.cpp
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

// Car effect constants from a light material (see Port3DDraw::material).
void MaterialConstants(const eLightMaterial *light_material, float out[20]) {
    if (light_material == nullptr) {
        const float defaults[20] = {1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 8};
        std::memcpy(out, defaults, sizeof(defaults));
        return;
    }
    const eLightMaterialData &d = light_material->feData;
    float dmin[4] = {d.DiffuseMinScale * d.DiffuseMinR, d.DiffuseMinScale * d.DiffuseMinG, d.DiffuseMinScale * d.DiffuseMinB, d.DiffuseMinA};
    float dmax[4] = {d.DiffuseMaxScale * d.DiffuseMaxR, d.DiffuseMaxScale * d.DiffuseMaxG, d.DiffuseMaxScale * d.DiffuseMaxB, d.DiffuseMaxA};
    float smin[4] = {d.SpecularMinScale * d.SpecularMinR, d.SpecularMinScale * d.SpecularMinG, d.SpecularMinScale * d.SpecularMinB, 0};
    float smax[4] = {d.SpecularMaxScale * d.SpecularMaxR, d.SpecularMaxScale * d.SpecularMaxG, d.SpecularMaxScale * d.SpecularMaxB, 0};
    for (int i = 0; i < 4; i++) {
        out[i] = dmin[i];
        out[4 + i] = dmax[i] - dmin[i];
        out[8 + i] = smin[i];
        out[12 + i] = smax[i] - smin[i];
    }
    out[16] = d.EnvmapMinScale;
    out[17] = d.EnvmapMaxScale - d.EnvmapMinScale;
    out[18] = d.EnvmapPower;
    out[19] = d.SpecularPower;
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
        d.effect = group.Effect;
        int light_index = group.TextureSlots[5];
        const eLightMaterial *light_material = nullptr;
        if (light_index < solid->NumLightMaterials && solid->LightMaterialTable) {
            light_material = solid->LightMaterialTable[light_index].LightMaterial;
        }
        MaterialConstants(light_material, d.material);
        d.num_indices = group.NumIndices;
        if (texture && texture->AlphaBlendType == TEXBLEND_ADDATIVE) {
            d.flags = PORT_3D_ADDITIVE;
        } else if (texture && texture->AlphaUsageType == TEXUSAGE_MODULATED) {
            d.flags = PORT_3D_ALPHA_BLEND;
        } else if (texture && texture->AlphaUsageType == TEXUSAGE_PUNCHTHRU) {
            d.flags = PORT_3D_ALPHA_TEST;
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

// Each view's world -> clip matrix for the frame (visibility tests).
const int kMaxViews = 22;
M44 gViewClip[kMaxViews];
bool gViewClipValid[kMaxViews];

void SetViewClip(eView *view, const M44 view_proj) {
    if (view->ID >= 0 && view->ID < kMaxViews) {
        std::memcpy(gViewClip[view->ID], view_proj, sizeof(M44));
        gViewClipValid[view->ID] = true;
    }
    // Focal length in pixels for eView::GetPixelSize (480-line screen).
    Camera *camera = view->GetCamera();
    float fov_h = camera->GetFieldOfView() * (2.0f * 3.1415927f / 65536.0f);
    view->H = 320.0f / std::tan(fov_h * 0.5f);
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
    SetViewClip(view, view_proj);
    {
        bVector3 *eye = view->GetCamera()->GetPosition();
        float e[3] = {eye->x, eye->y, eye->z};
        PortMetal3DSetEye(e);
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
    garage->HandleRender(1);
    int backdrop = gSceneDraws;
    RenderFrontEndCars(view, 0);
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
    DrawSolid(model->GetSolid(), world);
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
void PortRenderScene(int frame) {
    M44 view_proj;
    Identity(view_proj);
    if (SmokeSetup(frame, view_proj)) {
        PortMetal3DBeginFrame(view_proj);
        PortMetal3DSetOverlay(true);
        SmokeDraw();
        return;
    }
    if (!RenderFrontEndScene()) {
        PortMetal3DBeginFrame(view_proj);
    }
}
