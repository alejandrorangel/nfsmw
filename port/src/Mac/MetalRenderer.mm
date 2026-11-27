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
};

struct Frame3D {
    float view_proj[16];
    float eye[3] = {0, 0, 0};
    std::vector<Draw3D> draws;
    bool overlay = false;
};
static Frame3D g3DBuilding;
static Frame3D g3DPublished;

static float gViewAspect = 4.0f / 3.0f;

float PortMetalViewAspect() {
    return gViewAspect;
}

void PortMetal3DBeginFrame(const float view_proj[16]) {
    memcpy(g3DBuilding.view_proj, view_proj, sizeof(g3DBuilding.view_proj));
    g3DBuilding.draws.clear();
    g3DBuilding.overlay = false;
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
        g3DBuilding.draws.push_back(d);
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
struct MeshOut { float4 pos [[position]]; float2 uv; float4 color; float3 normal; float3 world_pos; };

// Shading constants per draw (see Port3DDraw::material).
struct Material {
    float4 diffuse_min, diffuse_range, specular_min, specular_range;
    float envmap_min, envmap_range, envmap_power, specular_power;
};

vertex MeshOut mesh_vs(uint vid [[vertex_id]], const device float *verts [[buffer(0)]], constant MeshConst &c [[buffer(1)]]) {
    const device float *v = verts + vid * (c.stride / 4);
    float4 p = float4(v[0], v[1], v[2], 1);
    MeshOut o;
    // Row-vector matrices loaded column-major: M * p computes p * M.
    o.pos = c.mvp * p;
    o.world_pos = (c.world * p).xyz;
    o.normal = (c.world * float4(v[3], v[4], v[5], 0)).xyz;
    uint argb = as_type<uint>(v[6]);
    o.color = float4((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, argb >> 24) / 255.0;
    o.uv = float2(v[7], v[8]);
    return o;
}

// Simple stand-in for the game's environment cube map: brighter above.
float3 EnvColour(float3 r) {
    return mix(float3(0.08, 0.08, 0.09), float3(0.55, 0.57, 0.6), saturate(r.z * 0.5 + 0.5));
}

// PC CAR effect, technique 'car' shader=1 (IDI_CAR_FX vs #11 / ps #10), per
// pixel: fresnel-style diffuse/specular/envmap ranges from the light
// material, three directional lights, and a 2*(N.H)^8 highlight.
float4 CarShade(MeshOut in, float4 t, constant MeshConst &c, constant Material &m) {
    const float3 light_dir[3] = {normalize(float3(-0.3, -0.4, 0.85)), normalize(float3(0.6, 0.5, 0.3)), normalize(float3(0.0, 0.8, -0.2))};
    const float3 light_col[3] = {float3(0.85, 0.85, 0.82), float3(0.35, 0.38, 0.45), float3(0.25, 0.25, 0.28)};
    float3 n = normalize(in.normal);
    float3 v = normalize(c.eye.xyz - in.world_pos);
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
    float3 r = 2.0 * ndotv * n - v;
    float3 rgb = t.rgb * d + highlight * diffuse.a * spec + EnvColour(r) * env_strength * diffuse.a;
    return float4(rgb, t.a * diffuse.a);
}

// shade: bit 0 texture-only debug view, bit 1 alpha test (punch-through
// textures only; elsewhere alpha is a gloss/spec mask, or blends).
fragment float4 mesh_fs(MeshOut in [[stage_in]], texture2d<float> tex [[texture(0)]], constant MeshConst &c [[buffer(0)]],
                        constant Material &m [[buffer(1)]]) {
    constexpr sampler s(filter::linear, mip_filter::linear, address::repeat);
    float4 t = tex.sample(s, in.uv);
    float4 out;
    if (c.shade & 1) {
        out = t;
    } else if (c.effect == 4) {
        out = CarShade(in, t, c, m);
    } else {
        // PC WORLD / WORLDPRELIT: 2 x texture x vertex colour (rgb),
        // texture alpha x vertex alpha.
        out = float4(saturate(2.0 * t.rgb * in.color.rgb), t.a * in.color.a);
    }
    if ((c.shade & 2) && out.a < 0.5) {
        discard_fragment();
    }
    return out;
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
    }
    return self;
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
}

- (void)drawScene:(const Frame3D &)scene encoder:(id<MTLRenderCommandEncoder>)enc view:(MTKView *)view {
    if (!scene.draws.empty() && _meshPipelines[0]) {
        // Opaque first, then blended in submission order.
        CGSize size = view.drawableSize;
        [enc setViewport:(MTLViewport){0, 0, size.width, size.height, 0, 1}];
        [enc setCullMode:MTLCullModeNone];
        for (int blended = 0; blended < 2; blended++) {
            for (const Draw3D &d : scene.draws) {
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
                memcpy(c.eye, scene.eye, sizeof(scene.eye));
                static const char *shade_env = getenv("NFSMW_SHADE");
                c.shade = (shade_env && strcmp(shade_env, "texture") == 0) ? 1 : 0;
                if (d.flags & PORT_3D_ALPHA_TEST) {
                    c.shade |= 2;
                }
                // mvp = world * view_proj (row vectors).
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        float sum = 0;
                        for (int k = 0; k < 4; k++) {
                            sum += d.world[i * 4 + k] * scene.view_proj[k * 4 + j];
                        }
                        c.mvp[i * 4 + j] = sum;
                    }
                }
                memcpy(c.world, d.world, sizeof(c.world));
                [enc setRenderPipelineState:_meshPipelines[mode]];
                [enc setDepthStencilState:(mode == 0 ? _depthWrite : _depthTest)];
                [enc setVertexBuffer:d.vertex_buffer offset:0 atIndex:0];
                [enc setVertexBytes:&c length:sizeof(c) atIndex:1];
                [enc setFragmentTexture:(d.texture ? d.texture : _white) atIndex:0];
                [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
                [enc setFragmentBytes:d.material length:sizeof(d.material) atIndex:1];
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:d.num_indices
                                 indexType:MTLIndexTypeUInt16
                               indexBuffer:d.index_buffer
                         indexBufferOffset:d.first_index * 2];
            }
        }
    }
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
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
    if (!scene.overlay) {
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
    [cmd commit];
}

@end
