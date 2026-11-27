#import "MetalRenderer.h"

#include "MetalBridge.h"

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
    id<MTLTexture> t = (__bridge id<MTLTexture>)texture;
    FEFrame &f = gFEBuilding;
    if (f.batches.empty() || f.batches.back().texture != t || f.batches.back().blend != blend) {
        f.batches.push_back({t, blend, (uint32_t)f.vertices.size(), 0});
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
    uint32_t first_index;
    uint32_t num_indices;
    uint32_t flags;
    uint32_t effect;
    float world[16];
    float material[20];
    float lights[28];
    float envmap[12];
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
};
static Frame3D g3DBuilding;
static Frame3D g3DPublished;
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
        d.first_index = draw->first_index;
        d.num_indices = draw->num_indices;
        d.flags = draw->flags;
        d.effect = draw->effect;
        memcpy(d.world, draw->world, sizeof(d.world));
        memcpy(d.material, draw->material, sizeof(d.material));
        memcpy(d.lights, draw->lights, sizeof(d.lights));
        memcpy(d.envmap, draw->envmap, sizeof(d.envmap));
        g3DTarget->push_back(d);
    }
}

int PortMetalFEEndFrame() {
    std::lock_guard<std::mutex> guard(gFELock);
    std::swap(gFEPublished, gFEBuilding);
    std::swap(g3DPublished, g3DBuilding);
    return gFEPublished.quads;
}

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

struct FEVertex { packed_float2 pos; packed_float2 uv; uchar4 rgba; };
struct FEOut { float4 pos [[position]]; float2 uv; float4 color; };

vertex FEOut fe_vs(uint vid [[vertex_id]], const device FEVertex *verts [[buffer(0)]], constant float2 &scale [[buffer(1)]]) {
    FEVertex v = verts[vid];
    FEOut o;
    o.pos = float4(float2(v.pos) * scale, 0, 1);
    o.uv = float2(v.uv);
    // Console-style colour: RGB 0x80 = 1.0 (the PC renderer used MODULATE2X).
    o.color = float4(float3(v.rgba.xyz) * (2.0 / 255.0), v.rgba.w / 255.0);
    return o;
}

fragment float4 fe_fs(FEOut in [[stage_in]], texture2d<float> tex [[texture(0)]]) {
    constexpr sampler s(filter::linear, mip_filter::linear, address::clamp_to_edge);
    return in.color * tex.sample(s, in.uv);
}

// 3D meshes. Vertices are pulled from the raw 36-byte PC layout.
struct MeshConst { float4x4 mvp; float4x4 world; uint stride; uint shade; uint effect; uint pad; float4 eye; };
struct MeshOut { float4 pos [[position]]; float2 uv; float4 color; float3 normal; float3 world_pos; float3 local_normal; float3 local_pos;
                 float4 d0; float3 d1; float3 inscatter; };

// PortWorldFog (MetalBridge.h).
struct WorldFog {
    float4 br_plus_bm, one_over_brbm, const1, const2, const3, light_dir;
    float fog_value;
    uint enabled;
    float4 ambient, diffuse;
    uint lit;
};

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

vertex MeshOut mesh_vs(uint vid [[vertex_id]], const device float *verts [[buffer(0)]], constant MeshConst &c [[buffer(1)]],
                       constant WorldFog &fog [[buffer(2)]]) {
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
    if (FoggedEffect(c.effect)) {
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
            o.d1 = saturate(saturate(dot(fog.light_dir.xyz, n)) * fog.diffuse.rgb);
        }
    }
    return o;
}

// Fallback while no environment cube exists yet: brighter above (z up).
float3 EnvColour(float3 r) {
    return mix(float3(0.08, 0.08, 0.09), float3(0.55, 0.57, 0.6), saturate(r.z * 0.5 + 0.5));
}

// EnvMapMatrix rows (Port3DDraw::envmap).
struct EnvMatrix { float4 row[3]; };

// PC CAR effect, technique 'car' shader=1 (IDI_CAR_FX vs #11 / ps #10), per
// pixel: fresnel-style diffuse/specular/envmap ranges from the light
// material, three directional lights, and a 2*(N.H)^8 highlight.
float4 CarShade(MeshOut in, float4 t, constant MeshConst &c, constant Material &m, constant Lights &l, bool lit,
                texturecube<float> env_map, constant EnvMatrix &em) {
    float3 light_dir[3] = {normalize(float3(-0.3, -0.4, 0.85)), normalize(float3(0.6, 0.5, 0.3)), normalize(float3(0.0, 0.8, -0.2))};
    float3 light_col[3] = {float3(0.85, 0.85, 0.82), float3(0.35, 0.38, 0.45), float3(0.25, 0.25, 0.28)};
    float3 n = normalize(in.normal);
    float3 v = normalize(c.eye.xyz - in.world_pos);
    if (lit) {
        // The game's lights, in model space as on PC.
        for (int i = 0; i < 3; i++) {
            light_dir[i] = l.dir[i].xyz;
            light_col[i] = l.colour[i].rgb;
        }
        n = normalize(in.local_normal);
        v = normalize(l.eye.xyz - in.local_pos);
    }
    float ndotv = max(dot(n, v), 0.01);
    float ao = saturate(in.color.r * 2.0); // vertex colour red: occlusion mask
    float3 spec = (m.specular_min.rgb + pow(ndotv, m.specular_power) * m.specular_range.rgb) * ao;
    float env_strength = ao * (m.envmap_min + pow(ndotv, m.envmap_power) * m.envmap_range);
    float3 lights = 0;
    for (int i = 0; i < 3; i++) {
        lights += saturate(dot(n, light_dir[i]) + 0.1) * light_col[i];
    }
    float4 diffuse = m.diffuse_min + ndotv * m.diffuse_range;
    float3 d = ao * lights * diffuse.rgb;
    float3 h = normalize(v + light_dir[0]);
    float nh = saturate(dot(n, h));
    float highlight = 2.0 * pow(nh, 8.0);
    // vs #11: oT0 = (2 (N.V) N - V) * WorldView; ps #10: t0 = texCUBE(oT0),
    // env term t0 * oD1.w * oD0.w.
    float3 r = 2.0 * ndotv * n - v;
    float3 env = 0;
    if (c.shade & 16) {
        float3 re = r.x * em.row[0].xyz + r.y * em.row[1].xyz + r.z * em.row[2].xyz;
        if (c.shade & 8) {
            constexpr sampler es(filter::linear, mip_filter::linear, address::clamp_to_edge);
            env = env_map.sample(es, re).rgb;
        } else {
            env = EnvColour(float3(re.x, re.z, -re.y)); // camera space, y down
        }
    } else if (!lit) {
        env = EnvColour(r);
    }
    if (c.shade & 32) {
        return float4(env, 1); // NFSMW_SHADE=envmap: the cube lookup alone
    }
    float3 rgb = t.rgb * d + highlight * diffuse.a * spec + env * env_strength * diffuse.a;
    return float4(rgb, t.a * diffuse.a);
}

// shade: bit 0 texture-only debug view, bit 1 alpha test (punch-through
// textures only; elsewhere alpha is a gloss/spec mask, or blends), bit 2
// lit (model-space lights), bit 3 an environment cube is bound, bit 4 the
// draw has an EnvMapMatrix.
fragment float4 mesh_fs(MeshOut in [[stage_in]], texture2d<float> tex [[texture(0)]], texturecube<float> env_map [[texture(1)]],
                        constant MeshConst &c [[buffer(0)]], constant Material &m [[buffer(1)]], constant Lights &l [[buffer(2)]],
                        constant EnvMatrix &em [[buffer(3)]]) {
    constexpr sampler s(filter::linear, mip_filter::linear, address::repeat);
    float4 t = tex.sample(s, in.uv);
    float4 out;
    if (c.shade & 1) {
        out = t;
    } else if (c.effect == 4) {
        out = CarShade(in, t, c, m, l, (c.shade & 4) != 0, env_map, em);
    } else {
        // PC WORLD (ps #12): 2 x texture x (oD0 + oD1) + in-scatter (mad_x2,
        // then add t2); alpha texture x oD0 alpha. WORLDPRELIT and the
        // unlit effects: oD0 = vertex colour, oD1 = 0.
        out = float4(saturate(2.0 * t.rgb * (in.d0.rgb + in.d1) + in.inscatter), t.a * in.d0.a);
    }
    if ((c.shade & 2) && out.a < 0.5) {
        discard_fragment();
    }
    return out;
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

// IDI_FILTER_FX: weighted taps of one texture (sub_6D2CD0). lum_alpha: pass
// p4, alpha = luminance of the weighted rgb; otherwise pass p0..p3, rgba.
struct FilterConst { float4 weights[4]; float4 offsets[4]; uint taps; uint lum_alpha; };

fragment float4 filter_fs(QuadOut in [[stage_in]], texture2d<float> src [[texture(0)]], constant FilterConst &c [[buffer(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float4 sum = 0;
    for (uint i = 0; i < c.taps; i++) {
        sum += src.sample(s, in.uv + c.offsets[i].xy) * c.weights[i];
    }
    if (c.lum_alpha) {
        sum.a = dot(saturate(sum.rgb), kLuminance);
    }
    return saturate(sum);
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
    id<MTLRenderPipelineState> _meshPipelines[3]; // opaque, alpha blend, additive
    id<MTLDepthStencilState> _depthWrite;
    id<MTLDepthStencilState> _depthTest;
    id<MTLDepthStencilState> _depthOff;
    id<MTLTexture> _white;
    // Visual treatment (see PortVisualTreatment).
    id<MTLRenderPipelineState> _filterPipeline;
    id<MTLRenderPipelineState> _treatmentPipeline;
    id<MTLTexture> _sceneColor, _sceneDepth; // the frame (PC 0x93DEF8)
    id<MTLTexture> _bloom[2];                // quarter size (PC 0x982964, 0x982968)
    // Dynamic environment cube (PC 0x982A30) and its depth (0x982A4C).
    id<MTLTexture> _envCube, _envDepth;
    id<MTLTexture> _blackCube; // bound when there is none
    unsigned _envFacesDrawn; // faces drawn at least once (bit per face)
    uint64_t _frame;
}

- (instancetype)initWithView:(MTKView *)view {
    if ((self = [super init])) {
        _device = view.device;
        gDevice = _device;
        _queue = [_device newCommandQueue];
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
            MTLRenderPipelineDescriptor *md = [MTLRenderPipelineDescriptor new];
            md.vertexFunction = [lib newFunctionWithName:@"mesh_vs"];
            md.fragmentFunction = [lib newFunctionWithName:@"mesh_fs"];
            md.colorAttachments[0].pixelFormat = view.colorPixelFormat;
            md.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
            md.stencilAttachmentPixelFormat = view.depthStencilPixelFormat;
            for (int mode = 0; mode < 3; mode++) {
                MTLRenderPipelineColorAttachmentDescriptor *c = md.colorAttachments[0];
                c.blendingEnabled = mode != 0;
                c.sourceRGBBlendFactor = c.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
                c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor =
                    mode == 1 ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorOne;
                _meshPipelines[mode] = [_device newRenderPipelineStateWithDescriptor:md error:&error];
                if (!_meshPipelines[mode]) {
                    NSLog(@"NFSMW: mesh pipeline %d failed: %@", mode, error);
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
            pd.fragmentFunction = [lib newFunctionWithName:@"treatment_fs"];
            pd.colorAttachments[0].pixelFormat = view.colorPixelFormat;
            pd.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
            pd.stencilAttachmentPixelFormat = view.depthStencilPixelFormat;
            _treatmentPipeline = [_device newRenderPipelineStateWithDescriptor:pd error:&error];
            if (!_treatmentPipeline) {
                NSLog(@"NFSMW: treatment pipeline failed: %@", error);
            }
        }
        MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionLessEqual;
        dd.depthWriteEnabled = YES;
        _depthWrite = [_device newDepthStencilStateWithDescriptor:dd];
        dd.depthWriteEnabled = NO;
        _depthTest = [_device newDepthStencilStateWithDescriptor:dd];
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        _depthOff = [_device newDepthStencilStateWithDescriptor:dd];

        // Bound in place of a texture for untextured frontend quads.
        MTLTextureDescriptor *wd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:1 height:1 mipmapped:NO];
        _white = [_device newTextureWithDescriptor:wd];
        uint32_t white = 0xFFFFFFFF;
        [_white replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&white bytesPerRow:4];
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

- (void)drawScene:(const Frame3D &)scene encoder:(id<MTLRenderCommandEncoder>)enc view:(MTKView *)view {
    CGSize size = view.drawableSize;
    [self drawDraws:scene.draws viewProj:scene.view_proj eye:scene.eye fog:scene.fog envMap:[self envMapFor:scene]
              width:size.width height:size.height encoder:enc];
}

// PC ___eDisplayFrame 0x6DE64C: with the env map on, in game, each active
// env-map view is drawn into its cube face (cleared black, sub_6BD3B0)
// before the player view. 256x256 (0x8F8FF4), A8R8G8B8 (0x8F8FF8).
- (void)drawEnvFaces:(const Frame3D &)scene commandBuffer:(id<MTLCommandBuffer>)cmd {
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
    for (int face = 0; face < 6; face++) {
        const EnvFace3D &f = scene.env_faces[face];
        if (!f.active) {
            continue;
        }
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = _envCube;
        rp.colorAttachments[0].slice = face;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.depthAttachment.texture = _envDepth;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.clearDepth = 1.0;
        rp.depthAttachment.storeAction = MTLStoreActionDontCare;
        rp.stencilAttachment.texture = _envDepth;
        rp.stencilAttachment.loadAction = MTLLoadActionClear;
        rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
        enc.label = @"EnvMap face";
        [self drawDraws:f.draws viewProj:f.view_proj eye:f.eye fog:scene.fog envMap:nil width:size height:size encoder:enc];
        [enc endEncoding];
        _envFacesDrawn |= 1u << face;
    }
}

- (void)drawDraws:(const std::vector<Draw3D> &)draws viewProj:(const float *)view_proj eye:(const float *)eye fog:(const PortWorldFog &)fog
           envMap:(id<MTLTexture>)env_map width:(double)width height:(double)height encoder:(id<MTLRenderCommandEncoder>)enc {
    if (!draws.empty() && _meshPipelines[0]) {
        // Opaque first, then blended in submission order.
        [enc setViewport:(MTLViewport){0, 0, width, height, 0, 1}];
        [enc setCullMode:MTLCullModeNone];
        for (int blended = 0; blended < 2; blended++) {
            for (const Draw3D &d : draws) {
                int mode = (d.flags & PORT_3D_ADDITIVE) ? 2 : (d.flags & PORT_3D_ALPHA_BLEND) ? 1 : 0;
                if ((mode != 0) != (blended != 0)) {
                    continue;
                }
                struct {
                    float mvp[16];
                    float world[16];
                    uint32_t stride;
                    uint32_t shade;
                    uint32_t effect;
                    uint32_t pad;
                    float eye[4];
                } c = {};
                c.stride = d.vertex_stride;
                c.effect = d.effect;
                memcpy(c.eye, eye, sizeof(float) * 3);
                static const char *shade_env = getenv("NFSMW_SHADE");
                c.shade = (shade_env && strcmp(shade_env, "texture") == 0) ? 1 : 0;
                if (shade_env && strcmp(shade_env, "envmap") == 0) {
                    c.shade |= 32;
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
                // mvp = world * view_proj (row vectors).
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        float sum = 0;
                        for (int k = 0; k < 4; k++) {
                            sum += d.world[i * 4 + k] * view_proj[k * 4 + j];
                        }
                        c.mvp[i * 4 + j] = sum;
                    }
                }
                memcpy(c.world, d.world, sizeof(c.world));
                [enc setRenderPipelineState:_meshPipelines[mode]];
                [enc setDepthStencilState:(mode == 0 ? _depthWrite : _depthTest)];
                [enc setVertexBuffer:d.vertex_buffer offset:0 atIndex:0];
                [enc setVertexBytes:&c length:sizeof(c) atIndex:1];
                [enc setVertexBytes:&fog length:sizeof(fog) atIndex:2];
                [enc setFragmentTexture:(d.texture ? d.texture : _white) atIndex:0];
                [enc setFragmentTexture:(env_map ? env_map : _blackCube) atIndex:1];
                [enc setFragmentBytes:d.envmap length:sizeof(d.envmap) atIndex:3];
                [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
                [enc setFragmentBytes:d.material length:sizeof(d.material) atIndex:1];
                [enc setFragmentBytes:d.lights length:sizeof(d.lights) atIndex:2];
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:d.num_indices
                                 indexType:MTLIndexTypeUInt16
                               indexBuffer:d.index_buffer
                         indexBufferOffset:d.first_index * 2];
            }
        }
    }
}

// Offscreen targets for the visual treatment, sized like the drawable.
- (void)ensureTreatmentTargets:(CGSize)size {
    NSUInteger w = (NSUInteger)size.width, h = (NSUInteger)size.height;
    if (_sceneColor && _sceneColor.width == w && _sceneColor.height == h) {
        return;
    }
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
    _sceneDepth = [_device newTextureWithDescriptor:dd];
    // PC 0x6C311D: the bloom targets are a quarter of the back buffer.
    cd.width = w / 4 > 0 ? w / 4 : 1;
    cd.height = h / 4 > 0 ? h / 4 : 1;
    for (int i = 0; i < 2; i++) {
        _bloom[i] = [_device newTextureWithDescriptor:cd];
    }
}

// One IDI_FILTER_FX pass (sub_6D2CD0): taps of (weight, dx, dy) in source
// texels, drawn over the whole target.
- (void)filter:(id<MTLTexture>)src into:(id<MTLTexture>)dst taps:(const float (*)[3])taps count:(int)count
     lumAlpha:(bool)lumAlpha commandBuffer:(id<MTLCommandBuffer>)cmd {
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
    c.lum_alpha = lumAlpha ? 1 : 0;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:_filterPipeline];
    [enc setFragmentTexture:src atIndex:0];
    [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
}

// The 3D scene into an offscreen frame, then the bloom source: PC
// sub_6DBEC0 (2x2 taps down to a quarter, alpha = luminance; kernel 0x8F9560)
// and sub_6DBFE0 (0x8F9474 = 2 rounds of a 4-tap horizontal then vertical
// blur; kernels 0x8F9530 / 0x8F9500).
- (void)drawTreatedScene:(const Frame3D &)scene commandBuffer:(id<MTLCommandBuffer>)cmd view:(MTKView *)view {
    [self ensureTreatmentTargets:view.drawableSize];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = _sceneColor;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.depthAttachment.texture = _sceneDepth;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.clearDepth = 1.0;
    rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    rp.stencilAttachment.texture = _sceneDepth;
    rp.stencilAttachment.loadAction = MTLLoadActionClear;
    rp.stencilAttachment.storeAction = MTLStoreActionDontCare;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    [self drawScene:scene encoder:enc view:view];
    [enc endEncoding];

    static const float down[4][3] = {{0.25f, -0.5f, -0.5f}, {0.25f, 0.5f, -0.5f}, {0.25f, -0.5f, 0.5f}, {0.25f, 0.5f, 0.5f}};
    static const float blur_h[4][3] = {{1 / 3.0f, 1.5f, 0}, {1 / 6.0f, 3.0f, 0}, {1 / 3.0f, -1.5f, 0}, {1 / 6.0f, -3.0f, 0}};
    static const float blur_v[4][3] = {{1 / 3.0f, 0, 1.5f}, {1 / 6.0f, 0, 3.0f}, {1 / 3.0f, 0, -1.5f}, {1 / 6.0f, 0, -3.0f}};
    [self filter:_sceneColor into:_bloom[0] taps:down count:4 lumAlpha:true commandBuffer:cmd];
    for (int i = 0; i < 2; i++) {
        [self filter:_bloom[0] into:_bloom[1] taps:blur_h count:4 lumAlpha:false commandBuffer:cmd];
        [self filter:_bloom[1] into:_bloom[0] taps:blur_v count:4 lumAlpha:false commandBuffer:cmd];
    }
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

- (void)drawInMTKView:(MTKView *)view {
    MTLRenderPassDescriptor *pass = view.currentRenderPassDescriptor;
    if (!pass) {
        return;
    }
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;

    FEFrame fe;
    Frame3D scene;
    {
        std::lock_guard<std::mutex> guard(gFELock);
        fe.vertices = gFEPublished.vertices;
        fe.batches = gFEPublished.batches;
        scene = g3DPublished;
    }
    gViewAspect = (float)(view.drawableSize.width / view.drawableSize.height);
    pass.depthAttachment.clearDepth = 1.0;
    pass.depthAttachment.loadAction = MTLLoadActionClear;

    id<MTLCommandBuffer> cmd = [_queue commandBuffer];
    [self drawEnvFaces:scene commandBuffer:cmd];
    bool treated = !scene.overlay && scene.treatment && !scene.draws.empty() && _filterPipeline && _treatmentPipeline;
    if (treated) {
        [self drawTreatedScene:scene commandBuffer:cmd view:view];
    }
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
    if (treated) {
        [self applyTreatment:scene encoder:enc];
    } else if (!scene.overlay) {
        [self drawScene:scene encoder:enc view:view];
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
        id<MTLBuffer> vb = [_device newBufferWithBytes:fe.vertices.data()
                                                length:fe.vertices.size() * sizeof(PortFEVertex)
                                               options:MTLResourceStorageModeShared];
        [enc setVertexBuffer:vb offset:0 atIndex:0];
        for (const FEBatch &b : fe.batches) {
            [enc setRenderPipelineState:_fePipelines[b.blend]];
            [enc setFragmentTexture:(b.texture ? b.texture : _white) atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:b.first vertexCount:b.count];
        }
    }
    if (scene.overlay) {
        [self drawScene:scene encoder:enc view:view];
    }
    [enc endEncoding];
    [cmd presentDrawable:view.currentDrawable];
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
}

@end
