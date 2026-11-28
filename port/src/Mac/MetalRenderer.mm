#import "MetalRenderer.h"

#include "MetalBridge.h"
#import "BenchmarkMac.h"
#include "../Platform/Benchmark.h"
#include "MetalPrelitBridge.h"
#include "MetalUpload.h"

#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

// ---------------------------------------------------------------------------
// Bridge for the game's platform layer (MetalBridge.h). Called from the game
// thread; Metal resource creation is thread-safe.
// ---------------------------------------------------------------------------
static id<MTLDevice> gDevice;
static std::mutex gDebugLock;
static id<MTLTexture> gDebugTexture;

static MTLPixelFormat ToMetal(PortPixelFormat f) {
    switch (f) {
    case PORT_PIXEL_BGRA8:
        return MTLPixelFormatBGRA8Unorm;
    case PORT_PIXEL_BC1:
        return MTLPixelFormatBC1_RGBA;
    case PORT_PIXEL_BC2:
        return MTLPixelFormatBC2_RGBA;
    case PORT_PIXEL_BC3:
        return MTLPixelFormatBC3_RGBA;
    case PORT_PIXEL_B5G6R5:
        return MTLPixelFormatB5G6R5Unorm;
    case PORT_PIXEL_BGR5A1:
        return MTLPixelFormatBGR5A1Unorm;
    case PORT_PIXEL_ABGR4:
        return MTLPixelFormatABGR4Unorm;
    }
    return MTLPixelFormatInvalid;
}

void *PortMetalCreateTexture(int width, int height, int mip_levels, PortPixelFormat format, const char *label) {
    if (gDevice == nil) {
        return nullptr;
    }
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:ToMetal(format)
                                                                                    width:width
                                                                                   height:height
                                                                                mipmapped:NO];
    desc.mipmapLevelCount = mip_levels > 0 ? mip_levels : 1;
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [gDevice newTextureWithDescriptor:desc];
    if (label) {
        texture.label = [NSString stringWithUTF8String:label];
    }
    return (__bridge_retained void *)texture;
}

void PortMetalUploadMip(void *texture, int level, int width, int height, const void *data, size_t bytes_per_row) {
    id<MTLTexture> t = (__bridge id<MTLTexture>)texture;
    [t replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:level withBytes:data bytesPerRow:bytes_per_row];
}

void PortMetalReleaseTexture(void *texture) {
    id<MTLTexture> t = (__bridge_transfer id<MTLTexture>)texture;
    std::lock_guard<std::mutex> guard(gDebugLock);
    if (gDebugTexture == t) {
        gDebugTexture = nil;
    }
    (void)t; // released when it goes out of scope
}

void *PortMetalCreateBuffer(const void *data, size_t length, const char *label) {
    if (gDevice == nil || length == 0) {
        return nullptr;
    }
    id<MTLBuffer> buffer = [gDevice newBufferWithBytes:data length:length options:MTLResourceStorageModeShared];
    if (label) {
        buffer.label = [NSString stringWithUTF8String:label];
    }
    return (__bridge_retained void *)buffer;
}

const void *PortMetalBufferContents(void *buffer) {
    return buffer ? [(__bridge id<MTLBuffer>)buffer contents] : nullptr;
}

void PortMetalReleaseBuffer(void *buffer) {
    if (buffer) {
        id<MTLBuffer> b = (__bridge_transfer id<MTLBuffer>)buffer;
        (void)b;
    }
}

void PortMetalSetDebugTexture(void *texture) {
    std::lock_guard<std::mutex> guard(gDebugLock);
    gDebugTexture = (__bridge id<MTLTexture>)texture;
}

// ---------------------------------------------------------------------------
// Frontend frames: built on the game thread, drawn by the renderer.
// ---------------------------------------------------------------------------
struct FEBatch {
    id<MTLTexture> texture; // nil = untextured
    id<MTLTexture> mask;    // nil = unmasked
    PortBlendMode blend;
    uint32_t first;
    uint32_t count;
};

struct FEFrame {
    std::vector<PortFEVertex> vertices;
    std::vector<FEBatch> batches;
    int quads = 0;
};

static FEFrame gFEBuilding;
static FEFrame gFEPublished;
static std::mutex gFELock;

// How the FE virtual screen was last placed in the view (fractions of the view).
struct FEMapping {
    float vp_x, vp_y, vp_w, vp_h; // viewport
    float scale_x, scale_y;       // NDC scale inside it
};
static std::mutex gFEMappingLock;
static FEMapping gFEMapping = {0, 0, 1, 1, 1, 1};

void PortMetalViewToFEScreen(float u, float v, float *x, float *y) {
    FEMapping m;
    {
        std::lock_guard<std::mutex> guard(gFEMappingLock);
        m = gFEMapping;
    }
    float ndc_x = ((u - m.vp_x) / m.vp_w) * 2.0f - 1.0f;
    float ndc_y = 1.0f - ((v - m.vp_y) / m.vp_h) * 2.0f;
    ndc_x /= m.scale_x;
    ndc_y /= m.scale_y;
    *x = (ndc_x + 1.0f) * 0.5f * 640.0f;
    *y = (1.0f - ndc_y) * 0.5f * 480.0f;
}

void PortMetalFEBeginFrame() {
    gFEBuilding.vertices.clear();
    gFEBuilding.batches.clear();
    gFEBuilding.quads = 0;
}

void PortMetalFEAddQuad(void *texture, PortBlendMode blend, const PortFEVertex quad[4]) {
    PortMetalFEAddMaskedQuad(texture, nullptr, blend, quad);
}

void PortMetalFEAddMaskedQuad(void *texture, void *mask, PortBlendMode blend, const PortFEVertex quad[4]) {
    id<MTLTexture> t = (__bridge id<MTLTexture>)texture;
    id<MTLTexture> m = (__bridge id<MTLTexture>)mask;
    FEFrame &f = gFEBuilding;
    if (f.batches.empty() || f.batches.back().texture != t || f.batches.back().mask != m || f.batches.back().blend != blend) {
        f.batches.push_back({t, m, blend, (uint32_t)f.vertices.size(), 0});
    }
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i : order) {
        f.vertices.push_back(quad[i]);
    }
    f.batches.back().count += 6;
    f.quads++;
}

// Strong references, so a model unloaded mid-frame stays alive until drawn.
struct Draw3D {
    id<MTLBuffer> vertex_buffer;
    uint32_t vertex_stride;
    id<MTLBuffer> index_buffer;
    id<MTLTexture> texture;
    id<MTLTexture> texture2;
    id<MTLTexture> texture3;
    uint32_t first_index;
    uint32_t num_indices;
    uint32_t flags;
    uint32_t effect;
    float world[16];
    float material[20];
    float lights[28];
    float envmap[12];
    uint32_t shadow_level;
    const float *bones = nullptr; // WORLDBONE BlendMatrices (Frame3D::bones)
};

// One face of the dynamic environment cube (PC env-map views).
struct EnvFace3D {
    bool active = false; // drawn this frame
    float view_proj[16];
    float eye[3];
    std::vector<Draw3D> draws;
};

struct Frame3D {
    float view_proj[16];
    float eye[3] = {0, 0, 0};
    std::vector<Draw3D> draws;
    EnvFace3D env_faces[6];
    PortEnvMapMode env_mode = PORT_ENVMAP_NONE;
    id<MTLTexture> env_static;
    bool overlay = false;
    bool treatment = false; // apply the visual treatment (vt) to this frame
    PortVisualTreatment vt;
    id<MTLTexture> gain_map;
    PortWorldFog fog = {};
    bool overbright = false;
    PortOverBright ob = {};
    bool motion_blur = false; // PortMetal3DSetMotionBlur
    PortMotionBlur mb = {};
    bool x360_flares = false; // PortMetal3DSetX360Flares
    // IDI_SKYBOX_FX constants (PortMetal3DSetSky); textures held strongly.
    PortSky sky = {};
    id<MTLTexture> sky_misc[3];
    // PC shadow map pass (PortMetal3DBeginShadow).
    bool shadow_active = false;
    int shadow_detail = 1;
    float shadow_view_proj[16];
    std::vector<Draw3D> shadow_draws;
    bool shadow_transform_set = false;
    float shadow_transform[16];
    float inv_shadow_strength = 0;
    // PC road reflection (PortMetal3DBeginReflection).
    bool reflection_active = false;
    float reflection_view_proj[16];
    std::vector<Draw3D> reflection_draws;
    int reflection_mode = 0; // PortMetal3DSetReflection
    bool reflection_blur = false;
    // IDI_PARTICLES_FX quads (PortMetal3DAddParticles), as triangles.
    struct Particles {
        id<MTLTexture> texture;
        uint32_t flags;
        float world[16];
        uint32_t first, count; // vertices in particle_verts
    };
    std::vector<PortParticleVertex> particle_verts;
    std::vector<Particles> particles;
    // IDI_RAIN_DROP_FX drops (PortMetal3DSetRainDrops).
    std::vector<PortRainDrop> drops;
    id<MTLTexture> drop_displace;
    // WORLDBONE skinning palettes this frame (Draw3D::bones points in here;
    // a deque keeps them in place as it grows and across the frame swap).
    std::deque<std::array<float, 256>> bones;
};
static Frame3D g3DBuilding;
static Frame3D g3DPublished;
static uint64_t g3DPublishedSerial; // frames published (gFELock)
static std::vector<Draw3D> *g3DTarget = &g3DBuilding.draws; // where PortMetal3DAdd goes

static float gViewAspect = 4.0f / 3.0f;

float PortMetalViewAspect() {
    return gViewAspect;
}

void PortMetal3DBeginFrame(const float view_proj[16]) {
    memcpy(g3DBuilding.view_proj, view_proj, sizeof(g3DBuilding.view_proj));
    g3DBuilding.draws.clear();
    for (EnvFace3D &f : g3DBuilding.env_faces) {
        f.active = false;
        f.draws.clear();
    }
    g3DBuilding.env_mode = PORT_ENVMAP_NONE;
    g3DBuilding.env_static = nil;
    g3DTarget = &g3DBuilding.draws;
    g3DBuilding.overlay = false;
    g3DBuilding.treatment = false;
    g3DBuilding.gain_map = nil;
    g3DBuilding.fog = {};
    g3DBuilding.overbright = false;
    g3DBuilding.motion_blur = false;
    g3DBuilding.x360_flares = false;
    g3DBuilding.sky = {};
    for (int i = 0; i < 3; i++) {
        g3DBuilding.sky_misc[i] = nil;
    }
    g3DBuilding.shadow_active = false;
    g3DBuilding.shadow_draws.clear();
    g3DBuilding.shadow_transform_set = false;
    g3DBuilding.inv_shadow_strength = 0;
    g3DBuilding.reflection_active = false;
    g3DBuilding.reflection_draws.clear();
    g3DBuilding.reflection_mode = 0;
    g3DBuilding.reflection_blur = false;
    PortMetalPrelitBeginFrame();
    g3DBuilding.particle_verts.clear();
    g3DBuilding.particles.clear();
    g3DBuilding.drops.clear();
    g3DBuilding.drop_displace = nil;
    g3DBuilding.bones.clear();
}

void PortMetal3DAddParticles(void *texture, uint32_t flags, const float world[16], const PortParticleVertex *quads, int num_quads) {
    if (num_quads <= 0) {
        return;
    }
    Frame3D::Particles p;
    p.texture = (__bridge id<MTLTexture>)texture;
    p.flags = flags;
    memcpy(p.world, world, sizeof(p.world));
    p.first = (uint32_t)g3DBuilding.particle_verts.size();
    p.count = (uint32_t)num_quads * 6;
    static const int kOrder[6] = {0, 1, 2, 0, 2, 3};
    for (int q = 0; q < num_quads; q++) {
        for (int k : kOrder) {
            g3DBuilding.particle_verts.push_back(quads[q * 4 + k]);
        }
    }
    g3DBuilding.particles.push_back(std::move(p));
}

void PortMetal3DSetRainDrops(void *displace, const PortRainDrop *drops, int count) {
    g3DBuilding.drop_displace = (__bridge id<MTLTexture>)displace;
    g3DBuilding.drops.assign(drops, drops + count);
}

void PortMetal3DBeginReflection(const float view_proj[16]) {
    g3DBuilding.reflection_active = true;
    g3DBuilding.reflection_draws.clear();
    memcpy(g3DBuilding.reflection_view_proj, view_proj, sizeof(g3DBuilding.reflection_view_proj));
    g3DTarget = &g3DBuilding.reflection_draws;
}

void PortMetal3DEndReflection() {
    g3DTarget = &g3DBuilding.draws;
}

void PortMetal3DSetReflection(int mode, bool blur) {
    g3DBuilding.reflection_mode = mode;
    g3DBuilding.reflection_blur = blur;
}

// MSAA sample counts the GPU supports beyond 1 (2, 4, 8 in order).
static int gMSAACounts[3];
static int gNumMSAACounts = -1;
static std::atomic<int> gFSAALevel{0};

static void FindMSAACounts() {
    if (gNumMSAACounts >= 0) {
        return;
    }
    id<MTLDevice> device = gDevice ? gDevice : MTLCreateSystemDefaultDevice();
    gNumMSAACounts = 0;
    for (int c : {2, 4, 8}) {
        if ([device supportsTextureSampleCount:c]) {
            gMSAACounts[gNumMSAACounts++] = c;
        }
    }
}

int PortMetalMaxFSAALevel() {
    FindMSAACounts();
    return gNumMSAACounts;
}

int PortMetalFSAASamples(int level) {
    FindMSAACounts();
    if (level <= 0 || gNumMSAACounts == 0) {
        return 1;
    }
    return gMSAACounts[(level > gNumMSAACounts ? gNumMSAACounts : level) - 1];
}

void PortSetFSAALevel(int level) {
    gFSAALevel = level < 0 ? 0 : level;
}

void PortMetal3DSetSky(const PortSky *sky) {
    g3DBuilding.sky = *sky;
    for (int i = 0; i < 3; i++) {
        g3DBuilding.sky_misc[i] = (__bridge id<MTLTexture>)sky->misc[i];
        g3DBuilding.sky.misc[i] = nullptr; // held by sky_misc
    }
}

void PortMetal3DSetOverBright(const PortOverBright *overbright) {
    g3DBuilding.overbright = overbright != nullptr;
    if (overbright) {
        g3DBuilding.ob = *overbright;
    }
}

void PortMetal3DSetMotionBlur(const PortMotionBlur *blur) {
    g3DBuilding.motion_blur = blur != nullptr;
    if (blur) {
        g3DBuilding.mb = *blur;
        PortMetalPrelitSetPart(1);
    }
}

void PortMetal3DBeginX360Flares() {
    g3DBuilding.x360_flares = true;
    PortMetalPrelitSetPart(2);
}

void PortMetal3DEndX360Flares() {
    PortMetalPrelitSetPart(1);
}

void PortMetal3DBeginShadow(const float view_proj[16], int shadow_detail) {
    g3DBuilding.shadow_active = true;
    g3DBuilding.shadow_detail = shadow_detail;
    g3DBuilding.shadow_draws.clear();
    memcpy(g3DBuilding.shadow_view_proj, view_proj, sizeof(g3DBuilding.shadow_view_proj));
    g3DTarget = &g3DBuilding.shadow_draws;
}

void PortMetal3DEndShadow() {
    g3DTarget = &g3DBuilding.draws;
}

void PortMetal3DSetShadowTransform(const float shadow_transform[16], float inv_shadow_strength) {
    g3DBuilding.shadow_transform_set = true;
    memcpy(g3DBuilding.shadow_transform, shadow_transform, sizeof(g3DBuilding.shadow_transform));
    g3DBuilding.inv_shadow_strength = inv_shadow_strength;
}

void PortMetal3DSetFog(const PortWorldFog *fog) {
    static const bool no_fog = getenv("NFSMW_NO_FOG") != nullptr;
    static const bool no_light = getenv("NFSMW_NO_TOD_LIGHT") != nullptr;
    g3DBuilding.fog = {};
    if (fog) {
        g3DBuilding.fog = *fog;
        if (no_fog) {
            g3DBuilding.fog.enabled = 0;
        }
        if (no_light) {
            g3DBuilding.fog.lit = 0;
        }
    }
}

static std::atomic<int> gTextureFiltering{1};
void PortSetTextureFiltering(int level) {
    gTextureFiltering = level < 0 ? 0 : level > 2 ? 2 : level;
}

void PortMetal3DSetTreatment(const PortVisualTreatment *treatment) {
    g3DBuilding.treatment = treatment != nullptr;
    if (treatment) {
        g3DBuilding.vt = *treatment;
        g3DBuilding.gain_map = (__bridge id<MTLTexture>)treatment->gain_map;
        g3DBuilding.vt.gain_map = nullptr; // held by gain_map
    }
}

void PortMetal3DSetEnvMap(PortEnvMapMode mode, void *static_cube) {
    g3DBuilding.env_mode = mode;
    g3DBuilding.env_static = mode == PORT_ENVMAP_STATIC ? (__bridge id<MTLTexture>)static_cube : nil;
    if (mode == PORT_ENVMAP_STATIC && static_cube == nullptr) {
        g3DBuilding.env_mode = PORT_ENVMAP_NONE;
    }
}

void PortMetal3DBeginEnvFace(int face, const float view_proj[16], const float eye[3]) {
    if (face < 0 || face >= 6) {
        return;
    }
    EnvFace3D &f = g3DBuilding.env_faces[face];
    f.active = true;
    f.draws.clear();
    memcpy(f.view_proj, view_proj, sizeof(f.view_proj));
    memcpy(f.eye, eye, sizeof(f.eye));
    g3DTarget = &f.draws;
}

void PortMetal3DEndEnvFace() {
    g3DTarget = &g3DBuilding.draws;
}

// Blits level 0 of each face into a new cube, on its own queue (game thread).
void *PortMetalCreateCube(void *const faces[6], const char *label) {
    if (gDevice == nil || faces[0] == nullptr) {
        return nullptr;
    }
    static id<MTLCommandQueue> queue;
    static std::once_flag once;
    std::call_once(once, [] { queue = [gDevice newCommandQueue]; });
    id<MTLTexture> first = (__bridge id<MTLTexture>)faces[0];
    MTLTextureDescriptor *desc = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:first.pixelFormat
                                                                                     size:first.width
                                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> cube = [gDevice newTextureWithDescriptor:desc];
    if (cube == nil) {
        return nullptr;
    }
    if (label) {
        cube.label = [NSString stringWithUTF8String:label];
    }
    id<MTLCommandBuffer> cmd = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    for (int i = 0; i < 6; i++) {
        id<MTLTexture> f = (__bridge id<MTLTexture>)faces[i];
        if (f == nil || f.pixelFormat != first.pixelFormat || f.width != first.width || f.height != first.width) {
            continue;
        }
        [blit copyFromTexture:f sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(f.width, f.height, 1)
                    toTexture:cube destinationSlice:i destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    }
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    return (__bridge_retained void *)cube;
}

void PortMetal3DSetEye(const float eye[3]) {
    memcpy(g3DBuilding.eye, eye, sizeof(g3DBuilding.eye));
}

void PortMetal3DSetOverlay(bool on_top) {
    g3DBuilding.overlay = on_top;
}

void PortMetal3DAdd(const Port3DDraw *draw) {
    if (draw->vertex_buffer && draw->index_buffer && draw->num_indices) {
        Draw3D d;
        d.vertex_buffer = (__bridge id<MTLBuffer>)draw->vertex_buffer;
        d.vertex_stride = draw->vertex_stride;
        d.index_buffer = (__bridge id<MTLBuffer>)draw->index_buffer;
        d.texture = (__bridge id<MTLTexture>)draw->texture;
        d.texture2 = (__bridge id<MTLTexture>)draw->texture2;
        d.texture3 = (__bridge id<MTLTexture>)draw->texture3;
        d.first_index = draw->first_index;
        d.num_indices = draw->num_indices;
        d.flags = draw->flags;
        d.effect = draw->effect;
        memcpy(d.world, draw->world, sizeof(d.world));
        memcpy(d.material, draw->material, sizeof(d.material));
        memcpy(d.lights, draw->lights, sizeof(d.lights));
        memcpy(d.envmap, draw->envmap, sizeof(d.envmap));
        d.shadow_level = draw->shadow_level;
        if (draw->bones) {
            g3DBuilding.bones.emplace_back();
            memcpy(g3DBuilding.bones.back().data(), draw->bones, sizeof(float) * 256);
            d.bones = g3DBuilding.bones.back().data();
        }
        g3DTarget->push_back(std::move(d));
    }
}

int PortMetalFEEndFrame() {
    if (PortBenchmarkActive()) {
        PortMacBenchmarkBeforePublish(); // the benchmark draws every frame
    }
    std::lock_guard<std::mutex> guard(gFELock);
    std::swap(gFEPublished, gFEBuilding);
    std::swap(g3DPublished, g3DBuilding);
    g3DPublishedSerial++;
    PortMetalPrelitPublish();
    return gFEPublished.quads;
}

// What drawDraws reads of the renderer's per-frame state, taken when a list
// is encoded: the pass (sample count, colour write mask, ...), the sky, the
// shadow map and the reflection bound then. The env-map faces are encoded
// on a worker thread with what these were when they started (the last
// frame's shadow map and reflection, as on the main thread before).
// mesh_vs / mesh_fs's MeshConst.
struct MeshConstants {
    float mvp[16];
    float world[16];
    uint32_t stride;
    uint32_t shade;
    uint32_t effect;
    uint32_t pad;
    float eye[4];
    float view_x[4];
    float shadow[16];
    float inv_shadow;
    uint32_t shadow_level;
    uint32_t shadow_map;
    float shadow_texel;
};

struct PassState {
    int samples, mask;
    bool main_scene, skip_sky_alpha, only_sky_alpha, env_face;
    PortSky sky;
    id<MTLTexture> sky_misc[3];
    bool shadow_valid;
    id<MTLTexture> shadow_depth, shadow_colour;
    NSUInteger shadow_size;
    float shadow_transform[16];
    float inv_shadow_strength;
    id<MTLTexture> refl_bound;
};

// ---------------------------------------------------------------------------
// Renderer. Draws the latest frontend frame; until the game has drawn one, it
// shows the most recently loaded game texture, centred
// and aspect-correct, as a check of the texture pipeline.
// ---------------------------------------------------------------------------
static NSString *const kDebugShader = @R"(
#include <metal_stdlib>
using namespace metal;

struct VOut { float4 pos [[position]]; float2 uv; };

vertex VOut debug_vs(uint vid [[vertex_id]], constant float2 &scale [[buffer(0)]]) {
    float2 corners[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };
    VOut o;
    o.pos = float4(corners[vid] * scale, 0, 1);
    o.uv = float2((corners[vid].x + 1) * 0.5, 1 - (corners[vid].y + 1) * 0.5);
    return o;
}

struct FEVertex { packed_float2 pos; packed_float2 uv; uchar4 rgba; packed_float2 muv; };
struct FEOut { float4 pos [[position]]; float2 uv; float4 color; float2 muv; };

vertex FEOut fe_vs(uint vid [[vertex_id]], const device FEVertex *verts [[buffer(0)]], constant float2 &scale [[buffer(1)]]) {
    FEVertex v = verts[vid];
    FEOut o;
    o.pos = float4(float2(v.pos) * scale, 0, 1);
    o.uv = float2(v.uv);
    // Console-style colour: RGB 0x80 = 1.0 (the PC renderer used MODULATE2X).
    o.color = float4(float3(v.rgba.xyz) * (2.0 / 255.0), v.rgba.w / 255.0);
    o.muv = float2(v.muv);
    return o;
}

// IDI_FE_FX / IDI_FE_MASK_FX (ps #0: alpha also times the mask's alpha, the
// mask sampled clamped at the second UV set). Unmasked batches bind white.
fragment float4 fe_fs(FEOut in [[stage_in]], texture2d<float> tex [[texture(0)]], texture2d<float> mask [[texture(1)]]) {
    constexpr sampler s(filter::linear, mip_filter::linear, address::clamp_to_edge);
    constexpr sampler ms(filter::linear, mip_filter::linear, address::clamp_to_edge);
    float4 c = in.color * tex.sample(s, in.uv);
    c.a *= mask.sample(ms, in.muv).a;
    return c;
}

// 3D meshes. Vertices are pulled from the raw 36-byte PC layout.
// view_x: the camera's x axis in world space (first column of the view
// matrix, so dot(world normal, view_x) is the WorldView x of a model normal).
// shadow: model -> shadow texture (the effects' ShadowTransform), used when
// shadow_level > 0; shadow_map: a shadow map was drawn this frame (else
// every tap reads lit).
struct MeshConst { float4x4 mvp; float4x4 world; uint stride; uint shade; uint effect; uint pad; float4 eye; float4 view_x;
                   float4x4 shadow; float inv_shadow; uint shadow_level; uint shadow_map; float shadow_texel; };
struct MeshOut { float4 pos [[position]]; float2 uv; float4 color; float3 normal; float3 world_pos; float3 local_normal; float3 local_pos;
                 float4 d0; float4 d1; float3 inscatter; float2 uv1; float spec; float3 tlight; float3 tview; float4 shadow_pos;
                 float2 uv2; float4 clip; float3 env; };

// PortSky (the float part): CloudScroll, CloudIntensity.
struct SkyConst { float cloud_scroll; float pad[3]; float4 cloud_intensity; };

// PortWorldFog (MetalBridge.h).
struct WorldFog {
    float4 br_plus_bm, one_over_brbm, const1, const2, const3, light_dir;
    float fog_value;
    uint enabled;
    float4 ambient, diffuse;
    uint lit;
    float4 specular;
    float specular_power;
    float reflect_specular_scale;
};

// shade bit 7: the draw has the normal and specular maps and 60-byte
// vertices (tangent at float 11) the WORLDNORMALMAP/WORLDREFLECT shaders need.
bool NormalMapped(constant MeshConst &c) {
    return (c.effect == 1 || c.effect == 3) && (c.shade & 128) != 0;
}

// Effects with the PC scattering and AmbientColour/DiffuseColour lighting
// terms (IDI_WORLD/WORLDREFLECT/WORLDNORMALMAP/GLOSSYWINDOW/TREE_FX);
// WORLDBONE, CAR, WORLDMIN, WORLDNOFOG and WORLDPRELIT have none.
bool FoggedEffect(uint effect) {
    return effect == 0 || effect == 1 || effect == 3 || effect == 5 || effect == 6;
}

// IDI_WORLD_FX vs (e.g. shader #13): s = clamp(|v - eye| * FogValue, 0, 2500),
// cos = L . normalize(eye - v), E = exp(-s * (Br + Bm)), in-scatter =
// saturate((Const_1 (1 + cos^2) + Const_2 Const_3.x (Const_3.y + Const_3.z
// cos)^-1.5) / (Br + Bm) * (1 - E) * Const_3.w). PC works in model space
// (LocalEyePos, LocalLightVec); world space is the same for rigid transforms.
void Scatter(float3 world_pos, float3 eye, constant WorldFog &f, thread float3 &extinction, thread float3 &inscatter) {
    float3 to_eye = eye - world_pos;
    float dist = length(to_eye);
    float cos_t = dist > 0 ? dot(f.light_dir.xyz, to_eye / dist) : 0;
    float s = clamp(dist * f.fog_value, 0.0, 2500.0);
    float3 e = exp(-s * f.br_plus_bm.xyz);
    float mie = f.const3.x * pow(max(f.const3.y + f.const3.z * cos_t, 1e-6), -1.5);
    float3 in = (f.const1.xyz * (1 + cos_t * cos_t) + f.const2.xyz * mie) * f.one_over_brbm.xyz * (1 - e) * f.const3.w;
    extinction = min(e, 1.0);
    inscatter = saturate(in);
}

// Model-space lights for the car effect (eDynamicLightContext).
struct Lights {
    float4 dir[3];
    float4 colour[3];
    float4 eye;
};

// Shading constants per draw (see Port3DDraw::material).
struct Material {
    float4 diffuse_min, diffuse_range, specular_min, specular_range;
    float envmap_min, envmap_range, envmap_power, specular_power;
};

struct EnvMatrix { float4 row[3]; };

// IDI_CAR_FX vertex shaders, technique #0 'car' shader=1 (vs #11) and the
// shadowed techniques #1..#3 (vs #9/#7/#5, identical but for the outputs),
// instruction by instruction. v0 = local position, v1 = the vertex normal
// (as stored, not renormalised), v2 = the D3DCOLOR vertex colour (v2.x: red),
// c(LocalEyePos) = the camera in model space, LocalDirectionMatrix columns =
// the three light directions, LocalColourMatrix rows = their colours,
// WorldView = local_world x view (sub_6C8000). oD0/oD1 are colour registers:
// clamped to 0..1.
void CarVertex(thread MeshOut &o, float3 v0, float3 v1, float4 v2, constant MeshConst &c, constant Material &m, constant Lights &l,
               constant EnvMatrix &wv) {
    float3 eye_vec = l.eye.xyz - v0;                           // add r0.xyz, -v0, LocalEyePos
    float3 v = eye_vec * rsqrt(dot(eye_vec, eye_vec));         // dp3, rsq, mul: V
    float ndv = max(dot(v, v1), 0.01);                         // dp3, max r.x, c.x (0.01)
    float ao = clamp(v2.x + v2.x, 0.0, 1.0);                   // add r1.w, v2.x, v2.x; max 0; min 1
    // lit r0, (ndv, ndv, -, SpecularPower): r0.z = ndv^SpecularPower.
    float3 spec = pow(ndv, clamp(m.specular_power, -128.0, 128.0)) * m.specular_range.rgb + m.specular_min.rgb;
    // lit r0, (ndv, ndv, -, EnvmapPower); EnvmapMin/Range .x only.
    float envamt = pow(ndv, clamp(m.envmap_power, -128.0, 128.0)) * m.envmap_range + m.envmap_min;
    // Per light: clamp(N . L + 0.1, 0, 1) x its colour (c.w = 0.1).
    float3 lights = 0;
    for (int i = 0; i < 3; i++) {
        lights += clamp(dot(v1, l.dir[i].xyz) + 0.1, 0.0, 1.0) * l.colour[i].rgb;
    }
    float4 diffuse = ndv * m.diffuse_range + m.diffuse_min;    // mad r0, ndv, DiffuseRange, DiffuseMin
    o.d0 = saturate(float4(ao * lights * diffuse.rgb, diffuse.w));
    o.d1 = saturate(float4(spec * ao, ao * envamt));
    float3 r = (ndv + ndv) * v1 - v;                            // mad r0.xyz, 2 ndv, v1, -V
    o.env = r.x * wv.row[0].xyz + r.y * wv.row[1].xyz + r.z * wv.row[2].xyz; // oT0 = R x WorldView (dp3 with its columns)
    if (c.shadow_level == 0) {
        // vs #11: oT2 = N, oT3 = normalize(V + L0).
        float3 h = v + l.dir[0].xyz;
        o.local_normal = v1;
        o.tlight = h * rsqrt(dot(h, h));
    } else {
        // vs #9/#7/#5: oT3 = L0, oT4 = N, oT5 = V.
        o.tlight = l.dir[0].xyz;
        o.local_normal = v1;
        o.tview = v;
    }
}

// IDI_WORLDBONE_FX technique #0 'world' vs #1 (vs_1_1), instruction by
// instruction: 60-byte vertices (position, normal, D3DCOLOR, uv, three blend
// weights v4, three blend indices v5 as floats). The position is skinned by
// the three BlendMatrices its indices pick; the lighting uses the unskinned
// position and normal (LocalEyePos, two lights: LocalDirectionMatrix rows 0
// and 1, LocalColourMatrix columns 0 and 1, +0.3 wrap).
void BoneVertex(thread MeshOut &o, const device float *v, constant MeshConst &c, constant Material &m, constant Lights &l,
                constant float4x4 *bones) {
    float4 v0 = float4(v[0], v[1], v[2], 1);
    float3 v1 = float3(v[3], v[4], v[5]);
    uint argb = as_type<uint>(v[6]);
    float red = float((argb >> 16) & 0xff) / 255.0;
    float3 w = float3(v[9], v[10], v[11]);
    int3 idx = int3(floor(float3(v[12], v[13], v[14])));
    // Row-vector matrices loaded column-major: B * p computes p * B.
    float4 skinned = (bones[idx.y] * v0) * w.y;                // dp4 x4 c[a0]; mul r1, r0, v4.y
    skinned = (bones[idx.x] * v0) * w.x + skinned;             // mad r1, r0, v4.x, r1
    skinned = (bones[idx.z] * v0) * w.z + skinned;             // mad r1, r0, v4.z, r1
    o.pos = c.mvp * skinned;                                   // dp4 oPos, r1, WorldViewProj
    o.world_pos = (c.world * skinned).xyz;
    float3 e = l.eye.xyz - v0.xyz;                             // add r2.xyz, -v0, LocalEyePos
    e = e * rsqrt(dot(e, e));                                  // dp3, rsq, mul
    float ndv = max(clamp(dot(e, v1), 0.0, 1.0), 0.01);       // dp3; max 0; min 1; max 0.01
    float spec = pow(ndv, clamp(m.specular_power, -128.0, 128.0));   // lit r0, r2.xxyy
    float env = pow(ndv, clamp(m.envmap_power, -128.0, 128.0));      // lit r1, r2.xxzz
    float4 diffuse = ndv * m.diffuse_range + m.diffuse_min;    // mad r0, r2.x, DiffuseRange, DiffuseMin
    float l1 = clamp(dot(v1, l.dir[1].xyz) + 0.3, 0.0, 1.0);   // dp3 r1.x, v1, c72; add 0.3; max 0; min 1
    float l0 = clamp(dot(v1, l.dir[0].xyz) + 0.3, 0.0, 1.0);   // dp3 r2.x, v1, c71; ...
    float3 lights = l0 * l.colour[0].rgb + l1 * l.colour[1].rgb;
    o.d0 = saturate(float4(diffuse.rgb * lights, diffuse.w));  // mul oD0.xyz; mov oD0.w
    float envamt = (env * m.envmap_range + m.envmap_min) * red;
    o.d1 = saturate(float4((spec * m.specular_range.rgb + m.specular_min.rgb) * red, envamt + envamt));
    o.uv = float2(v[7], v[8]);                                 // mov oT0, v3
}

vertex MeshOut mesh_vs(uint vid [[vertex_id]], const device float *verts [[buffer(0)]], constant MeshConst &c [[buffer(1)]],
                       constant WorldFog &fog [[buffer(2)]], constant SkyConst &sky [[buffer(3)]], constant Material &m [[buffer(4)]],
                       constant Lights &l [[buffer(5)]], constant EnvMatrix &wv [[buffer(6)]],
                       constant float4x4 *bones [[buffer(7)]]) {
    const device float *v = verts + vid * (c.stride / 4);
    float4 p = float4(v[0], v[1], v[2], 1);
    MeshOut o;
    // Row-vector matrices loaded column-major: M * p computes p * M.
    o.pos = c.mvp * p;
    o.world_pos = (c.world * p).xyz;
    o.local_pos = p.xyz;
    o.local_normal = float3(v[3], v[4], v[5]);
    o.normal = (c.world * float4(v[3], v[4], v[5], 0)).xyz;
    uint argb = as_type<uint>(v[6]);
    o.color = float4((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, argb >> 24) / 255.0;
    o.uv = float2(v[7], v[8]);
    // Colour registers: oD0 (ambient) and oD1 (diffuse), clamped to 0..1 as
    // vs_1_1 colour outputs are. Other effects: oD0 = vertex colour.
    o.d0 = o.color;
    o.d1 = 0;
    o.inscatter = 0;
    o.uv1 = 0;
    o.spec = 0;
    o.tlight = 0;
    o.tview = 0;
    // The shadowed techniques' vertex shaders: oT = v0 * ShadowTransform.
    o.shadow_pos = c.shadow * p;
    o.uv2 = 0;
    // IDI_WORLDREFLECT_FX vs #13: oT1 = ((x + w) / 2, (w - y) / 2, z, w) of
    // the clip position, sampled projectively (texldp): the screen position
    // in the reflection target.
    o.clip = o.pos;
    o.env = 0;
    if (c.effect == 4) {
        CarVertex(o, p.xyz, o.local_normal, o.color, c, m, l, wv);
        return o;
    }
    if (c.effect == 2 && (c.shade & 1024) != 0) {
        BoneVertex(o, v, c, m, l, bones);
        return o;
    }
    if (c.effect == 22) {
        // IDI_SHADOW_CG_FX vs #1: position, oD0 = colour, oT0 = uv.
        return o;
    }
    if (c.effect == 19) {
        // IDI_SKYBOX_FX (c.pad = pass). Second uv set (dcl_texcoord1) after
        // the first in the longer vertex formats.
        float2 second = c.stride >= 44 ? float2(v[9], v[10]) : o.uv;
        o.uv1 = second;
        if (c.pad == 0) {
            // vs #5: oT0 = uv - (0.4 CloudScroll, 0), oT2 = uv - (0.2
            // CloudScroll + 0.3, 0), oT1 = uv1, oT3 = in-scatter at the fixed
            // distance 15000 (expp of -21640.43 (Br + Bm), no extinction).
            o.uv = float2(o.uv.x - 0.4 * sky.cloud_scroll, o.uv.y);
            o.uv2 = float2(o.uv.x + 0.4 * sky.cloud_scroll - (0.2 * sky.cloud_scroll + 0.3), o.uv.y);
            float3 to_eye = normalize(c.eye.xyz - o.world_pos);
            float cos_t = dot(normalize(fog.light_dir.xyz), to_eye);
            float mie = fog.const3.x * pow(max(fog.const3.z * cos_t + fog.const3.y, 1e-6), -1.5);
            float3 e = exp2(-21640.426 * fog.br_plus_bm.xyz);
            float3 in = (fog.const1.xyz * (1 + cos_t * cos_t) + fog.const2.xyz * mie) * fog.one_over_brbm.xyz * (1 - e) * fog.const3.w;
            o.inscatter = saturate(in);
        } else {
            // vs #3 / #1: oT0 = uv - (CloudScroll, 0), oT1 = uv1.
            o.uv = float2(o.uv.x - sky.cloud_scroll, o.uv.y);
        }
        return o;
    }
    if (NormalMapped(c)) {
        // IDI_WORLDNORMALMAP_FX vs #13 / IDI_WORLDREFLECT_FX vs #13 (technique
        // #0): tangent frame T (tangent), B = (N x T) T.w, N. oT2/oT3 =
        // normalize(L) and V = normalize(eye - v) in it; oD0 = (AmbientColour.rgb
        // * AmbientColour.w, 1) colour; oT5 = min(E, 1), oT4 = in-scatter.
        float4 tangent = float4(v[11], v[12], v[13], v[14]);
        float3 t = (c.world * float4(tangent.xyz, 0)).xyz;
        float3 n = o.normal;
        float3 b = cross(n, t) * tangent.w;
        float3 l = fog.light_dir.xyz;
        float3 vv = normalize(c.eye.xyz - o.world_pos);
        o.tlight = normalize(float3(dot(t, l), dot(b, l), dot(n, l)));
        o.tview = float3(dot(t, vv), dot(b, vv), dot(n, vv));
        float3 extinction = 1;
        if (fog.enabled) {
            Scatter(o.world_pos, c.eye.xyz, fog, extinction, o.inscatter);
        }
        float3 ambient = fog.lit ? fog.ambient.rgb * fog.ambient.w : float3(1);
        o.d0 = saturate(float4(ambient, 1) * o.color);
        o.d1 = float4(min(extinction, 1.0), 0);
    } else if (c.effect == 5) {
        // IDI_GLOSSYWINDOW_FX technique #0 'world' shader=1 (vs #11). L =
        // LocalLightVec, V = normalize(eye - v), R = 2 (N.L) N - L.
        float3 n = o.normal;
        float3 l = fog.light_dir.xyz;
        float3 v = normalize(c.eye.xyz - o.world_pos);
        float ndl = dot(n, l);
        float rv = saturate(dot(2.0 * ndl * n - l, v));
        float rv2 = rv * rv;
        float rv8 = rv2 * rv2;
        rv8 *= rv8;
        o.spec = rv2 * rv8; // oT2 = (R.V)^10
        float3 extinction = 1;
        if (fog.enabled) {
            Scatter(o.world_pos, c.eye.xyz, fog, extinction, o.inscatter); // oT3
        }
        // oD0 = saturate(N.L) DiffuseColour; oD1 = E (AmbientColour.rgb *
        // AmbientColour.w) colour, alpha the vertex alpha.
        o.d0 = fog.lit ? saturate(saturate(ndl) * fog.diffuse) : float4(0);
        float3 ambient = fog.lit ? fog.ambient.rgb * fog.ambient.w : float3(1);
        o.d1 = saturate(float4(min(extinction, 1.0) * ambient * o.color.rgb, o.color.a));
        // oT1 = (u / 2 + (N . WorldView[0]) / 2, v / 2): WindowReflection.
        o.uv1 = float2(0.5 * o.uv.x + 0.5 * dot(n, c.view_x.xyz), 0.5 * o.uv.y);
    } else if (c.effect == 6) {
        // IDI_TREE_FX technique #0 (vs #1): oD0 = (E sat(N.L) DiffuseColour.rgb,
        // sat(N.L) DiffuseColour.w), oD1 = (AmbientColour.rgb AmbientColour.w,
        // 1) colour (no extinction), oT2 = in-scatter.
        float3 extinction = 1;
        if (fog.enabled) {
            Scatter(o.world_pos, c.eye.xyz, fog, extinction, o.inscatter);
        }
        float ndl = saturate(dot(fog.light_dir.xyz, o.normal));
        float4 diffuse = fog.lit ? ndl * fog.diffuse : float4(0);
        o.d0 = saturate(float4(min(extinction, 1.0) * diffuse.rgb, diffuse.a));
        float3 ambient = fog.lit ? fog.ambient.rgb * fog.ambient.w : float3(1);
        o.d1 = saturate(float4(ambient, 1) * o.color);
    } else if (FoggedEffect(c.effect)) {
        float3 extinction = 1;
        if (fog.enabled) {
            Scatter(o.world_pos, c.eye.xyz, fog, extinction, o.inscatter);
        }
        // IDI_WORLD_FX vs #13: oD0 = E * (AmbientColour.rgb * AmbientColour.w)
        // * colour (alpha: colour alpha), oD1 = saturate(N.L) * DiffuseColour.
        float3 ambient = fog.lit ? fog.ambient.rgb * fog.ambient.w : float3(1);
        o.d0 = saturate(float4(extinction * ambient * o.color.rgb, o.color.a));
        if (fog.lit) {
            float3 n = normalize(o.normal);
            o.d1 = float4(saturate(saturate(dot(fog.light_dir.xyz, n)) * fog.diffuse.rgb), 0);
        }
    }
    return o;
}

// Shadow map samples (NVIDIA path: texldp on the D24S8 depth texture is a
// hardware depth compare, bilinearly filtered; ZFUNC LESSEQUAL).
// ShadowMapScale = 1/1024 (sub_6C86B0); the four-tap techniques offset
// the projected coordinate by +-0.5 ShadowMapScale w in x and y.
// Port deviation: PC's map is a fixed 1024x1024 (a 2005 GPU limit); natively
// its size follows the render height and Shadow Detail (see drawShadowMap),
// and MeshConst::shadow_texel (1 / size) keeps PC's +-0.5 texel tap pattern.
constant float2 kShadowTaps[4] = { float2(-0.5, -0.5), float2(0.5, -0.5), float2(-0.5, 0.5), float2(0.5, 0.5) };

float ShadowCompare(depth2d<float> sm, float4 p) {
    constexpr sampler ss(compare_func::less_equal, filter::linear, address::clamp_to_edge);
    return sm.sample_compare(ss, p.xy / p.w, p.z / p.w);
}

float ShadowColour(texture2d<float> cm, float4 p) {
    constexpr sampler cs(filter::linear, address::clamp_to_edge);
    return cm.sample(cs, p.xy / p.w).r;
}

float4 ShadowTap(float4 p, int i, float texel) {
    return float4(p.xy + kShadowTaps[i] * (texel * p.w), p.zw);
}

// World effects (IDI_WORLD_FX ps #10 / ps #8 and the WORLDNORMALMAP,
// WORLDREFLECT, GLOSSYWINDOW equivalents): saturate(depth compare (one tap,
// or the mean of four) + saturate((0.2 - v) 5) + InvShadowStrength); the
// middle term fades the shadow out towards the top of the map (far end).
float WorldShadow(constant MeshConst &c, float4 p, depth2d<float> sm) {
    if (c.shadow_level == 0 || !c.shadow_map) {
        return 1.0;
    }
    float lit;
    if (c.shadow_level == 1) {
        lit = ShadowCompare(sm, p);
    } else {
        lit = 0;
        for (int i = 0; i < 4; i++) {
            lit += ShadowCompare(sm, ShadowTap(p, i, c.shadow_texel));
        }
        lit *= 0.25;
    }
    float fade = saturate((0.2 - p.y / p.w) * 5.0);
    return saturate(lit + fade + c.inv_shadow);
}

// IDI_CAR_FX ps #8 (one tap) / ps #6 (four): cars in the colour map (red,
// IDI_CAR_SHADOW_MAP_FX) don't shadow cars, so where it reads > 0.5 (one
// tap) or more than one of the four taps is a car, the pixel is lit; the
// factor is lerp(0.2 + InvShadowStrength, 1, lit).
float CarShadow(constant MeshConst &c, float4 p, depth2d<float> sm, texture2d<float> cm) {
    if (c.shadow_level == 0 || !c.shadow_map) {
        return 1.0;
    }
    float lit;
    if (c.shadow_level == 1) {
        lit = 0.5 - ShadowColour(cm, p) >= 0 ? ShadowCompare(sm, p) : 1.0;
    } else {
        float depth = 0, colour = 0;
        for (int i = 0; i < 4; i++) {
            float4 t = ShadowTap(p, i, c.shadow_texel);
            depth += ShadowCompare(sm, t);
            colour += ShadowColour(cm, t);
        }
        lit = 1.0 - colour >= 0 ? depth * 0.25 : 1.0;
    }
    return mix(0.2 + c.inv_shadow, 1.0, lit);
}

// IDI_CAR_FX pixel shaders, from the vertex shader's colour registers (v0 =
// oD0: ao x lights x diffuse, alpha the diffuse alpha; v1 = oD1: specular
// colour, alpha the env-map amount), t0 = texCUBE(EnvMapTex, oT0), t1 = the
// diffuse texture at oT1.
float4 CarShade(MeshOut in, float4 t1, constant MeshConst &c, texturecube<float> env_map, float shadow) {
    float4 v0 = in.d0, v1 = in.d1;
    float3 t0 = 0;
    if ((c.shade & 24) == 24) { // a cube is bound and the draw has the env map (CarRender's disable_env_flag)
        constexpr sampler es(filter::linear, mip_filter::linear, address::clamp_to_edge); // envmap_sampler
        t0 = env_map.sample(es, in.env).rgb;
    }
    if (c.shade & 32) {
        return float4(t0, 1); // NFSMW_SHADE=envmap: the cube lookup alone
    }
    if (c.shadow_level == 0) {
        // ps #10 (ps_1_1, technique #0). texcoord t2/t3 hand the
        // interpolated oT2 (N) and oT3 (H) over as colours, clamped to 0..1.
        float3 t2 = saturate(in.local_normal), t3 = saturate(in.tlight);
        float r0w = saturate(dot(t2, t3));                       // dp3_sat r0, t2, t3
        r0w = r0w * r0w;                                         // mul r0.w, r0.w, r0.w
        r0w = r0w * r0w;                                         // mul r0.w, r0.w, r0.w
        float3 r1 = v0.w * v1.rgb;                               // +mul r1.xyz, v0.w, v1
        r0w = 2.0 * (r0w * r0w);                                 // mul_x2 r0.w, r0.w, r0.w
        float3 t2c = t1.rgb * v0.rgb;                            // +mul t2.xyz, t1, v0
        float3 r0 = r0w * r1 + t2c;                              // mad r0.xyz, r0.w, r1, t2
        r1 = t0 * v1.w;                                          // mul r1.xyz, t0, v1.w
        float a = t1.a * v0.w;                                   // +mul r0.w, t1.w, v0.w
        r0 = r1 * v0.w + r0;                                     // mad r0.xyz, r1, v0.w, r0
        return saturate(float4(r0, a));
    }
    // ps #8 / #6 / #4 (technique #1..#3): Phong (R_L . V)^16 x 2 with R_L =
    // 2 (N . L0) N - L0 from the unnormalised interpolated N (t4), L0 (t3)
    // and V (t5); s = lerp(0.2 + InvShadowStrength, 1, lit) (CarShadow).
    float3 n = in.local_normal;
    float ndl = dot(n, in.tlight);                               // dp3 r5.x, r4, t3
    float3 rl = (ndl + ndl) * n - in.tlight;                     // add, mad r4.xyz, r1.w, r4, -t3
    float x = saturate(dot(rl, in.tview));                       // dp3_sat r4.x, r4, t5
    x = x * x;
    x = x * x;
    x = x * x;
    x = x * x;
    float hl = x + x;                                            // add r1.w, r1.w, r1.w
    float3 r2 = hl * (v0.w * v1.rgb) * shadow;                   // mul r2.xyz, v0.w, v1; x hl; x s
    float3 r0 = v0.rgb * t1.rgb + r2;                            // mad r0.xyz, v0, r0, r2
    float3 env = t0 * v1.w * v0.w * 0.5;                         // mul r1.xyz, r1, v1.w; x v0.w; x 0.5
    return float4(r0 * shadow + env, t1.a * v0.w);               // mad r0.xyz, r0, r1.w, r1
}

// kMeshAlphaTest: the variant with the alpha test (shade bit 1), for the
// draws that have it. The other draws' variant decides whether to discard
// from a uniform alone (never, in fact), not from each pixel's shaded alpha:
// measured cheaper on the GPU, and the same pixels. (No
// discard at all would let hidden surface removal run, which is cheaper
// still but resolves coplanar surfaces against the alpha-tested draws
// differently: different pixels.) Undefined (a plain newFunctionWithName:)
// means with.
constant bool kMeshAlphaTestValue [[function_constant(0)]];
constant bool kMeshAlphaTest = is_function_constant_defined(kMeshAlphaTestValue) ? kMeshAlphaTestValue : true;

// shade: bit 0 texture-only debug view, bit 1 alpha test (punch-through
// textures only; elsewhere alpha is a gloss/spec mask, or blends), bit 2
// lit (model-space lights), bit 3 an environment cube is bound, bit 4 the
// draw has an EnvMapMatrix.
fragment float4 mesh_fs(MeshOut in [[stage_in]], texture2d<float> tex [[texture(0)]], texturecube<float> env_map [[texture(1)]],
                        texture2d<float> tex2 [[texture(2)]], texture2d<float> tex3 [[texture(3)]],
                        depth2d<float> shadow_map [[texture(4)]], texture2d<float> shadow_colour [[texture(5)]],
                        texture2d<float> sky1 [[texture(6)]], texture2d<float> sky2 [[texture(7)]], texture2d<float> sky3 [[texture(8)]],
                        texture2d<float> reflected [[texture(9)]],
                        constant SkyConst &sky [[buffer(5)]],
                        constant WorldFog &fog [[buffer(4)]],
                        constant MeshConst &c [[buffer(0)]], sampler s [[sampler(0)]]) {
    float4 t = tex.sample(s, in.uv);
    float4 out;
    if (c.shade & 512) {
        out = float4(in.color.rgb, 1); // NFSMW_SHADE=vcolour: the vertex colour
    } else if (c.shade & 1) {
        out = t;
    } else if (c.effect == 19) {
        // IDI_SKYBOX_FX: p0 (ps #4) the sky colour: a = 2 t0.a t2.a
        // CloudIntensity.x, (t1 + t0) / 2 a + in-scatter (1 - a), opaque;
        // p1 (ps #2) SKY_TEXTURE_MISC_2 with alpha x CloudIntensity.y and p2
        // (ps #0) SKY_TEXTURE_MISC_3 at uv1 with alpha x CloudIntensity.z,
        // both blended over it with ZFUNC EQUAL. The misc samplers have LOD
        // bias -1. Guess: ps_1_1's clamp of a to 0..1.
        if (c.pad == 0) {
            float4 t2 = tex.sample(s, in.uv2);
            float4 t1 = sky1.sample(s, in.uv1, bias(-1.0));
            float a = saturate(2.0 * t.a * t2.a * sky.cloud_intensity.x);
            out = float4(saturate(0.5 * (t1.rgb + t.rgb) * a + in.inscatter * (1 - a)), 1);
        } else if (c.pad == 1) {
            float4 m = sky2.sample(s, in.uv, bias(-1.0));
            out = float4(m.rgb, m.a * sky.cloud_intensity.y);
        } else {
            float4 m = sky3.sample(s, in.uv1, bias(-1.0));
            out = float4(m.rgb, m.a * sky.cloud_intensity.z);
        }
        return out;
    } else if (c.effect == 22) {
        // IDI_SHADOW_CG_FX ps #0 (ps_1_1): t v0 ShadowColor c1; ShadowColor
        // (0.5, 0.5, 0.5, w) with w in c.eye.w. ps_1_x clamps constants to
        // -1..1: the 'def c1, 1, 1, 1, 2' is (1, 1, 1, 1) and w at most 1.
        out = saturate(t * saturate(in.d0) * float4(0.5, 0.5, 0.5, min(c.eye.w, 1.0)));
    } else if (c.effect == 4) {
        out = CarShade(in, t, c, env_map, CarShadow(c, in.shadow_pos, shadow_map, shadow_colour));
    } else if (c.effect == 2 && (c.shade & 1024) != 0) {
        // IDI_WORLDBONE_FX ps #0 (ps_1_1): t v0 c0, c0 = (1, 1, 1, 2)
        // clamped to 1 as ps_1_x constants are.
        out = saturate(t * in.d0);
    } else if (NormalMapped(c)) {
        // ps #12 of IDI_WORLDNORMALMAP_FX / IDI_WORLDREFLECT_FX: N from the
        // normal map, L = normalize(oT2), R = 2 (N.L) N - L, V = normalize(oT3);
        // spec = (R.V)^SpecularPower SpecularColour x specular map (x2 for
        // WORLDREFLECT); c = (t (sat(N.L) DiffuseColour) + spec + t oD0) oT5,
        // out 2c + oT4. WORLDNORMALMAP alpha t.a oD0.a; WORLDREFLECT alpha 1
        // and c also + ReflectedTex (texldp oT1, clamp, no mips) x t.a oD0.a
        // 2 spec.r SpecularColour.w, with SpecularColour.rgb x (1 - cloud).
        // Guess: the shadowed techniques (ps #10 / #8) add it the same way.
        float3 nm = normalize((tex2.sample(s, in.uv).rgb - 0.5) * 2.0);
        float3 sm = tex3.sample(s, in.uv).rgb;
        float3 lt = normalize(in.tlight);
        float ndl = dot(nm, lt);
        float rv = saturate(dot(2.0 * ndl * nm - lt, normalize(in.tview)));
        bool world_reflect = c.effect == 1;
        float3 spec = pow(rv, fog.specular_power) * fog.specular.rgb * sm * (world_reflect ? 2.0 * fog.reflect_specular_scale : 1.0);
        float3 diffuse = fog.lit ? fog.diffuse.rgb : float3(0);
        // Shadowed techniques (#1/#2: ps #10 / #8): the diffuse + specular sum
        // times the shadow factor.
        float sh = WorldShadow(c, in.shadow_pos, shadow_map);
        float3 col = (t.rgb * (saturate(ndl) * diffuse) + spec) * sh + t.rgb * in.d0.rgb;
        if (world_reflect && fog.specular.w > 0) {
            constexpr sampler rs(filter::linear, address::clamp_to_edge);
            float2 ruv = float2(0.5 + 0.5 * in.clip.x / in.clip.w, 0.5 - 0.5 * in.clip.y / in.clip.w);
            col += reflected.sample(rs, ruv).rgb * (t.a * in.d0.a * 2.0 * sm.r * fog.specular.w);
        }
        col *= in.d1.rgb;
        out = float4(saturate(2.0 * col + in.inscatter), world_reflect ? 1.0 : t.a * in.d0.a);
    } else if (c.effect == 6) {
        // IDI_TREE_FX ps #0: 2 (t oD1 + t oD0) + oT2, alpha t.a.
        out = float4(saturate(2.0 * t.rgb * (in.d0.rgb + in.d1.rgb) + in.inscatter), t.a);
    } else if (c.effect == 5) {
        // IDI_GLOSSYWINDOW_FX ps #10: 2 (t oD1 + t oD0 + 2 t.a spec) (the
        // mad_x2 doubles the running sum) + t.a reflection (WindowReflection,
        // wrap, LOD bias -1) + in-scatter; alpha t.a.
        // Shadowed (technique #1 ps #8, #2): 2 ((t oD0 + 2 t.a spec) s + t oD1) + ...
        float3 refl = tex2.sample(s, in.uv1, bias(-1.0)).rgb;
        float sh = WorldShadow(c, in.shadow_pos, shadow_map);
        float3 rgb = 2.0 * (t.rgb * in.d1.rgb + (t.rgb * in.d0.rgb + 2.0 * t.a * in.spec) * sh) + t.a * refl + in.inscatter;
        out = float4(saturate(rgb), t.a);
    } else {
        // PC WORLD (ps #12): 2 x texture x (oD0 + oD1) + in-scatter (mad_x2,
        // then add t2); alpha texture x oD0 alpha. WORLDPRELIT and the
        // unlit effects: oD0 = vertex colour, oD1 = 0.
        // Shadowed WORLD (technique #1 ps #10, #2 ps #8): 2 (t oD1 s + t oD0) + t2.
        float sh = WorldShadow(c, in.shadow_pos, shadow_map);
        out = float4(saturate(2.0 * t.rgb * (in.d0.rgb + in.d1.rgb * sh) + in.inscatter), t.a * in.d0.a);
    }
    if (kMeshAlphaTest && (c.shade & 2) && out.a < (95.0 / 255.0)) { // ALPHAREF 0x5F (sub_6C68B0)
        discard_fragment();
    }
    if (!kMeshAlphaTest && c.shade == 0xFFFFFFFFu) {
        discard_fragment(); // never: keeps the discarding shader's depth handling (see kMeshAlphaTest)
    }
    if (c.shade & 256) {
        // NFSMW_SHADE=shadow: the shadow factor (red: level 1, green: 2).
        float f = c.effect == 4 ? CarShadow(c, in.shadow_pos, shadow_map, shadow_colour) : WorldShadow(c, in.shadow_pos, shadow_map);
        return float4(c.shadow_level == 1 ? f : f * 0.5, c.shadow_level == 2 ? f : f * 0.5, f * 0.5, 1);
    }
    return out;
}

// Shadow map pass (render target type 4). IDI_CAR_SHADOW_MAP_FX: position
// only, colour (1, 0, 0, 0) with ColorWriteMode 1 (red); the world effects
// (WORLDNOFOG / WORLDNORMALMAPNOFOG substitutes) write depth only with their
// alpha test. alpha_test: discard below 0.5 of texture x vertex alpha.
struct ShadowConst { float4x4 mvp; uint stride; uint alpha_test; uint pad[2]; };
struct ShadowOut { float4 pos [[position]]; float2 uv; float alpha; };

vertex ShadowOut shadow_vs(uint vid [[vertex_id]], const device float *verts [[buffer(0)]], constant ShadowConst &c [[buffer(1)]]) {
    const device float *v = verts + vid * (c.stride / 4);
    ShadowOut o;
    o.pos = c.mvp * float4(v[0], v[1], v[2], 1);
    o.uv = 0;
    o.alpha = 1;
    if (c.alpha_test) { // (shadow-map meshes are positions only)
        o.uv = float2(v[7], v[8]);
        o.alpha = float(as_type<uint>(v[6]) >> 24) / 255.0;
    }
    return o;
}

fragment float4 shadow_fs(ShadowOut in [[stage_in]], texture2d<float> tex [[texture(0)]], constant ShadowConst &c [[buffer(0)]],
                          sampler s [[sampler(0)]]) {
    if (c.alpha_test && tex.sample(s, in.uv).a * in.alpha < 0.5) {
        discard_fragment();
    }
    return float4(1, 0, 0, 0);
}

// Full-screen triangle for the post-process passes.
struct QuadOut { float4 pos [[position]]; float2 uv; };

vertex QuadOut quad_vs(uint vid [[vertex_id]]) {
    float2 p = float2((vid << 1) & 2, vid & 2);
    QuadOut o;
    o.pos = float4(p * 2 - 1, 0, 1);
    o.uv = float2(p.x, 1 - p.y);
    return o;
}

constant float3 kLuminance = float3(0.6125, 0.5154, 0.0721); // LUMINANCE_VECTOR (IDI_FILTER_FX / IDI_VISUALTREATMENT_FX)

// IDI_FILTER_FX: weighted taps of one texture (sub_6D2CD0). lum_alpha 1:
// pass p4, alpha = luminance of the weighted rgb; 2: pass p5, rgb times the
// weighted alpha; 0: pass p0..p3, rgba.
struct FilterConst { float4 weights[4]; float4 offsets[4]; uint taps; uint lum_alpha; };

fragment float4 filter_fs(QuadOut in [[stage_in]], texture2d<float> src [[texture(0)]], constant FilterConst &c [[buffer(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float4 sum = 0;
    for (uint i = 0; i < c.taps; i++) {
        sum += src.sample(s, in.uv + c.offsets[i].xy) * c.weights[i];
    }
    if (c.lum_alpha == 1) {
        sum.a = dot(saturate(sum.rgb), kLuminance);
    } else if (c.lum_alpha == 2) {
        sum = saturate(sum);
        sum.rgb *= sum.a;
    }
    return saturate(sum);
}

// The offscreen frame into the drawable when the visual treatment is off.
fragment float4 copy_fs(QuadOut in [[stage_in]], texture2d<float> src [[texture(0)]]) {
    constexpr sampler s(filter::nearest, address::clamp_to_edge);
    return float4(src.sample(s, in.uv).rgb, 1);
}

// IDI_VISUALTREATMENT_FX technique 0 (ps_2_0, shader #4).
struct TreatmentConst { float4 black_bloom; float4 tint; float4 desaturation; float colour_bloom; float brightness; float vignette; float pad; };

fragment float4 treatment_fs(QuadOut in [[stage_in]], texture2d<float> scene [[texture(0)]], texture2d<float> bloom [[texture(1)]],
                             texture2d<float> gain [[texture(2)]], constant TreatmentConst &c [[buffer(0)]],
                             constant uint *spline [[buffer(1)]]) {
    constexpr sampler lin(filter::linear, address::clamp_to_edge);
    float4 b = bloom.sample(lin, in.uv);              // DIFFUSETEX: blurred quarter-size scene, a = luminance
    // SPLINE (256x1, point sampled at (b.a, b.r)): r black bloom, g colour bloom.
    uint texel = spline[min(uint(saturate(b.a) * 256.0), 255u)];
    float4 curve = float4((texel >> 16) & 0xff, (texel >> 8) & 0xff, texel & 0xff, texel >> 24) / 255.0;
    float4 col = scene.sample(lin, in.uv);            // DiffuseMap: the frame
    float4 g = gain.sample(lin, in.uv);               // GAINMAP: UVESVIGNETTE
    float lum = dot(col.rgb, kLuminance);
    float3 desat = c.desaturation.x * col.rgb + lum * c.desaturation.y;
    float3 colour_bloom = col.rgb * (curve.g * c.colour_bloom * c.tint.rgb);
    float black_bloom = curve.r * c.black_bloom.x + c.black_bloom.y;
    float3 rgb = desat * black_bloom + colour_bloom;
    rgb += g.g * c.vignette;
    rgb *= c.brightness;
    return float4(g.b * rgb, 1);
}

// IDI_OVERBRIGHT_FX pContrast (ps #0, ps_1_1): c = saturate(2 (t - offset)),
// grey = dot(c, OverBrightGreyScale (0x8F9B38: 0.5)), out (c grey, 0).
fragment float4 overbright_fs(QuadOut in [[stage_in]], texture2d<float> src [[texture(0)]], constant float &offset [[buffer(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float3 c = saturate(2.0 * (src.sample(s, in.uv).rgb - offset));
    float grey = dot(c, float3(0.5));
    return float4(c * grey, 0);
}

// sub_6D3B80 with IDI_FILTER_FX p0 (t0 FilterWeights[0] = 1) and blending
// BLENDFACTOR, ONE (sub_6C6810(0xE, 2)): the blurred over-bright added over
// the frame scaled by the blend factor.
fragment float4 overbright_add_fs(QuadOut in [[stage_in]], texture2d<float> src [[texture(0)]], constant float &intensity [[buffer(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    return float4(src.sample(s, in.uv).rgb * intensity, 0);
}

// Motion blur (sub_6DBB20): one sub_6D2840 pass, IDI_FILTER_FX p3 (ps #4:
// four taps times FilterWeights 0.25, rgba). Tap i's texture coordinates
// run over the rect (-i k o.xy)..(1 - i k o.zw) across the quad (k = 1, then
// 4). sub_6D2840 doesn't offset its quad by half a pixel (sub_6D2CD0 and
// sub_6D3B80 do), so D3D9 interpolates the pixel centres' coordinates at
// x / w rather than (x + 0.5) / w.
struct RadialConst { float4 offsets; float scale; float pad[3]; };

fragment float4 radial_fs(QuadOut in [[stage_in]], texture2d<float> src [[texture(0)]], constant RadialConst &c [[buffer(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float2 uv = in.uv - 0.5 / float2(src.get_width(), src.get_height());
    float4 sum = 0;
    for (int i = 0; i < 4; i++) {
        float k = float(i) * c.scale;
        float2 t = mix(-k * c.offsets.xy, 1.0 - k * c.offsets.zw, uv);
        sum += src.sample(s, t) * 0.25;
    }
    return saturate(sum);
}

// sub_6D3B80 with IDI_FILTER_FX p0 (FilterWeights[0] = 1, ColorWriteMode
// 0xF): the texture as is; the blend state does the rest.
fragment float4 texture_fs(QuadOut in [[stage_in]], texture2d<float> src [[texture(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    return src.sample(s, in.uv);
}

// sub_6DAE20's fade of the X360 flare target: sub_6D3B80 with ZERO,
// BLENDFACTOR 0xFF786482 (the target times (0x78, 0x64, 0x82, 0xFF) / 255),
// here as a copy of last frame's target (the copy sub_6D40E0 makes into the
// FSAA surface) times the factor.
fragment float4 flare_fade_fs(QuadOut in [[stage_in]], texture2d<float> src [[texture(0)]]) {
    constexpr sampler s(filter::nearest, address::clamp_to_edge);
    return src.sample(s, in.uv) * float4(120.0 / 255.0, 100.0 / 255.0, 130.0 / 255.0, 1.0);
}

// IDI_PARTICLES_FX technique 0 (vs #5; ps_1_1 #4: mul_x2 r0, t0, v0) with
// the texture's alpha test (ALPHAREF 0x5F, sub_6C68B0); with a shadow map
// technique #1 (vs #3, ps #2: saturate(depth compare + 0.5) on the rgb; see
// MetalPrelit.mm), ShadowTransform = local2world x the shadow texture
// matrix (sub_6C1120 -> sub_6C8000).
struct ParticleVertex { packed_float3 pos; uint color; packed_float2 uv; };
struct ParticleConst { float4x4 mvp; float4x4 shadow; uint alpha_test; uint shadowed; uint pad[2]; };
struct ParticleOut { float4 pos [[position]]; float2 uv; float4 color; float4 shadow_pos; };

vertex ParticleOut particle_vs(uint vid [[vertex_id]], const device ParticleVertex *verts [[buffer(0)]],
                               constant ParticleConst &c [[buffer(1)]]) {
    ParticleVertex v = verts[vid];
    ParticleOut o;
    o.pos = c.mvp * float4(float3(v.pos), 1);
    uint argb = v.color;
    o.color = float4((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, argb >> 24) / 255.0;
    o.uv = float2(v.uv);
    o.shadow_pos = c.shadowed ? c.shadow * float4(float3(v.pos), 1) : float4(0, 0, 0, 1);
    return o;
}

fragment float4 particle_fs(ParticleOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler s [[sampler(0)]],
                            depth2d<float> sm [[texture(1)]], constant ParticleConst &c [[buffer(0)]]) {
    float4 out = 2.0 * tex.sample(s, in.uv) * in.color;
    if (c.shadowed) {
        float4 p = in.shadow_pos;
        out.rgb *= saturate(ShadowCompare(sm, float4(p.xy, saturate(p.z / p.w) * p.w, p.w)) + 0.5);
    }
    out = saturate(out);
    if (c.alpha_test && out.a < (95.0 / 255.0)) {
        discard_fragment();
    }
    return out;
}

// IDI_RAIN_DROP_FX (vs #1 passes clip-space corners; ps #0): the
// displacement texture's g/b offset into the frame copy, alpha its a x
// raindropalpha, colour 1.2 x the frame.
struct DropConst { float4 quad; float4 offset; float alpha; float pad[3]; };
struct DropOut { float4 pos [[position]]; float2 uv; };

vertex DropOut drop_vs(uint vid [[vertex_id]], constant DropConst &c [[buffer(0)]]) {
    // Triangle strip (X, Y) (X+s, Y) (X, Y+s) (X+s, Y+s), uv (0,0) (1,0) (0,1) (1,1).
    float2 k = float2(vid & 1, vid >> 1);
    DropOut o;
    o.pos = float4(c.quad.xy + k * c.quad.z, 0, 1);
    o.uv = k;
    return o;
}

fragment float4 drop_fs(DropOut in [[stage_in]], texture2d<float> frame [[texture(0)]], texture2d<float> displace [[texture(1)]],
                        constant DropConst &c [[buffer(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float4 d = displace.sample(s, in.uv);
    float2 uv = float2(d.g * c.offset.z + c.offset.x, d.b * c.offset.w + c.offset.y);
    return saturate(float4(frame.sample(s, uv).rgb * 1.2, d.a * c.alpha));
}

fragment float4 debug_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]]) {
    constexpr sampler s(filter::linear, mip_filter::linear);
    float4 c = tex.sample(s, in.uv);
    // Checkerboard behind transparent texels.
    float2 cell = floor(in.pos.xy / 16);
    float check = fmod(cell.x + cell.y, 2) * 0.15 + 0.2;
    return float4(mix(float3(check), c.rgb, c.a), 1);
}
)";

@implementation MetalRenderer {
    id<MTLDevice> _device;
    id<MTLCommandQueue> _queue;
    id<MTLRenderPipelineState> _debugPipeline;
    id<MTLRenderPipelineState> _fePipelines[3]; // indexed by PortBlendMode
    id<MTLRenderPipelineState> _meshPipelines[3]; // opaque, alpha blend, additive (1 sample, all channels, alpha test)
    // All of them, made on first use but for the 1-sample, all-channel ones
    // (made up front): [sample count 1, 2, 4, 8][mode][mask: all, rgb,
    // alpha][alpha test: without, with] (see kMeshAlphaTest).
    id<MTLRenderPipelineState> _meshPSO[4][3][3][2];
    // The player view's depth prepass (drawDraws): mesh_vs alone, no colour
    // writes, by sample count index.
    id<MTLRenderPipelineState> _meshDepthPSO[4];
    id<MTLLibrary> _lib;
    MTLPixelFormat _colorFormat, _depthFormat;
    // The pass drawDraws is drawing: its sample count, colour write mask
    // (0 all, 1 rgb, 2 alpha) and whether it is the player view's frame
    // (ColorWriteMode 7, then the sky's alpha-only pass).
    int _passSamples, _passMask;
    bool _passMainScene;
    id<MTLRenderPipelineState> _copyPipeline;
    // Multisampled frame (memoryless), resolved into _sceneColor.
    id<MTLTexture> _sceneColorMS;
    int _sceneSamples;
    // PC road reflection target (0x982A6C) and its blur partner (0x982A74),
    // 320x240, depth 0x982A78 (memoryless here).
    id<MTLTexture> _reflColor, _reflBlur, _reflDepth;
    bool _reflValid;           // _reflColor has been drawn
    id<MTLTexture> _reflBound; // this frame's ReflectedTex
    id<MTLSamplerState> _meshSamplers[3];         // by PC texture filtering (PortSetTextureFiltering)
    id<MTLDepthStencilState> _depthWrite;
    id<MTLDepthStencilState> _depthTest;
    id<MTLDepthStencilState> _depthOff;
    id<MTLDepthStencilState> _depthEqual; // test EQUAL, no writes
    PortSky _sky;                         // this frame's IDI_SKYBOX_FX constants
    id<MTLTexture> _skyMisc[3];
    id<MTLTexture> _white;
    id<MTLTexture> _black; // bound for a missing second texture
    // Visual treatment (see PortVisualTreatment).
    id<MTLRenderPipelineState> _filterPipeline;
    id<MTLRenderPipelineState> _treatmentPipeline;
    id<MTLTexture> _sceneColor, _sceneDepth; // the frame (PC 0x93DEF8)
    id<MTLTexture> _bloom[2];                // quarter size (PC 0x982964, 0x982968)
    // Over bright: the quarter-size copy (PC 0x98296C) and its contrast/blur
    // targets (on PC 0x982964/0x982968 again, after the treatment used them).
    id<MTLTexture> _obSource, _ob[2];
    id<MTLRenderPipelineState> _overbrightPipeline, _overbrightAddPipeline;
    // Motion blur (PortMotionBlur): PC 0x93DE60 / 0x93DE64 (full size), the
    // sub_6D2840 pipeline, sub_6D3B80's blend-factor composite per sample
    // count, and the frame's multisampled colour and depth kept in memory
    // (not memoryless) for the frames where the scene pass is split around
    // the blur.
    id<MTLTexture> _mb[2];
    id<MTLRenderPipelineState> _radialPipeline;
    id<MTLRenderPipelineState> _compositePSO[2][4]; // [blend factor, one/one][samples]
    id<MTLTexture> _sceneColorMSKept, _sceneDepthKept;
    // X360 flares (PC g_X360EffectsEnable, sub_6DAE20): the persistent flare
    // target 0x982960 / 0x93E7BC (two here, last frame's and this frame's),
    // its FSAA surface 0x93E7B8 (memoryless), the fade pipelines per sample
    // count, and the BLENDFACTOR / ONE add of over bright into it.
    id<MTLTexture> _flare[2], _flareMS;
    int _flareCurrent;
    id<MTLRenderPipelineState> _flareFadePipeline[4];
    id<MTLRenderPipelineState> _flareAddPipeline;
    // IDI_PARTICLES_FX: [sample count 1, 2, 4, 8][opaque, alpha, additive], rgb writes.
    id<MTLRenderPipelineState> _particlePSO[4][3];
    // IDI_RAIN_DROP_FX and the frame copy it refracts.
    id<MTLRenderPipelineState> _dropPipeline;
    id<MTLTexture> _dropSource;
    // Dynamic environment cube (PC 0x982A30) and its depth (0x982A4C).
    id<MTLTexture> _envCube, _envDepth;
    id<MTLTexture> _blackCube; // bound when there is none
    unsigned _envFacesDrawn; // faces drawn at least once (bit per face)
    bool _drawingEnvFace;    // drawDraws is drawing a dynamic env-map face
    bool _skipSkyAlpha;      // drawDraws leaves out the sky's alpha-only pass
    bool _onlySkyAlpha;      // drawDraws draws just the sky's alpha-only pass
    // PC shadow map (sub_6E44C0): 1024x1024 depth (0x982CF8) and colour
    // (0x982CF0) targets; pipelines for car (red) and world (depth only) draws.
    id<MTLTexture> _shadowDepth, _shadowColour;
    NSUInteger _shadowSize;
    CGFloat _drawableHeight;
    id<MTLTexture> _shadowDepthLit; // 1x1 depth 1, bound when no map was drawn
    id<MTLRenderPipelineState> _shadowPipelines[2];
    bool _shadowValid;        // this frame's scene has a shadow map
    float _shadowTransform[16];
    float _invShadowStrength;
    // The 3D frame being drawn: the published one, taken (swapped out, not
    // copied) when the game has published a new one, else drawn again.
    Frame3D _scene;
    uint64_t _sceneSerial;
    dispatch_group_t _envGroup; // the env-map faces' encode (drawEnvFaces)
}

- (instancetype)initWithView:(MTKView *)view {
    if ((self = [super init])) {
        _device = view.device;
        gDevice = _device;
        _queue = [_device newCommandQueue];
        _envGroup = dispatch_group_create();
        view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        view.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        view.preferredFramesPerSecond = 60;

        NSError *error = nil;
        id<MTLLibrary> lib = [_device newLibraryWithSource:kDebugShader options:nil error:&error];
        if (!lib) {
            NSLog(@"NFSMW: debug shader failed: %@", error);
        } else {
            MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
            desc.vertexFunction = [lib newFunctionWithName:@"debug_vs"];
            desc.fragmentFunction = [lib newFunctionWithName:@"debug_fs"];
            desc.colorAttachments[0].pixelFormat = view.colorPixelFormat;
            desc.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
            desc.stencilAttachmentPixelFormat = view.depthStencilPixelFormat;
            _debugPipeline = [_device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!_debugPipeline) {
                NSLog(@"NFSMW: debug pipeline failed: %@", error);
            }

            desc.vertexFunction = [lib newFunctionWithName:@"fe_vs"];
            desc.fragmentFunction = [lib newFunctionWithName:@"fe_fs"];
            MTLRenderPipelineColorAttachmentDescriptor *c = desc.colorAttachments[0];
            c.blendingEnabled = YES;
            c.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            c.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
            for (int mode = 0; mode < 3; mode++) {
                c.rgbBlendOperation = c.alphaBlendOperation =
                    mode == PORT_BLEND_SUBTRACTIVE ? MTLBlendOperationReverseSubtract : MTLBlendOperationAdd;
                c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor =
                    mode == PORT_BLEND_ALPHA ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorOne;
                _fePipelines[mode] = [_device newRenderPipelineStateWithDescriptor:desc error:&error];
                if (!_fePipelines[mode]) {
                    NSLog(@"NFSMW: FE pipeline %d failed: %@", mode, error);
                }
            }
        }

        if (lib) {
            _lib = lib;
            _colorFormat = view.colorPixelFormat;
            _depthFormat = view.depthStencilPixelFormat;
            _passSamples = 1;
            // The env-map faces' (a worker thread draws them): made now.
            for (int mode = 0; mode < 3; mode++) {
                for (int at = 0; at < 2; at++) {
                    _meshPSO[0][mode][0][at] = [self newMeshPipeline:mode sampleIndex:0 mask:0 alphaTest:at != 0];
                }
                _meshPipelines[mode] = _meshPSO[0][mode][0][1];
            }
        }
        if (lib) {
            MTLRenderPipelineDescriptor *sd = [MTLRenderPipelineDescriptor new];
            sd.vertexFunction = [lib newFunctionWithName:@"shadow_vs"];
            sd.fragmentFunction = [lib newFunctionWithName:@"shadow_fs"];
            sd.colorAttachments[0].pixelFormat = MTLPixelFormatR8Unorm;
            sd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            for (int i = 0; i < 2; i++) {
                // ColorWriteMode: 1 (red) for the cars, 0 for the world.
                sd.colorAttachments[0].writeMask = i == 0 ? MTLColorWriteMaskRed : MTLColorWriteMaskNone;
                _shadowPipelines[i] = [_device newRenderPipelineStateWithDescriptor:sd error:&error];
                if (!_shadowPipelines[i]) {
                    NSLog(@"NFSMW: shadow pipeline %d failed: %@", i, error);
                }
            }
        }
        if (lib) {
            MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = [lib newFunctionWithName:@"quad_vs"];
            pd.fragmentFunction = [lib newFunctionWithName:@"filter_fs"];
            pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            _filterPipeline = [_device newRenderPipelineStateWithDescriptor:pd error:&error];
            if (!_filterPipeline) {
                NSLog(@"NFSMW: filter pipeline failed: %@", error);
            }
            pd.fragmentFunction = [lib newFunctionWithName:@"radial_fs"];
            _radialPipeline = [_device newRenderPipelineStateWithDescriptor:pd error:&error];
            if (!_radialPipeline) {
                NSLog(@"NFSMW: radial blur pipeline failed: %@", error);
            }
            pd.fragmentFunction = [lib newFunctionWithName:@"overbright_add_fs"];
            pd.colorAttachments[0].blendingEnabled = YES;
            pd.colorAttachments[0].sourceRGBBlendFactor = pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
            pd.colorAttachments[0].destinationRGBBlendFactor = pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
            _flareAddPipeline = [_device newRenderPipelineStateWithDescriptor:pd error:&error];
            if (!_flareAddPipeline) {
                NSLog(@"NFSMW: flare add pipeline failed: %@", error);
            }
            pd.colorAttachments[0].blendingEnabled = NO;
            pd.fragmentFunction = [lib newFunctionWithName:@"overbright_fs"];
            _overbrightPipeline = [_device newRenderPipelineStateWithDescriptor:pd error:&error];
            if (!_overbrightPipeline) {
                NSLog(@"NFSMW: overbright pipeline failed: %@", error);
            }
            pd.fragmentFunction = [lib newFunctionWithName:@"treatment_fs"];
            pd.colorAttachments[0].pixelFormat = view.colorPixelFormat;
            pd.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
            pd.stencilAttachmentPixelFormat = view.depthStencilPixelFormat;
            _treatmentPipeline = [_device newRenderPipelineStateWithDescriptor:pd error:&error];
            if (!_treatmentPipeline) {
                NSLog(@"NFSMW: treatment pipeline failed: %@", error);
            }
            pd.fragmentFunction = [lib newFunctionWithName:@"copy_fs"];
            _copyPipeline = [_device newRenderPipelineStateWithDescriptor:pd error:&error];
            if (!_copyPipeline) {
                NSLog(@"NFSMW: copy pipeline failed: %@", error);
            }
            pd.fragmentFunction = [lib newFunctionWithName:@"overbright_add_fs"];
            pd.colorAttachments[0].blendingEnabled = YES;
            pd.colorAttachments[0].sourceRGBBlendFactor = pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
            pd.colorAttachments[0].destinationRGBBlendFactor = pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
            _overbrightAddPipeline = [_device newRenderPipelineStateWithDescriptor:pd error:&error];
            if (!_overbrightAddPipeline) {
                NSLog(@"NFSMW: overbright add pipeline failed: %@", error);
            }
            MTLRenderPipelineDescriptor *rd = [MTLRenderPipelineDescriptor new];
            rd.vertexFunction = [lib newFunctionWithName:@"drop_vs"];
            rd.fragmentFunction = [lib newFunctionWithName:@"drop_fs"];
            rd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            rd.colorAttachments[0].blendingEnabled = YES;
            rd.colorAttachments[0].sourceRGBBlendFactor = rd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
            rd.colorAttachments[0].destinationRGBBlendFactor = rd.colorAttachments[0].destinationAlphaBlendFactor =
                MTLBlendFactorOneMinusSourceAlpha;
            rd.colorAttachments[0].writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue; // COLORWRITEENABLE 7
            _dropPipeline = [_device newRenderPipelineStateWithDescriptor:rd error:&error];
            if (!_dropPipeline) {
                NSLog(@"NFSMW: rain drop pipeline failed: %@", error);
            }
        }
        // PC 0x6C5EE9: texture filtering 0 linear min/mag with point mips,
        // 1 linear mips, 2 anisotropic minification (caps MaxAnisotropy).
        for (int i = 0; i < 3; i++) {
            MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
            sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
            sd.mipFilter = i == 0 ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
            sd.maxAnisotropy = i == 2 ? 16 : 1;
            sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeRepeat;
            _meshSamplers[i] = [_device newSamplerStateWithDescriptor:sd];
        }
        MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionLessEqual;
        dd.depthWriteEnabled = YES;
        _depthWrite = [_device newDepthStencilStateWithDescriptor:dd];
        dd.depthWriteEnabled = NO;
        _depthTest = [_device newDepthStencilStateWithDescriptor:dd];
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        _depthOff = [_device newDepthStencilStateWithDescriptor:dd];
        dd.depthCompareFunction = MTLCompareFunctionEqual;
        _depthEqual = [_device newDepthStencilStateWithDescriptor:dd];

        // Bound in place of a texture for untextured frontend quads.
        MTLTextureDescriptor *wd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:1 height:1 mipmapped:NO];
        _white = [_device newTextureWithDescriptor:wd];
        uint32_t white = 0xFFFFFFFF;
        [_white replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&white bytesPerRow:4];
        _black = [_device newTextureWithDescriptor:wd];
        uint32_t opaque_black = 0xFF000000;
        [_black replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&opaque_black bytesPerRow:4];
        {
            MTLTextureDescriptor *ld = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:1 height:1 mipmapped:NO];
            ld.usage = MTLTextureUsageShaderRead;
            ld.storageMode = MTLStorageModeShared;
            _shadowDepthLit = [_device newTextureWithDescriptor:ld];
            float one = 1.0f;
            [_shadowDepthLit replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&one bytesPerRow:4];
        }
        MTLTextureDescriptor *bd = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm size:1 mipmapped:NO];
        _blackCube = [_device newTextureWithDescriptor:bd];
        uint32_t black = 0xFF000000;
        for (int i = 0; i < 6; i++) {
            [_blackCube replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 slice:i withBytes:&black bytesPerRow:4 bytesPerImage:4];
        }
    }
    return self;
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
}

// A mesh pipeline: blend mode, sample count 1 << si, colour write mask (0
// all, 1 rgb, 2 alpha), with or without the alpha test (kMeshAlphaTest).
- (id<MTLRenderPipelineState>)newMeshPipeline:(int)mode sampleIndex:(int)si mask:(int)mask alphaTest:(bool)alpha_test {
    MTLFunctionConstantValues *fc = [MTLFunctionConstantValues new];
    bool at = alpha_test;
    [fc setConstantValue:&at type:MTLDataTypeBool atIndex:0];
    NSError *error = nil;
    MTLRenderPipelineDescriptor *md = [MTLRenderPipelineDescriptor new];
    md.vertexFunction = [_lib newFunctionWithName:@"mesh_vs"];
    md.fragmentFunction = [_lib newFunctionWithName:@"mesh_fs" constantValues:fc error:&error];
    md.colorAttachments[0].pixelFormat = _colorFormat;
    md.depthAttachmentPixelFormat = _depthFormat;
    md.stencilAttachmentPixelFormat = _depthFormat;
    md.rasterSampleCount = (NSUInteger)(1 << si);
    MTLRenderPipelineColorAttachmentDescriptor *c = md.colorAttachments[0];
    c.blendingEnabled = mode != 0;
    c.sourceRGBBlendFactor = c.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor = mode == 1 ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorOne;
    c.writeMask = mask == 1 ? (MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue)
                : mask == 2 ? MTLColorWriteMaskAlpha
                            : MTLColorWriteMaskAll;
    id<MTLRenderPipelineState> p = md.fragmentFunction ? [_device newRenderPipelineStateWithDescriptor:md error:&error] : nil;
    if (p == nil) {
        NSLog(@"NFSMW: mesh pipeline %d/%d/%d/%d failed: %@", si, mode, mask, (int)at, error);
    }
    return p;
}

// The depth prepass pipeline for a pass of this sample count (made on first
// use; main thread).
- (id<MTLRenderPipelineState>)meshDepthPipeline:(int)samples {
    int si = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
    if (_meshDepthPSO[si] == nil && _lib) {
        MTLRenderPipelineDescriptor *md = [MTLRenderPipelineDescriptor new];
        md.vertexFunction = [_lib newFunctionWithName:@"mesh_vs"];
        md.colorAttachments[0].pixelFormat = _colorFormat;
        md.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
        md.depthAttachmentPixelFormat = _depthFormat;
        md.stencilAttachmentPixelFormat = _depthFormat;
        md.rasterSampleCount = (NSUInteger)(1 << si);
        NSError *error = nil;
        _meshDepthPSO[si] = [_device newRenderPipelineStateWithDescriptor:md error:&error];
        if (_meshDepthPSO[si] == nil) {
            NSLog(@"NFSMW: mesh depth pipeline %d failed: %@", si, error);
        }
    }
    return _meshDepthPSO[si];
}

// The mesh pipeline for a blend mode in a pass of this sample count and
// colour write mask, with or without the alpha test. Made on first use (main
// thread); the 1-sample, all channels ones (the env-map faces', from a
// worker thread) always exist.
- (id<MTLRenderPipelineState>)meshPipeline:(int)mode samples:(int)samples mask:(int)mask alphaTest:(bool)alpha_test {
    int si = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
    id<MTLRenderPipelineState> __strong &p = _meshPSO[si][mode][mask][alpha_test ? 1 : 0];
    if (p == nil && _lib) {
        p = [self newMeshPipeline:mode sampleIndex:si mask:mask alphaTest:alpha_test];
        if (p == nil) {
            return _meshPipelines[mode];
        }
    }
    return p;
}

// The IDI_PARTICLES_FX pipeline for a blend mode in the current pass.
- (id<MTLRenderPipelineState>)particlePipeline:(int)mode {
    int si = _passSamples >= 8 ? 3 : _passSamples >= 4 ? 2 : _passSamples >= 2 ? 1 : 0;
    if (_particlePSO[si][mode] == nil && _lib) {
        MTLRenderPipelineDescriptor *md = [MTLRenderPipelineDescriptor new];
        md.vertexFunction = [_lib newFunctionWithName:@"particle_vs"];
        md.fragmentFunction = [_lib newFunctionWithName:@"particle_fs"];
        md.colorAttachments[0].pixelFormat = _colorFormat;
        md.depthAttachmentPixelFormat = _depthFormat;
        md.stencilAttachmentPixelFormat = _depthFormat;
        md.rasterSampleCount = (NSUInteger)(1 << si);
        MTLRenderPipelineColorAttachmentDescriptor *c = md.colorAttachments[0];
        c.blendingEnabled = mode != 0;
        c.sourceRGBBlendFactor = c.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor = mode == 1 ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorOne;
        c.writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue; // COLORWRITEENABLE 7
        NSError *error = nil;
        _particlePSO[si][mode] = [_device newRenderPipelineStateWithDescriptor:md error:&error];
        if (_particlePSO[si][mode] == nil) {
            NSLog(@"NFSMW: particle pipeline %d/%d failed: %@", si, mode, error);
        }
    }
    return _particlePSO[si][mode];
}

// The frame's IDI_PARTICLES_FX batches after the world: ZFUNC LESSEQUAL, no
// depth writes (___eDisplayFrame sets ZWRITEENABLE 0 around them), each
// batch with its texture's blending (sub_6C1010).
- (void)drawParticles:(const Frame3D &)scene encoder:(id<MTLRenderCommandEncoder>)enc commandBuffer:(id<MTLCommandBuffer>)cmd {
    if (scene.particles.empty()) {
        return;
    }
    id<MTLBuffer> vb = port_upload::Acquire(_device, scene.particle_verts.data(), scene.particle_verts.size() * sizeof(PortParticleVertex));
    port_upload::Retire(vb, cmd);
    [enc setCullMode:MTLCullModeNone];
    [enc setDepthStencilState:_depthTest];
    [enc setVertexBuffer:vb offset:0 atIndex:0];
    for (const Frame3D::Particles &p : scene.particles) {
        int mode = (p.flags & PORT_3D_ADDITIVE) ? 2 : (p.flags & PORT_3D_ALPHA_BLEND) ? 1 : 0;
        id<MTLRenderPipelineState> pso = [self particlePipeline:mode];
        if (pso == nil) {
            continue;
        }
        struct {
            float mvp[16];
            float shadow[16];
            uint32_t alpha_test;
            uint32_t shadowed;
            uint32_t pad[2];
        } c = {};
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                float sum = 0, shadow = 0;
                for (int k = 0; k < 4; k++) {
                    sum += p.world[i * 4 + k] * scene.view_proj[k * 4 + j];
                    shadow += p.world[i * 4 + k] * _shadowTransform[k * 4 + j];
                }
                c.mvp[i * 4 + j] = sum;
                c.shadow[i * 4 + j] = shadow;
            }
        }
        c.alpha_test = (p.flags & PORT_3D_ALPHA_TEST) ? 1 : 0;
        c.shadowed = _shadowValid ? 1 : 0;
        [enc setRenderPipelineState:pso];
        [enc setVertexBytes:&c length:sizeof(c) atIndex:1];
        [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
        [enc setFragmentTexture:(p.texture ? p.texture : _white) atIndex:0];
        [enc setFragmentSamplerState:_meshSamplers[gTextureFiltering.load() % 3] atIndex:0];
        [enc setFragmentTexture:(_shadowValid ? _shadowDepth : _shadowDepthLit) atIndex:1];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:p.first vertexCount:p.count];
    }
}

// sub_6D4620: the frame copied (sub_6D2CD0 into the other target), then
// each drop drawn over it from the copy.
- (void)drawRainDrops:(const Frame3D &)scene commandBuffer:(id<MTLCommandBuffer>)cmd {
    if (scene.drops.empty() || !scene.drop_displace || !_dropPipeline) {
        return;
    }
    if (!_dropSource || _dropSource.width != _sceneColor.width || _dropSource.height != _sceneColor.height) {
        MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:_sceneColor.width
                                                                                     height:_sceneColor.height
                                                                                  mipmapped:NO];
        cd.usage = MTLTextureUsageShaderRead;
        cd.storageMode = MTLStorageModePrivate;
        _dropSource = [_device newTextureWithDescriptor:cd];
    }
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:_sceneColor toTexture:_dropSource];
    [blit endEncoding];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = _sceneColor;
    rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    enc.label = @"Rain drops";
    [enc setRenderPipelineState:_dropPipeline];
    [enc setFragmentTexture:_dropSource atIndex:0];
    [enc setFragmentTexture:scene.drop_displace atIndex:1];
    for (const PortRainDrop &d : scene.drops) {
        // raindropoffset = (x - s/4, y + s/4, s, -s); the quad from (2x - 1, 1 - 2y).
        struct {
            float quad[4];
            float offset[4];
            float alpha;
            float pad[3];
        } c = {{2.0f * d.x - 1.0f, 1.0f - 2.0f * d.y, d.size, 0},
               {d.x - 0.25f * d.size, d.y + 0.25f * d.size, d.size, -d.size},
               d.alpha,
               {0, 0, 0}};
        [enc setVertexBytes:&c length:sizeof(c) atIndex:0];
        [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }
    [enc endEncoding];
}

// This frame's EnvMapTex, or nil (shader fallback).
- (id<MTLTexture>)envMapFor:(const Frame3D &)scene {
    if (scene.env_mode == PORT_ENVMAP_STATIC) {
        return scene.env_static;
    }
    if (scene.env_mode == PORT_ENVMAP_DYNAMIC && _envCube && _envFacesDrawn == 0x3F) {
        return _envCube;
    }
    return nil;
}

// drawDraws' state now (see PassState).
- (PassState)passState {
    PassState st;
    st.samples = _passSamples;
    st.mask = _passMask;
    st.main_scene = _passMainScene;
    st.skip_sky_alpha = _skipSkyAlpha;
    st.only_sky_alpha = _onlySkyAlpha;
    st.env_face = _drawingEnvFace;
    st.sky = _sky;
    for (int i = 0; i < 3; i++) {
        st.sky_misc[i] = _skyMisc[i];
    }
    st.shadow_valid = _shadowValid;
    st.shadow_depth = _shadowDepth;
    st.shadow_colour = _shadowColour;
    st.shadow_size = _shadowSize;
    memcpy(st.shadow_transform, _shadowTransform, sizeof(st.shadow_transform));
    st.inv_shadow_strength = _invShadowStrength;
    st.refl_bound = _reflBound;
    return st;
}

- (void)drawScene:(const Frame3D &)scene encoder:(id<MTLRenderCommandEncoder>)enc view:(MTKView *)view {
    CGSize size = view.drawableSize;
    [self drawDraws:scene.draws viewProj:scene.view_proj eye:scene.eye fog:scene.fog envMap:[self envMapFor:scene]
              width:size.width height:size.height encoder:enc state:[self passState]];
}

// PC ___eDisplayFrame 0x6DE64C: with the env map on, in game, each active
// env-map view is drawn into its cube face (cleared black, sub_6BD3B0)
// before the player view. 256x256 (0x8F8FF4), A8R8G8B8 (0x8F8FF8).
// The faces are encoded on a worker thread into their own command buffer,
// enqueued now (so the GPU runs them before this frame's other passes, as
// before) while the main thread encodes the rest; drawInMTKView waits for it
// (_envGroup) before the next frame is taken.
- (void)drawEnvFaces:(const Frame3D &)scene view:(MTKView *)view {
    if (scene.env_mode != PORT_ENVMAP_DYNAMIC || !_meshPipelines[0]) {
        return;
    }
    const NSUInteger size = 256;
    if (_envCube == nil) {
        MTLTextureDescriptor *cd = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm size:size mipmapped:NO];
        cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        cd.storageMode = MTLStorageModePrivate;
        _envCube = [_device newTextureWithDescriptor:cd];
        _envCube.label = @"EnvMap cube";
        MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                                                      width:size
                                                                                     height:size
                                                                                  mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModeMemoryless;
        _envDepth = [_device newTextureWithDescriptor:dd];
        _envFacesDrawn = 0;
    }
    unsigned faces = 0;
    for (int face = 0; face < 6; face++) {
        if (scene.env_faces[face].active) {
            faces |= 1u << face;
        }
    }
    if (faces == 0) {
        return;
    }
    _envFacesDrawn |= faces;
    id<MTLCommandBuffer> cmd = [_queue commandBuffer];
    cmd.label = @"EnvMap faces";
    [cmd enqueue];
    PortMacBenchmarkExtraCommandBuffer(cmd, view);
    _drawingEnvFace = true;
    const PassState st = [self passState];
    _drawingEnvFace = false;
    const Frame3D *frame = &scene; // _scene: not swapped until the faces are done
    id<MTLTexture> cube = _envCube, depth = _envDepth;
    dispatch_group_async(_envGroup, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
      for (int face = 0; face < 6; face++) {
          const EnvFace3D &f = frame->env_faces[face];
          if (!f.active) {
              continue;
          }
          MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
          rp.colorAttachments[0].texture = cube;
          rp.colorAttachments[0].slice = face;
          rp.colorAttachments[0].loadAction = MTLLoadActionClear;
          rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
          rp.colorAttachments[0].storeAction = MTLStoreActionStore;
          rp.depthAttachment.texture = depth;
          rp.depthAttachment.loadAction = MTLLoadActionClear;
          rp.depthAttachment.clearDepth = 1.0;
          rp.depthAttachment.storeAction = MTLStoreActionDontCare;
          rp.stencilAttachment.texture = depth;
          rp.stencilAttachment.loadAction = MTLLoadActionClear;
          rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
          id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
          enc.label = @"EnvMap face";
          [self drawDraws:f.draws viewProj:f.view_proj eye:f.eye fog:frame->fog envMap:nil width:size height:size encoder:enc state:st];
          // ___eDisplayFrame 0x6DE7F4: the face's world (type 1), pool and car
          // flares under IDI_WORLDPRELIT_FX.
          PortMetalPrelitDraw((__bridge void *)enc, (__bridge void *)_device, f.view_proj, 1, (int)_colorFormat, (int)_depthFormat,
                              PORT_PRELIT_PART_ENV_FACE + face);
          [enc endEncoding];
      }
      [cmd commit];
    });
}

// PC sub_6E54E0 / sub_6E44C0: the shadow view (render target type 4,
// cleared to black and depth 1 by sub_6BD3B0) before the player view, with
// D3DRS_DEPTHBIAS 4 * 2^-24 and D3DRS_SLOPESCALEDEPTHBIAS 4 (Metal's
// constant bias is in units of the float depth's resolution there: the same
// for depths in 0.5..1). IDI_CAR_SHADOW_MAP_FX writes red; the world depth
// only. Guess: alpha-blended world draws (no depth writes in the main pass)
// and additive draws cast no shadow.
- (void)drawShadowMap:(const Frame3D &)scene commandBuffer:(id<MTLCommandBuffer>)cmd {
    _shadowValid = false;
    if (!scene.shadow_active || !scene.shadow_transform_set || !_shadowPipelines[0] || !_shadowPipelines[1]) {
        return;
    }
    // PC: 1024 (sub_6C8770, sub_6C86B0). Natively about the render height
    // (Shadow Detail 1) or twice it (2), a power of two in 1024..4096.
    CGFloat height = _drawableHeight > 0 ? _drawableHeight : 1024;
    NSUInteger want = (NSUInteger)(height * (scene.shadow_detail >= 2 ? 2.0 : 1.0));
    NSUInteger size = 1024;
    while (size < want && size < 4096) {
        size *= 2;
    }
    if (_shadowDepth != nil && _shadowDepth.width != size) {
        _shadowDepth = nil;
        _shadowColour = nil;
    }
    _shadowSize = size;
    if (_shadowDepth == nil) {
        MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                      width:size
                                                                                     height:size
                                                                                  mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        dd.storageMode = MTLStorageModePrivate;
        _shadowDepth = [_device newTextureWithDescriptor:dd];
        _shadowDepth.label = @"Shadow map depth";
        MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                      width:size
                                                                                     height:size
                                                                                  mipmapped:NO];
        cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        cd.storageMode = MTLStorageModePrivate;
        _shadowColour = [_device newTextureWithDescriptor:cd];
        _shadowColour.label = @"Shadow colour map";
    }
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = _shadowColour;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.depthAttachment.texture = _shadowDepth;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.clearDepth = 1.0;
    rp.depthAttachment.storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    enc.label = @"Shadow map";
    [enc setViewport:(MTLViewport){0, 0, (double)size, (double)size, 0, 1}];
    [enc setCullMode:MTLCullModeNone];
    [enc setDepthStencilState:_depthWrite];
    [enc setDepthBias:4.0f slopeScale:4.0f clamp:0.0f];
    [enc setFragmentSamplerState:_meshSamplers[gTextureFiltering.load() % 3] atIndex:0];
    int bound = -1;
    id<MTLBuffer> bound_vertices = nil;
    id<MTLTexture> bound_texture = nil;
    for (const Draw3D &d : scene.shadow_draws) {
        bool car = d.effect == 4;
        if ((d.flags & PORT_3D_ADDITIVE) || (!car && (d.flags & PORT_3D_ALPHA_BLEND))) {
            continue;
        }
        struct {
            float mvp[16];
            uint32_t stride;
            uint32_t alpha_test;
            uint32_t pad[2];
        } c = {};
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                float sum = 0;
                for (int k = 0; k < 4; k++) {
                    sum += d.world[i * 4 + k] * scene.shadow_view_proj[k * 4 + j];
                }
                c.mvp[i * 4 + j] = sum;
            }
        }
        c.stride = d.vertex_stride;
        c.alpha_test = (!car && (d.flags & PORT_3D_ALPHA_TEST)) ? 1 : 0;
        int pipeline = car ? 0 : 1;
        if (pipeline != bound) {
            [enc setRenderPipelineState:_shadowPipelines[pipeline]];
            bound = pipeline;
        }
        if (d.vertex_buffer != bound_vertices) {
            [enc setVertexBuffer:d.vertex_buffer offset:0 atIndex:0];
            bound_vertices = d.vertex_buffer;
        }
        [enc setVertexBytes:&c length:sizeof(c) atIndex:1];
        [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
        id<MTLTexture> texture = d.texture ? d.texture : _white;
        if (texture != bound_texture) {
            [enc setFragmentTexture:texture atIndex:0];
            bound_texture = texture;
        }
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:d.num_indices
                         indexType:MTLIndexTypeUInt16
                       indexBuffer:d.index_buffer
                 indexBufferOffset:d.first_index * 2];
    }
    [enc endEncoding];
    _shadowValid = true;
}

- (void)drawDraws:(const std::vector<Draw3D> &)draws viewProj:(const float *)view_proj eye:(const float *)eye fog:(const PortWorldFog &)fog
           envMap:(id<MTLTexture>)env_map width:(double)width height:(double)height encoder:(id<MTLRenderCommandEncoder>)enc
            state:(const PassState &)st {
    if (!draws.empty() && _meshPipelines[0]) {
        // The camera's x axis: view_proj = view * proj with proj diagonal in
        // x (ViewProjectionFor), so view column 0 is view_proj column 0 / sx.
        float view_x[4] = {view_proj[0], view_proj[4], view_proj[8], 0};
        float len = sqrtf(view_x[0] * view_x[0] + view_x[1] * view_x[1] + view_x[2] * view_x[2]);
        if (len > 0) {
            for (int i = 0; i < 3; i++) {
                view_x[i] /= len;
            }
        }
        static const bool trace_effects = getenv("NFSMW_TRACE_EFFECTS") != nullptr;
        if (trace_effects && width > 256) {
            static int frame;
            if (frame++ % 300 == 0) {
                int counts[32] = {}, second[32] = {};
                for (const Draw3D &d : draws) {
                    counts[d.effect & 31]++;
                    second[d.effect & 31] += d.texture2 != nil;
                }
                fprintf(stderr, "[effects]");
                for (int i = 0; i < 32; i++) {
                    if (counts[i]) {
                        fprintf(stderr, " %d:%d(%d)", i, counts[i], second[i]);
                    }
                }
                fprintf(stderr, "\n");
            }
        }
        // Opaque first, then blended in submission order.
        [enc setViewport:(MTLViewport){0, 0, width, height, 0, 1}];
        [enc setCullMode:MTLCullModeNone];
        // The bindings every draw of this list shares: bound once. Per draw
        // only what differs from the draw before is bound again (the
        // encoder keeps the rest), which spares the driver re-emitting it.
        [enc setVertexBytes:&fog length:sizeof(fog) atIndex:2];
        [enc setFragmentBytes:&fog length:sizeof(fog) atIndex:4];
        struct {
            float scroll;
            float pad[3];
            float intensity[4];
        } sky = {st.sky.cloud_scroll, {0, 0, 0},
                 {st.sky.cloud_intensity[0], st.sky.cloud_intensity[1], st.sky.cloud_intensity[2], st.sky.cloud_intensity[3]}};
        [enc setVertexBytes:&sky length:sizeof(sky) atIndex:3];
        [enc setFragmentSamplerState:_meshSamplers[gTextureFiltering.load() % 3] atIndex:0];
        [enc setFragmentTexture:(env_map ? env_map : _blackCube) atIndex:1];
        [enc setFragmentTexture:(st.shadow_valid ? st.shadow_depth : _shadowDepthLit) atIndex:4];
        [enc setFragmentTexture:(st.shadow_valid ? st.shadow_colour : _black) atIndex:5];
        [enc setFragmentTexture:(st.refl_bound ? st.refl_bound : _white) atIndex:9];
        // The sky's constants and textures are the same for the whole list;
        // bound up front so every mesh_fs draw has buffer 5 (Metal validation).
        [enc setFragmentBytes:&sky length:sizeof(sky) atIndex:5];
        for (int i = 0; i < 3; i++) {
            [enc setFragmentTexture:(st.sky_misc[i] ? st.sky_misc[i] : _black) atIndex:6 + i];
        }
        struct Bound {
            id<MTLRenderPipelineState> pipeline;
            id<MTLDepthStencilState> depth;
            id<MTLBuffer> vertices;
            id<MTLTexture> textures[4]; // 0, 2, 3 (1 is the list's)
            const Draw3D *car_consts;   // whose material / lights / envmap are bound
            // The MeshConst fields mesh_fs reads (shade, effect, pad, eye,
            // the shadow ones; not the matrices) of the constants bound for
            // the fragment stage, valid when has_fragment.
            bool has_fragment;
            uint32_t fragment[12];
        } bound = {};
        auto bindPipeline = [&](id<MTLRenderPipelineState> p) {
            if (p != bound.pipeline) {
                [enc setRenderPipelineState:p];
                bound.pipeline = p;
            }
        };
        auto bindDepth = [&](id<MTLDepthStencilState> ds) {
            if (ds != bound.depth) {
                [enc setDepthStencilState:ds];
                bound.depth = ds;
            }
        };
        auto bindTexture = [&](id<MTLTexture> t, int index) {
            if (t != bound.textures[index]) {
                [enc setFragmentTexture:t atIndex:index];
                bound.textures[index] = t;
            }
        };
        id<MTLRenderPipelineState> pipelines[3][2] = {}; // [mode][alpha test]
        // Pass 2 (the player view's frame only): ___eDisplayFrame 0x6DF3C3
        // draws the sky again with ColorWriteMode 8 (alpha only) and ZFUNC
        // LESSEQUAL after the world, so the frame's alpha (cleared to 0, the
        // world drawn with ColorWriteMode 7) holds the sky's: what over
        // bright's downsample (IDI_FILTER_FX p5) scales by.
        // mvp = world * view_proj (row vectors).
        auto worldViewProj = [&](const Draw3D &d, float *mvp) {
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    float sum = 0;
                    for (int k = 0; k < 4; k++) {
                        sum += d.world[i * 4 + k] * view_proj[k * 4 + j];
                    }
                    mvp[i * 4 + j] = sum;
                }
            }
        };
        // The player view: a depth prepass of the draws whose depth is
        // unconditional (opaque, depth-writing, no alpha test; not the sky,
        // which only tests), with the same vertex function. Then each pixel
        // sample's colour still comes from the last of the draws at the
        // nearest depth (LESSEQUAL), as without it, but the GPU shades the
        // hidden ones' pixels no more. The other passes have little
        // overdraw: measured no faster.
        id<MTLRenderPipelineState> depth_pipeline = st.main_scene && !st.only_sky_alpha ? [self meshDepthPipeline:st.samples] : nil;
        if (depth_pipeline) {
            [enc setRenderPipelineState:depth_pipeline];
            bindDepth(_depthWrite);
            MeshConstants c = {};
            bool first = true;
            for (const Draw3D &d : draws) {
                if ((d.flags & (PORT_3D_ADDITIVE | PORT_3D_ALPHA_BLEND | PORT_3D_ALPHA_TEST)) || d.effect == 19) {
                    continue;
                }
                if (first) {
                    // Read by mesh_vs, not by the position: bound for the
                    // pipeline's arguments, the draws' own bound later.
                    [enc setVertexBytes:d.material length:sizeof(d.material) atIndex:4];
                    [enc setVertexBytes:d.lights length:sizeof(d.lights) atIndex:5];
                    [enc setVertexBytes:d.envmap length:sizeof(d.envmap) atIndex:6];
                    first = false;
                }
                c.stride = d.vertex_stride;
                c.effect = d.effect;
                worldViewProj(d, c.mvp);
                if (d.vertex_buffer != bound.vertices) {
                    [enc setVertexBuffer:d.vertex_buffer offset:0 atIndex:0];
                    bound.vertices = d.vertex_buffer;
                }
                [enc setVertexBytes:&c length:sizeof(c) atIndex:1];
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:d.num_indices
                                 indexType:MTLIndexTypeUInt16
                               indexBuffer:d.index_buffer
                         indexBufferOffset:d.first_index * 2];
            }
        }
        int mask = st.mask;
        for (int blended = st.only_sky_alpha ? 2 : 0; blended < (st.main_scene && !st.skip_sky_alpha ? 3 : 2); blended++) {
            if (blended == 2) {
                mask = 2;
            }
            for (int m = 0; m < 3; m++) {
                for (int at = 0; at < 2; at++) {
                    pipelines[m][at] = [self meshPipeline:m samples:st.samples mask:mask alphaTest:at != 0];
                }
            }
            for (const Draw3D &d : draws) {
                int mode = (d.flags & PORT_3D_ADDITIVE) ? 2 : (d.flags & PORT_3D_ALPHA_BLEND) ? 1 : 0;
                if (d.effect == 19) {
                    mode = 0; // IDI_SKYBOX_FX p0: ALPHABLENDENABLE 0
                }
                if (blended == 2) {
                    if (d.effect != 19) {
                        continue;
                    }
                } else if ((mode != 0) != (blended != 0)) {
                    continue;
                }
                MeshConstants c = {};
                c.stride = d.vertex_stride;
                c.effect = d.effect;
                if (st.env_face) {
                    // eViewPlatInterface::Render 0x6DA9B0: in views other than
                    // the main one (render target type != 0), WORLD draws with
                    // WORLDNOFOG (8) and WORLDREFLECT / WORLDNORMALMAP with
                    // WORLDNORMALMAPNOFOG (30, fixed function); both are
                    // 2 x texture x colour, alpha texture x colour alpha.
                    if (d.effect == 0) {
                        c.effect = 8;
                    } else if (d.effect == 1 || d.effect == 3) {
                        c.effect = 30;
                    }
                }
                memcpy(c.eye, eye, sizeof(float) * 3);
                if (d.effect == 22) {
                    // sub_6C6630: ShadowColor.w = 0x8FAE68, x 2 off the main
                    // render target.
                    c.eye[3] = d.material[0] * (st.env_face ? 2.0f : 1.0f);
                }
                memcpy(c.view_x, view_x, sizeof(view_x));
                static const char *shade_env = getenv("NFSMW_SHADE");
                c.shade = (shade_env && strcmp(shade_env, "texture") == 0) ? 1 : 0;
                if (shade_env && strcmp(shade_env, "envmap") == 0) {
                    c.shade |= 32;
                }
                if (shade_env && strcmp(shade_env, "shadow") == 0) {
                    c.shade |= 256;
                }
                if (shade_env && strcmp(shade_env, "vcolour") == 0) {
                    c.shade |= 512;
                }
                if (d.flags & PORT_3D_ALPHA_TEST) {
                    c.shade |= 2;
                }
                if (d.flags & PORT_3D_LIT) {
                    c.shade |= 4;
                }
                if (env_map) {
                    c.shade |= 8;
                }
                if (d.flags & PORT_3D_ENVMAP) {
                    c.shade |= 16;
                }
                if (d.effect == 2 && d.bones) {
                    c.shade |= 1024; // WORLDBONE with its BlendMatrices
                }
                if ((c.effect == 1 || c.effect == 3) && d.texture2 && d.texture3 && d.vertex_stride >= 60) {
                    c.shade |= 128;
                }
                // Shadow level (sub_6E0AB0, see Port3DDraw::shadow_level): the
                // technique of the shadowed effects (CAR #0..#2, WORLD #0..#2,
                // ...), with ShadowTransform = world x the shadow view's
                // texture matrix (sub_6C82B0).
                c.shadow_level = d.shadow_level;
                c.shadow_map = st.shadow_valid ? 1 : 0;
                c.shadow_texel = 1.0f / (float)(st.shadow_size ? st.shadow_size : 1024);
                c.inv_shadow = st.inv_shadow_strength;
                if (d.shadow_level != 0) {
                    for (int i = 0; i < 4; i++) {
                        for (int j = 0; j < 4; j++) {
                            float sum = 0;
                            for (int k = 0; k < 4; k++) {
                                sum += d.world[i * 4 + k] * st.shadow_transform[k * 4 + j];
                            }
                            c.shadow[i * 4 + j] = sum;
                        }
                    }
                }
                worldViewProj(d, c.mvp);
                memcpy(c.world, d.world, sizeof(c.world));
                bindPipeline(pipelines[mode][(c.shade & 2) ? 1 : 0]);
                // D3D default ZWRITEENABLE stays on: no PC effect technique
                // turns it off, so alpha-blended car glass writes depth too
                // (otherwise the sky's alpha pass shows through the windows
                // and over bright blooms them). Additive draws (glows) and the
                // sky alpha pass only test.
                bindDepth(mode != 2 && blended != 2 && d.effect != 19 ? _depthWrite : _depthTest);
                if (d.vertex_buffer != bound.vertices) {
                    [enc setVertexBuffer:d.vertex_buffer offset:0 atIndex:0];
                    bound.vertices = d.vertex_buffer;
                }
                [enc setVertexBytes:&c length:sizeof(c) atIndex:1];
                bindTexture(d.texture ? d.texture : _white, 0);
                bindTexture(d.texture2 ? d.texture2 : _black, 2);
                bindTexture(d.texture3 ? d.texture3 : _black, 3);
                uint32_t fragment[12];
                memcpy(&fragment[0], &c.shade, sizeof(uint32_t) * 3); // shade, effect, pad
                memcpy(&fragment[3], c.eye, sizeof(c.eye));
                memcpy(&fragment[7], &c.inv_shadow, sizeof(uint32_t) * 4); // inv_shadow .. shadow_texel
                fragment[11] = 0;
                if (!bound.has_fragment || memcmp(fragment, bound.fragment, sizeof(fragment)) != 0) {
                    [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
                    memcpy(bound.fragment, fragment, sizeof(fragment));
                    bound.has_fragment = true;
                }
                // The car effect's vertex constants (IDI_CAR_FX vs #11/#9/...).
                if (bound.car_consts == nullptr || memcmp(bound.car_consts->material, d.material, sizeof(d.material)) != 0 ||
                    memcmp(bound.car_consts->lights, d.lights, sizeof(d.lights)) != 0 ||
                    memcmp(bound.car_consts->envmap, d.envmap, sizeof(d.envmap)) != 0) {
                    [enc setVertexBytes:d.material length:sizeof(d.material) atIndex:4];
                    [enc setVertexBytes:d.lights length:sizeof(d.lights) atIndex:5];
                    [enc setVertexBytes:d.envmap length:sizeof(d.envmap) atIndex:6];
                    bound.car_consts = &d;
                }
                if (d.effect == 2 && d.bones) { // IDI_WORLDBONE_FX BlendMatrices
                    [enc setVertexBytes:d.bones length:sizeof(float) * 256 atIndex:7];
                }
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:d.num_indices
                                 indexType:MTLIndexTypeUInt16
                               indexBuffer:d.index_buffer
                         indexBufferOffset:d.first_index * 2];
                if (c.effect == 19 && !(c.shade & 1)) {
                    // IDI_SKYBOX_FX passes p1 and p2: SRCALPHA / INVSRCALPHA,
                    // ZFUNC EQUAL, no depth writes.
                    bindPipeline(pipelines[1][(c.shade & 2) ? 1 : 0]);
                    bindDepth(_depthEqual);
                    for (uint32_t pass = 1; pass <= 2; pass++) {
                        c.pad = pass;
                        [enc setVertexBytes:&c length:sizeof(c) atIndex:1];
                        [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
                        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                        indexCount:d.num_indices
                                         indexType:MTLIndexTypeUInt16
                                       indexBuffer:d.index_buffer
                                 indexBufferOffset:d.first_index * 2];
                    }
                    bound.has_fragment = false; // pad differs
                }
            }
        }
    }
}

// Offscreen targets for the 3D frame, sized like the drawable: the frame
// (multisampled and memoryless when FSAA is on, resolved into _sceneColor)
// and the quarter-size bloom / over bright targets.
- (void)ensureSceneTargets:(CGSize)size samples:(int)samples {
    NSUInteger w = (NSUInteger)size.width, h = (NSUInteger)size.height;
    if (_sceneColor && _sceneColor.width == w && _sceneColor.height == h && _sceneSamples == samples) {
        return;
    }
    _sceneSamples = samples;
    MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:w
                                                                                 height:h
                                                                              mipmapped:NO];
    cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    cd.storageMode = MTLStorageModePrivate;
    _sceneColor = [_device newTextureWithDescriptor:cd];
    MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                                                  width:w
                                                                                 height:h
                                                                              mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget;
    dd.storageMode = MTLStorageModeMemoryless;
    if (samples > 1) {
        dd.textureType = MTLTextureType2DMultisample;
        dd.sampleCount = samples;
        MTLTextureDescriptor *md = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:w
                                                                                     height:h
                                                                                  mipmapped:NO];
        md.textureType = MTLTextureType2DMultisample;
        md.sampleCount = samples;
        md.usage = MTLTextureUsageRenderTarget;
        md.storageMode = MTLStorageModeMemoryless;
        _sceneColorMS = [_device newTextureWithDescriptor:md];
    } else {
        _sceneColorMS = nil;
    }
    _sceneDepth = [_device newTextureWithDescriptor:dd];
    // PC 0x6C311D: the bloom targets are a quarter of the back buffer.
    cd.width = w / 4 > 0 ? w / 4 : 1;
    cd.height = h / 4 > 0 ? h / 4 : 1;
    for (int i = 0; i < 2; i++) {
        _bloom[i] = [_device newTextureWithDescriptor:cd];
        _bloom[i].label = i == 0 ? @"Bloom 0x982964" : @"Bloom 0x982968";
        _ob[i] = [_device newTextureWithDescriptor:cd];
        _ob[i].label = i == 0 ? @"Over bright 0x982964" : @"Over bright 0x982968";
    }
    _obSource = [_device newTextureWithDescriptor:cd];
    _obSource.label = @"Over bright source 0x98296C";
    _mb[0] = _mb[1] = nil;
    _sceneColorMSKept = _sceneDepthKept = nil;
}

// The motion blur's targets and the kept (stored) multisampled colour and
// depth, made on first use at the frame's size.
- (void)ensureMotionBlurTargets {
    NSUInteger w = _sceneColor.width, h = _sceneColor.height;
    if (_mb[0] != nil) {
        return;
    }
    MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:w
                                                                                 height:h
                                                                              mipmapped:NO];
    cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    cd.storageMode = MTLStorageModePrivate;
    for (int i = 0; i < 2; i++) {
        _mb[i] = [_device newTextureWithDescriptor:cd];
        _mb[i].label = i == 0 ? @"Motion blur 0x93DE60" : @"Motion blur 0x93DE64";
    }
    MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                                                  width:w
                                                                                 height:h
                                                                              mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget;
    dd.storageMode = MTLStorageModePrivate;
    if (_sceneSamples > 1) {
        dd.textureType = MTLTextureType2DMultisample;
        dd.sampleCount = _sceneSamples;
        MTLTextureDescriptor *md = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:w
                                                                                     height:h
                                                                                  mipmapped:NO];
        md.textureType = MTLTextureType2DMultisample;
        md.sampleCount = _sceneSamples;
        md.usage = MTLTextureUsageRenderTarget;
        md.storageMode = MTLStorageModePrivate;
        _sceneColorMSKept = [_device newTextureWithDescriptor:md];
    }
    _sceneDepthKept = [_device newTextureWithDescriptor:dd];
}

// sub_6D3B80's composites over the frame in a scene pass with this many
// samples (depth attached, untouched): mode 0 BLENDFACTOR / INVBLENDFACTOR
// (the motion blur), mode 1 ONE / ONE (the X360 flare target).
- (id<MTLRenderPipelineState>)compositePipeline:(int)mode samples:(int)samples {
    int si = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
    if (_compositePSO[mode][si] != nil) {
        return _compositePSO[mode][si];
    }
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [_lib newFunctionWithName:@"quad_vs"];
    pd.fragmentFunction = [_lib newFunctionWithName:@"texture_fs"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    pd.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    pd.rasterSampleCount = (NSUInteger)(1 << si);
    MTLRenderPipelineColorAttachmentDescriptor *c = pd.colorAttachments[0];
    c.blendingEnabled = YES;
    c.sourceRGBBlendFactor = c.sourceAlphaBlendFactor = mode == 0 ? MTLBlendFactorBlendColor : MTLBlendFactorOne;
    c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor = mode == 0 ? MTLBlendFactorOneMinusBlendColor : MTLBlendFactorOne;
    NSError *error = nil;
    _compositePSO[mode][si] = [_device newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!_compositePSO[mode][si]) {
        NSLog(@"NFSMW: composite pipeline %d failed: %@", mode, error);
    }
    if (mode == 1 && _flareFadePipeline[si] == nil) {
        pd.fragmentFunction = [_lib newFunctionWithName:@"flare_fade_fs"];
        c.blendingEnabled = NO;
        _flareFadePipeline[si] = [_device newRenderPipelineStateWithDescriptor:pd error:&error];
        if (!_flareFadePipeline[si]) {
            NSLog(@"NFSMW: flare fade pipeline failed: %@", error);
        }
    }
    return _compositePSO[mode][si];
}

// PC sub_6DAE20 (g_X360EffectsEnable, in place of the flares at
// ___eDisplayFrame 0x6DF2A3): the flare target 0x982960 faded
// (BLENDFACTOR 0xFF786482), copied into the FSAA surface with FSAA, the
// world, car and pool flares drawn into it under IDI_WORLDPRELIT_FX with the
// frame's depth (z test, no writes), resolved back.
- (void)drawX360Flares:(const Frame3D &)scene samples:(int)samples commandBuffer:(id<MTLCommandBuffer>)cmd {
    NSUInteger w = _sceneColor.width, h = _sceneColor.height;
    if (_flare[0] == nil || _flare[0].width != w || _flare[0].height != h) {
        // 0x6C3000: created with the back buffer's size and cleared.
        MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:w
                                                                                     height:h
                                                                                  mipmapped:NO];
        cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        cd.storageMode = MTLStorageModePrivate;
        for (int i = 0; i < 2; i++) {
            _flare[i] = [_device newTextureWithDescriptor:cd];
            _flare[i].label = @"X360 flares 0x982960";
            MTLRenderPassDescriptor *cp = [MTLRenderPassDescriptor renderPassDescriptor];
            cp.colorAttachments[0].texture = _flare[i];
            cp.colorAttachments[0].loadAction = MTLLoadActionClear;
            cp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
            cp.colorAttachments[0].storeAction = MTLStoreActionStore;
            [[cmd renderCommandEncoderWithDescriptor:cp] endEncoding];
        }
        _flareMS = nil;
    }
    if (samples > 1 && (_flareMS == nil || (int)_flareMS.sampleCount != samples)) {
        MTLTextureDescriptor *md = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:w
                                                                                     height:h
                                                                                  mipmapped:NO];
        md.textureType = MTLTextureType2DMultisample;
        md.sampleCount = samples;
        md.usage = MTLTextureUsageRenderTarget;
        md.storageMode = MTLStorageModeMemoryless;
        _flareMS = [_device newTextureWithDescriptor:md];
    }
    const int next = _flareCurrent ^ 1;
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    if (samples > 1) {
        rp.colorAttachments[0].texture = _flareMS;
        rp.colorAttachments[0].resolveTexture = _flare[next];
        rp.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
    } else {
        rp.colorAttachments[0].texture = _flare[next];
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    rp.depthAttachment.texture = rp.stencilAttachment.texture = _sceneDepthKept;
    rp.depthAttachment.loadAction = rp.stencilAttachment.loadAction = MTLLoadActionLoad;
    rp.depthAttachment.storeAction = rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    enc.label = @"X360 flares";
    int si = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
    [enc setRenderPipelineState:_flareFadePipeline[si]];
    [enc setDepthStencilState:_depthOff];
    [enc setFragmentTexture:_flare[_flareCurrent] atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    PortMetalPrelitDraw((__bridge void *)enc, (__bridge void *)_device, scene.view_proj, samples, (int)_colorFormat, (int)_depthFormat, 2);
    [enc endEncoding];
    _flareCurrent = next;
}

// One sub_6D2840 pass (see radial_fs).
- (void)radialBlur:(id<MTLTexture>)src into:(id<MTLTexture>)dst offsets:(const float *)offsets scale:(float)scale
     commandBuffer:(id<MTLCommandBuffer>)cmd {
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    struct {
        float offsets[4];
        float scale;
        float pad[3];
    } c = {{offsets[0], offsets[1], offsets[2], offsets[3]}, scale, {0, 0, 0}};
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    enc.label = @"Motion blur";
    [enc setRenderPipelineState:_radialPipeline];
    [enc setFragmentTexture:src atIndex:0];
    [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
}

// One IDI_FILTER_FX pass (sub_6D2CD0): taps of (weight, dx, dy) in source
// texels, drawn over the whole target. mode: see filter_fs.
- (void)filter:(id<MTLTexture>)src into:(id<MTLTexture>)dst taps:(const float (*)[3])taps count:(int)count
          mode:(int)mode commandBuffer:(id<MTLCommandBuffer>)cmd {
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    struct {
        float weights[4][4];
        float offsets[4][4];
        uint32_t taps;
        uint32_t lum_alpha;
        uint32_t pad[2];
    } c = {};
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < 4; j++) {
            c.weights[i][j] = taps[i][0];
        }
        c.offsets[i][0] = taps[i][1] / (float)src.width;
        c.offsets[i][1] = taps[i][2] / (float)src.height;
    }
    c.taps = count;
    c.lum_alpha = mode;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    enc.label = dst.label;
    [enc setRenderPipelineState:_filterPipeline];
    [enc setFragmentTexture:src atIndex:0];
    [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
}

// Kernels: 0x8F9560 (2x2 down), 0x8F9530 / 0x8F9500 (4-tap horizontal /
// vertical blur).
static const float kDown[4][3] = {{0.25f, -0.5f, -0.5f}, {0.25f, 0.5f, -0.5f}, {0.25f, -0.5f, 0.5f}, {0.25f, 0.5f, 0.5f}};
static const float kBlurH[4][3] = {{1 / 3.0f, 1.5f, 0}, {1 / 6.0f, 3.0f, 0}, {1 / 3.0f, -1.5f, 0}, {1 / 6.0f, -3.0f, 0}};
static const float kBlurV[4][3] = {{1 / 3.0f, 0, 1.5f}, {1 / 6.0f, 0, 3.0f}, {1 / 3.0f, 0, -1.5f}, {1 / 6.0f, 0, -3.0f}};

// PC ___eDisplayFrame 0x6DE3FA: the road reflection view (view 4) into its
// 320x240 target before the player view, and sub_6D4960's blur. Binds this
// frame's ReflectedTex (_reflBound).
- (void)drawReflection:(const Frame3D &)scene commandBuffer:(id<MTLCommandBuffer>)cmd {
    _reflBound = _white; // sub_6BCDC0 at Road Reflection Detail 0: 1x1 0xFFFFFFFF
    if (scene.reflection_mode == 0 || !_meshPipelines[0]) {
        return;
    }
    const NSUInteger w = 320, h = 240; // sub_6BCDC0, sub_6BD0D0 render target 1
    if (_reflColor == nil) {
        MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:w
                                                                                     height:h
                                                                                  mipmapped:NO];
        cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        cd.storageMode = MTLStorageModePrivate;
        _reflColor = [_device newTextureWithDescriptor:cd];
        _reflColor.label = @"Road reflection";
        _reflBlur = [_device newTextureWithDescriptor:cd];
        _reflBlur.label = @"Road reflection blur";
        MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                                                      width:w
                                                                                     height:h
                                                                                  mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModePrivate; // kept for the flares after the blur
        _reflDepth = [_device newTextureWithDescriptor:dd];
        _reflValid = false;
    }
    if (scene.reflection_active) {
        // sub_6BD3B0 for render target 1: clear target and z (black, 1).
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = _reflColor;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.depthAttachment.texture = _reflDepth;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.clearDepth = 1.0;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        rp.stencilAttachment.texture = _reflDepth;
        rp.stencilAttachment.loadAction = MTLLoadActionClear;
        rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
        enc.label = @"Road reflection";
        _drawingEnvFace = true; // a non-main render target: the NOFOG effects
        [self drawDraws:scene.reflection_draws viewProj:scene.reflection_view_proj eye:scene.eye fog:scene.fog
                 envMap:[self envMapFor:scene] width:w height:h encoder:enc state:[self passState]];
        _drawingEnvFace = false;
        [enc endEncoding];
        _reflValid = true;
    }
    if (!_reflValid) {
        _reflBound = _black;
        return;
    }
    if (scene.reflection_blur && _filterPipeline) {
        [self filter:_reflColor into:_reflBlur taps:kBlurH count:4 mode:0 commandBuffer:cmd];
        [self filter:_reflBlur into:_reflColor taps:kBlurV count:4 mode:0 commandBuffer:cmd];
    }
    if (scene.reflection_active && PortMetalPrelitHasPart(PORT_PRELIT_PART_REFLECTION)) {
        // ___eDisplayFrame 0x6DE57B: after the blur, under IDI_WORLDPRELIT_FX,
        // the world (type 2), car and pool flares into the reflection.
        MTLRenderPassDescriptor *fp = [MTLRenderPassDescriptor renderPassDescriptor];
        fp.colorAttachments[0].texture = _reflColor;
        fp.colorAttachments[0].loadAction = MTLLoadActionLoad;
        fp.colorAttachments[0].storeAction = MTLStoreActionStore;
        fp.depthAttachment.texture = fp.stencilAttachment.texture = _reflDepth;
        fp.depthAttachment.loadAction = fp.stencilAttachment.loadAction = MTLLoadActionLoad;
        fp.depthAttachment.storeAction = fp.stencilAttachment.storeAction = MTLStoreActionDontCare;
        id<MTLRenderCommandEncoder> fe = [cmd renderCommandEncoderWithDescriptor:fp];
        fe.label = @"Road reflection flares";
        [fe setViewport:(MTLViewport){0, 0, (double)w, (double)h, 0, 1}];
        PortMetalPrelitDraw((__bridge void *)fe, (__bridge void *)_device, scene.reflection_view_proj, 1, (int)_colorFormat, (int)_depthFormat,
                            PORT_PRELIT_PART_REFLECTION);
        [fe endEncoding];
    }
    _reflBound = _reflColor;
}

// The 3D scene into the offscreen frame (multisampled with FSAA, then
// resolved; PC renders into the multisampled back buffer or the FSAA
// surface 0x93E7B8 and StretchRects it, 0x6DAEB5 / sub_6D40E0), then the
// bloom source for the treatment: PC sub_6DBEC0 (2x2 taps down to a
// quarter, alpha = luminance; kernel 0x8F9560) and sub_6DBFE0 (0x8F9474 = 2
// rounds of a 4-tap horizontal then vertical blur; kernels 0x8F9530 /
// 0x8F9500), and over bright's.
// One part of the scene pass. The first clears; later ones load what the
// part before kept (the motion blur and the X360 flares split the pass); the
// last resolves.
- (id<MTLRenderCommandEncoder>)beginScenePart:(bool)first last:(bool)last samples:(int)samples commandBuffer:(id<MTLCommandBuffer>)cmd {
    const bool kept = _sceneDepthKept != nil && !(first && last);
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    // The PC back buffer is cleared to 0 (alpha 0 too) after each present
    // (eDisplayFrame 0x6E75F0, D3DCLEAR target | z | stencil, colour 0).
    rp.colorAttachments[0].loadAction = first ? MTLLoadActionClear : MTLLoadActionLoad;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    if (samples > 1) {
        rp.colorAttachments[0].texture = kept ? _sceneColorMSKept : _sceneColorMS;
        rp.colorAttachments[0].resolveTexture = _sceneColor;
        rp.colorAttachments[0].storeAction = last ? MTLStoreActionMultisampleResolve : MTLStoreActionStoreAndMultisampleResolve;
    } else {
        rp.colorAttachments[0].texture = _sceneColor;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    id<MTLTexture> depth = kept ? _sceneDepthKept : _sceneDepth;
    rp.depthAttachment.texture = rp.stencilAttachment.texture = depth;
    rp.depthAttachment.loadAction = rp.stencilAttachment.loadAction = first ? MTLLoadActionClear : MTLLoadActionLoad;
    rp.depthAttachment.clearDepth = 1.0;
    // The X360 flares test against the frame's depth after the last part.
    rp.depthAttachment.storeAction = kept ? MTLStoreActionStore : MTLStoreActionDontCare;
    rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    enc.label = first ? @"Scene" : @"Scene (continued)";
    return enc;
}

// The 3D scene into the offscreen frame (multisampled with FSAA, then
// resolved; PC renders into the multisampled back buffer or the FSAA
// surface 0x93E7B8 and StretchRects it, 0x6DAEB5 / sub_6D40E0), then the
// bloom source for the treatment: PC sub_6DBEC0 (2x2 taps down to a
// quarter, alpha = luminance; kernel 0x8F9560) and sub_6DBFE0 (0x8F9474 = 2
// rounds of a 4-tap horizontal then vertical blur; kernels 0x8F9530 /
// 0x8F9500), and over bright's.
- (void)drawOffscreenScene:(const Frame3D &)scene commandBuffer:(id<MTLCommandBuffer>)cmd view:(MTKView *)view {
    const int samples = PortMetalFSAASamples(gFSAALevel.load());
    [self ensureSceneTargets:view.drawableSize samples:samples];
    // The motion blur (PortMetal3DSetMotionBlur) and the X360 flares
    // (PortMetal3DSetX360Flares) split the pass: the frame and its depth are
    // stored after a part and loaded for the next.
    const bool split = scene.motion_blur && _radialPipeline && [self compositePipeline:0 samples:samples];
    const int si = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
    const bool x360 = scene.x360_flares && [self compositePipeline:1 samples:samples] && _flareFadePipeline[si];
    if (split || x360) {
        [self ensureMotionBlurTargets];
    }
    id<MTLRenderCommandEncoder> enc = [self beginScenePart:true last:!split && !x360 samples:samples commandBuffer:cmd];
    // The world effects write ColorWriteMode 7 (rgb; sub_6CFCE0) in the
    // player view.
    _passSamples = samples;
    _passMask = 1;
    _passMainScene = true;
    _skipSkyAlpha = split;
    [self drawScene:scene encoder:enc view:view];
    _skipSkyAlpha = false;
    // Particles, skid marks, light flares (MetalPrelit.mm).
    // The particles' technique #1 reads the player view's shadow map.
    void *particle_shadow = _shadowValid ? (__bridge void *)_shadowDepth : nullptr;
    PortMetalPrelitDraw((__bridge void *)enc, (__bridge void *)_device, scene.view_proj, samples, (int)_colorFormat, (int)_depthFormat,
                        split ? 0 : -1, particle_shadow, _shadowTransform);
    if (split) {
        [enc endEncoding];
        // sub_6DBB20: StretchRect the frame to 0x93DE60, sub_6D2840 into
        // 0x93DE64 and back with 4x the offsets (the frame is read in place
        // here: it's the same image).
        [self radialBlur:_sceneColor into:_mb[1] offsets:scene.mb.offsets scale:1.0f commandBuffer:cmd];
        [self radialBlur:_mb[1] into:_mb[0] offsets:scene.mb.offsets scale:4.0f commandBuffer:cmd];
        enc = [self beginScenePart:false last:!x360 samples:samples commandBuffer:cmd];
        // sub_6D3B80(0x982A28, 0x93DEF8, BLENDFACTOR, INVBLENDFACTOR).
        [enc setRenderPipelineState:[self compositePipeline:0 samples:samples]];
        [enc setDepthStencilState:_depthOff];
        [enc setFragmentTexture:_mb[0] atIndex:0];
        const float f = scene.mb.factor;
        [enc setBlendColorRed:f green:f blue:f alpha:f];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        // The rest of the frame: the sky's alpha-only pass (0x6DF3C3; PC
        // draws it after the flares, but it writes only alpha), the flares
        // and the particles.
        _onlySkyAlpha = true;
        [self drawScene:scene encoder:enc view:view];
        _onlySkyAlpha = false;
        PortMetalPrelitDraw((__bridge void *)enc, (__bridge void *)_device, scene.view_proj, samples, (int)_colorFormat, (int)_depthFormat, 1,
                            particle_shadow, _shadowTransform);
    }
    if (x360) {
        [enc endEncoding];
        [self drawX360Flares:scene samples:samples commandBuffer:cmd];
        enc = [self beginScenePart:false last:true samples:samples commandBuffer:cmd];
        if (!scene.overbright) {
            // sub_6DAE20 0x6DB0C4, Over Bright off: sub_6D3B80(0x982A28,
            // 0x93E7BC, ONE, ONE) adds the flare target to the frame.
            [enc setRenderPipelineState:[self compositePipeline:1 samples:samples]];
            [enc setDepthStencilState:_depthOff];
            [enc setFragmentTexture:_flare[_flareCurrent] atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
    }
    [self drawParticles:scene encoder:enc commandBuffer:cmd];
    _passSamples = 1;
    _passMask = 0;
    _passMainScene = false;
    [enc endEncoding];

    if (scene.treatment) {
        [self filter:_sceneColor into:_bloom[0] taps:kDown count:4 mode:1 commandBuffer:cmd];
        for (int i = 0; i < 2; i++) {
            [self filter:_bloom[0] into:_bloom[1] taps:kBlurH count:4 mode:0 commandBuffer:cmd];
            [self filter:_bloom[1] into:_bloom[0] taps:kBlurV count:4 mode:0 commandBuffer:cmd];
        }
    }
    if (scene.overbright && _overbrightPipeline && _overbrightAddPipeline) {
        // sub_6DBEC0 with Over Bright on: the frame down to 0x98296C with the
        // 2x2 kernel 0x8F9560 (IDI_FILTER_FX p5: rgb times the weighted
        // alpha, the sky's from its alpha-only pass). Then sub_6DC040:
        // pContrast into 0x982964 and the 0x8F9474 rounds of the 0x8F9530 /
        // 0x8F9500 blur.
        [self filter:_sceneColor into:_obSource taps:kDown count:4 mode:2 commandBuffer:cmd];
        MTLRenderPassDescriptor *op = [MTLRenderPassDescriptor renderPassDescriptor];
        op.colorAttachments[0].texture = _ob[0];
        op.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        op.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> oe = [cmd renderCommandEncoderWithDescriptor:op];
        oe.label = @"Over bright contrast";
        [oe setRenderPipelineState:_overbrightPipeline];
        [oe setFragmentTexture:_obSource atIndex:0];
        float offset = scene.ob.offset;
        [oe setFragmentBytes:&offset length:sizeof(offset) atIndex:0];
        [oe drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [oe endEncoding];
        for (int i = 0; i < 2; i++) {
            [self filter:_ob[0] into:_ob[1] taps:kBlurH count:4 mode:0 commandBuffer:cmd];
            [self filter:_ob[1] into:_ob[0] taps:kBlurV count:4 mode:0 commandBuffer:cmd];
        }
        if (x360 && _flareAddPipeline) {
            // sub_6DC040 0x6DC161, with g_X360EffectsEnable: the blurred over
            // bright goes into the flare target (BLENDFACTOR, ONE), which
            // keeps it for the next frames' fades; applyOverBright then adds
            // the flare target instead.
            MTLRenderPassDescriptor *fp = [MTLRenderPassDescriptor renderPassDescriptor];
            fp.colorAttachments[0].texture = _flare[_flareCurrent];
            fp.colorAttachments[0].loadAction = MTLLoadActionLoad;
            fp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> fe = [cmd renderCommandEncoderWithDescriptor:fp];
            fe.label = @"Over bright into the flare target";
            [fe setRenderPipelineState:_flareAddPipeline];
            [fe setFragmentTexture:_ob[0] atIndex:0];
            float intensity = scene.ob.intensity;
            [fe setFragmentBytes:&intensity length:sizeof(intensity) atIndex:0];
            [fe drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [fe endEncoding];
        }
    }
    // ___eDisplayFrame 0x6DF7DF: the drops after the bloom sources were
    // taken (sub_6DBEC0), before the treatment.
    [self drawRainDrops:scene commandBuffer:cmd];
}

// PC sub_6D53D0: the treatment from the offscreen frame into the drawable.
- (void)applyTreatment:(const Frame3D &)scene encoder:(id<MTLRenderCommandEncoder>)enc {
    const PortVisualTreatment &vt = scene.vt;
    struct {
        float black_bloom[4];
        float tint[4];
        float desaturation[4];
        float colour_bloom, brightness, vignette, pad;
    } c = {{vt.black_bloom[0], vt.black_bloom[1], 0, 0},
           {vt.tint[0], vt.tint[1], vt.tint[2], vt.tint[3]},
           {vt.desaturation[0], vt.desaturation[1], 0, 0},
           vt.colour_bloom,
           vt.brightness,
           vt.vignette,
           0};
    [enc setRenderPipelineState:_treatmentPipeline];
    [enc setDepthStencilState:_depthOff];
    [enc setFragmentTexture:_sceneColor atIndex:0];
    [enc setFragmentTexture:_bloom[0] atIndex:1];
    [enc setFragmentTexture:(scene.gain_map ? scene.gain_map : _white) atIndex:2];
    [enc setFragmentBytes:vt.spline length:sizeof(vt.spline) atIndex:1];
    [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

// Without the treatment the PC frame stays in the back buffer: here the
// offscreen frame is copied into the drawable.
- (void)copyScene:(id<MTLRenderCommandEncoder>)enc {
    [enc setRenderPipelineState:_copyPipeline];
    [enc setDepthStencilState:_depthOff];
    [enc setFragmentTexture:_sceneColor atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

// sub_6DC040's composite over the (treated) frame (0x93DE60[0x982C60]), or
// over the back buffer with the treatment off (the 0.8 blend factor case).
- (void)applyOverBright:(const Frame3D &)scene encoder:(id<MTLRenderCommandEncoder>)enc {
    if (!scene.overbright || !_overbrightPipeline || !_overbrightAddPipeline) {
        return;
    }
    [enc setRenderPipelineState:_overbrightAddPipeline];
    [enc setDepthStencilState:_depthOff];
    // With the X360 flares, the flare target (holding the over bright) at
    // full weight (sub_6D3B80(.., 0x93E7BC, ONE, ONE)).
    const bool x360 = scene.x360_flares && _flare[_flareCurrent] != nil && _flareAddPipeline;
    [enc setFragmentTexture:(x360 ? _flare[_flareCurrent] : _ob[0]) atIndex:0];
    float intensity = x360 ? 1.0f : scene.ob.intensity;
    [enc setFragmentBytes:&intensity length:sizeof(intensity) atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

- (void)drawInMTKView:(MTKView *)view {
    if (PortMacBenchmarkSkipDraw(view)) {
        return;
    }
    _drawableHeight = view.drawableSize.height;
    // NFSMW_BENCHMARK draws into an offscreen target instead of the drawable.
    MTLRenderPassDescriptor *pass = PortMacBenchmarkRenderPass(view);
    if (!pass) {
        pass = view.currentRenderPassDescriptor;
    }
    if (!pass) {
        return;
    }
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;

    id<MTLCommandBuffer> cmd = [_queue commandBuffer];
    FEFrame fe;
    {
        std::lock_guard<std::mutex> guard(gFELock);
        fe.vertices = gFEPublished.vertices;
        fe.batches = gFEPublished.batches;
        if (_sceneSerial != g3DPublishedSerial) {
            std::swap(_scene, g3DPublished); // the game clears what it gets back (PortMetal3DBeginFrame)
            _sceneSerial = g3DPublishedSerial;
        }
        PortMetalPrelitTakeFrame((__bridge void *)_device, (__bridge void *)cmd);
    }
    PortMacBenchmarkFrameTaken(view);
    const Frame3D &scene = _scene;
    gViewAspect = (float)(view.drawableSize.width / view.drawableSize.height);
    pass.depthAttachment.clearDepth = 1.0;
    pass.depthAttachment.loadAction = MTLLoadActionClear;

    _sky = scene.sky;
    for (int i = 0; i < 3; i++) {
        _skyMisc[i] = scene.sky_misc[i];
    }
    [self drawEnvFaces:scene view:view];
    [self drawShadowMap:scene commandBuffer:cmd];
    if (scene.shadow_transform_set) {
        memcpy(_shadowTransform, scene.shadow_transform, sizeof(_shadowTransform));
    }
    _invShadowStrength = scene.inv_shadow_strength;
    [self drawReflection:scene commandBuffer:cmd];
    // The 3D frame goes through the offscreen target (FSAA resolve, the
    // treatment's and over bright's sources) unless it's the debug overlay.
    bool offscreen = !scene.overlay && !scene.draws.empty() && _filterPipeline && _copyPipeline;
    bool treated = offscreen && scene.treatment && _treatmentPipeline;
    if (offscreen) {
        [self drawOffscreenScene:scene commandBuffer:cmd view:view];
    }
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
    enc.label = @"Frame";
    if (offscreen) {
        if (treated) {
            [self applyTreatment:scene encoder:enc];
        } else {
            [self copyScene:enc];
        }
        [self applyOverBright:scene encoder:enc];
    }
    [enc setDepthStencilState:_depthOff];
    if (!fe.vertices.empty()) {
        // FEng draws a 640x480 (4:3) virtual screen. Aspect modes (NFSMW_ASPECT):
        //   fill    (default) "Hor+": scale by height, centre, and let elements
        //           outside the 4:3 area (backdrops already extend past it) use
        //           the extra width; narrower windows scale by width instead.
        //   4:3     pillar/letterbox to exactly the original screen.
        //   stretch stretch 4:3 to the window (what the PC original did).
        CGSize size = view.drawableSize;
        double aspect = size.width / size.height;
        float scale[2] = {1.0f, 1.0f};
        MTLViewport viewport = {0, 0, size.width, size.height, 0, 1};
        static const char *mode = getenv("NFSMW_ASPECT");
        if (mode && strcmp(mode, "4:3") == 0) {
            double w = size.width, h = size.height;
            if (aspect > 4.0 / 3.0) {
                w = h * 4.0 / 3.0;
            } else {
                h = w * 3.0 / 4.0;
            }
            viewport = (MTLViewport){(size.width - w) / 2, (size.height - h) / 2, w, h, 0, 1};
        } else if (!(mode && strcmp(mode, "stretch") == 0)) {
            if (aspect > 4.0 / 3.0) {
                scale[0] = (float)((4.0 / 3.0) / aspect);
            } else {
                scale[1] = (float)(aspect / (4.0 / 3.0));
            }
        }
        [enc setViewport:viewport];
        [enc setVertexBytes:scale length:sizeof(scale) atIndex:1];
        {
            std::lock_guard<std::mutex> guard(gFEMappingLock);
            gFEMapping = {(float)(viewport.originX / size.width), (float)(viewport.originY / size.height),
                          (float)(viewport.width / size.width), (float)(viewport.height / size.height), scale[0], scale[1]};
        }
        id<MTLBuffer> vb = port_upload::Acquire(_device, fe.vertices.data(), fe.vertices.size() * sizeof(PortFEVertex));
        port_upload::Retire(vb, cmd);
        [enc setVertexBuffer:vb offset:0 atIndex:0];
        for (const FEBatch &b : fe.batches) {
            [enc setRenderPipelineState:_fePipelines[b.blend]];
            [enc setFragmentTexture:(b.texture ? b.texture : _white) atIndex:0];
            [enc setFragmentTexture:(b.mask ? b.mask : _white) atIndex:1];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:b.first vertexCount:b.count];
        }
    }
    if (scene.overlay) {
        [self drawScene:scene encoder:enc view:view];
    }
    [enc endEncoding];
    if (!PortMacBenchmarkEndDraw(cmd, view)) { // NFSMW_BENCHMARK presents itself
        [cmd presentDrawable:view.currentDrawable];
    }
    static const bool trace_gpu = getenv("NFSMW_TRACE_GPU") != nullptr;
    if (trace_gpu) {
        // GPU time per frame, averaged over 120 frames.
        [cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
            static double sum;
            static int n;
            sum += b.GPUEndTime - b.GPUStartTime;
            if (++n == 120) {
                fprintf(stderr, "[gpu] %.2f ms/frame\n", sum / n * 1000.0);
                sum = 0;
                n = 0;
            }
        }];
    }
    [cmd commit];
    // The faces' worker reads _scene and the prelit frame: done before the
    // next frame is taken.
    dispatch_group_wait(_envGroup, DISPATCH_TIME_FOREVER);
}

@end
