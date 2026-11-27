// Prelit quads (particles, skid marks, light flares): see MetalPrelitBridge.h.

#import <Metal/Metal.h>

#include "MetalPrelitBridge.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

struct Batch {
    id<MTLTexture> texture; // strong: a pack unloaded mid-frame stays alive until drawn
    PortPrelitBlend blend;
    uint32_t flags;
    uint32_t first; // vertex (triangle list)
    uint32_t count;
    int part; // PortMetalPrelitSetPart
};

struct Frame {
    std::vector<PortPrelitVertex> vertices; // two triangles per quad
    std::vector<Batch> batches;
    int part = 0; // PortMetalPrelitSetPart
};

Frame gBuilding;
Frame gPublished;
std::mutex gLock;

// IDI_PARTICLES_FX technique #0 (vs #5, ps #4 'mul_x2 r0, t0, v0') and
// IDI_WORLDPRELIT_FX technique #0 (vs #1, ps #0, the same): position times
// WorldViewProj, colour and uv passed through; 2 x texture x colour
// (alpha too), saturated. FOGENABLE 0.
//
// PORT_PRELIT_SHADOWED batches: IDI_PARTICLES_FX technique #1 (shadowLevel
// 1, the NVIDIA path's only particle level; vs #3, ps_2_0 #2): oT1 =
// position x ShadowTransform, then 'texldp r1, t1, s1' (the hardware depth
// compare, as ShadowCompare in MetalRenderer.mm), 'add_sat r1.w, r1.x, 0.5'
// and the rgb of 2 x texture x colour times it (one saturate at the
// output). The D24S8 map clamps the reference depth to 0..1; the
// Depth32Float one doesn't, hence the saturate.
NSString *const kShader = @R"(
#include <metal_stdlib>
using namespace metal;

struct PrelitVertex { packed_float3 pos; uint colour; packed_float2 uv; };
struct PrelitOut { float4 pos [[position]]; float4 colour; float2 uv; float4 shadow_pos; };
struct PrelitConst { float4x4 view_proj; float4x4 shadow; uint alpha_test; uint shadowed; uint pad[2]; };

vertex PrelitOut prelit_vs(uint vid [[vertex_id]], const device PrelitVertex *v [[buffer(0)]], constant PrelitConst &c [[buffer(1)]]) {
    PrelitVertex in = v[vid];
    PrelitOut o;
    o.pos = c.view_proj * float4(float3(in.pos), 1);
    uint d = in.colour; // D3DCOLOR
    o.colour = float4(float((d >> 16) & 0xFF), float((d >> 8) & 0xFF), float(d & 0xFF), float(d >> 24)) / 255.0;
    o.uv = float2(in.uv);
    o.shadow_pos = c.shadowed ? c.shadow * float4(float3(in.pos), 1) : float4(0, 0, 0, 1);
    return o;
}

fragment float4 prelit_fs(PrelitOut in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]],
                          depth2d<float> sm [[texture(1)]], constant PrelitConst &c [[buffer(0)]]) {
    float4 out = 2.0 * t.sample(s, in.uv) * in.colour;
    if (c.shadowed) {
        constexpr sampler ss(compare_func::less_equal, filter::linear, address::clamp_to_edge);
        float4 p = in.shadow_pos;
        float lit = sm.sample_compare(ss, p.xy / p.w, saturate(p.z / p.w));
        out.rgb *= saturate(lit + 0.5);
    }
    out = saturate(out);
    if (c.alpha_test && out.a < (95.0 / 255.0)) { // ALPHAREF 0x5F (sub_6C68B0)
        discard_fragment();
    }
    return out;
}
)";

struct Pipelines {
    id<MTLDevice> device;
    id<MTLLibrary> lib;
    id<MTLRenderPipelineState> pso[4][PORT_PRELIT_NUM_BLENDS]; // by sample count index
    MTLPixelFormat colour = MTLPixelFormatInvalid, depth = MTLPixelFormatInvalid;
    id<MTLDepthStencilState> depth_write, depth_test;
    id<MTLSamplerState> sampler;
    id<MTLTexture> white;
    id<MTLTexture> lit_depth; // 1x1 depth 1: the unshadowed batches' (unread) shadow map
    bool failed = false;
};
Pipelines gP;

bool Setup(id<MTLDevice> device, MTLPixelFormat colour, MTLPixelFormat depth) {
    if (gP.failed) {
        return false;
    }
    if (gP.lib != nil && gP.colour == colour && gP.depth == depth) {
        return true;
    }
    gP.device = device;
    if (gP.lib == nil) {
        NSError *error = nil;
        gP.lib = [device newLibraryWithSource:kShader options:nil error:&error];
        if (gP.lib == nil) {
            NSLog(@"NFSMW: prelit shader failed: %@", error);
            gP.failed = true;
            return false;
        }
        MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionLessEqual; // ZFUNC 4
        dd.depthWriteEnabled = YES;
        gP.depth_write = [device newDepthStencilStateWithDescriptor:dd];
        dd.depthWriteEnabled = NO;
        gP.depth_test = [device newDepthStencilStateWithDescriptor:dd];
        // diffuse_sampler: WRAP, the PC texture filter (linear here).
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
        sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.mipFilter = MTLSamplerMipFilterLinear;
        sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeRepeat;
        gP.sampler = [device newSamplerStateWithDescriptor:sd];
        MTLTextureDescriptor *wd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:1 height:1 mipmapped:NO];
        gP.white = [device newTextureWithDescriptor:wd];
        uint32_t white = 0xFFFFFFFF;
        [gP.white replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&white bytesPerRow:4];
        MTLTextureDescriptor *ld = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:1 height:1 mipmapped:NO];
        ld.usage = MTLTextureUsageShaderRead;
        ld.storageMode = MTLStorageModeShared;
        gP.lit_depth = [device newTextureWithDescriptor:ld];
        float one = 1.0f;
        [gP.lit_depth replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&one bytesPerRow:4];
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < PORT_PRELIT_NUM_BLENDS; j++) {
            gP.pso[i][j] = nil;
        }
    }
    gP.colour = colour;
    gP.depth = depth;
    return true;
}

id<MTLRenderPipelineState> Pipeline(int samples, PortPrelitBlend blend) {
    int si = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
    __strong id<MTLRenderPipelineState> &p = gP.pso[si][blend];
    if (p != nil) {
        return p;
    }
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [gP.lib newFunctionWithName:@"prelit_vs"];
    d.fragmentFunction = [gP.lib newFunctionWithName:@"prelit_fs"];
    d.colorAttachments[0].pixelFormat = gP.colour;
    d.depthAttachmentPixelFormat = gP.depth;
    if (gP.depth == MTLPixelFormatDepth32Float_Stencil8) {
        d.stencilAttachmentPixelFormat = gP.depth;
    }
    d.rasterSampleCount = (NSUInteger)(1 << si);
    MTLRenderPipelineColorAttachmentDescriptor *c = d.colorAttachments[0];
    c.writeMask = blend == PORT_PRELIT_ALPHA_ONLY ? MTLColorWriteMaskAlpha // IDI_WORLDMIN_FX COLORWRITEENABLE 8
                                                  : MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue; // COLORWRITEENABLE 7
    MTLBlendFactor src = MTLBlendFactorSourceAlpha, dst = MTLBlendFactorOneMinusSourceAlpha;
    switch (blend) {
    case PORT_PRELIT_OPAQUE:
        break;
    case PORT_PRELIT_ALPHA:
        break;
    case PORT_PRELIT_ADDITIVE:
        dst = MTLBlendFactorOne;
        break;
    case PORT_PRELIT_SUBTRACTIVE:
        src = MTLBlendFactorZero;
        dst = MTLBlendFactorOneMinusSourceColor;
        break;
    case PORT_PRELIT_DEST_ALPHA:
        src = MTLBlendFactorDestinationAlpha;
        dst = MTLBlendFactorOneMinusDestinationAlpha;
        break;
    case PORT_PRELIT_DEST_ADDITIVE:
        src = MTLBlendFactorDestinationAlpha;
        dst = MTLBlendFactorOne;
        break;
    default:
        break;
    }
    c.blendingEnabled = blend != PORT_PRELIT_OPAQUE && blend != PORT_PRELIT_ALPHA_ONLY;
    c.sourceRGBBlendFactor = c.sourceAlphaBlendFactor = src;
    c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor = dst;
    NSError *error = nil;
    p = [gP.device newRenderPipelineStateWithDescriptor:d error:&error];
    if (p == nil) {
        NSLog(@"NFSMW: prelit pipeline %d/%d failed: %@", si, (int)blend, error);
    }
    return p;
}

} // namespace

void PortMetalPrelitBeginFrame() {
    gBuilding.vertices.clear();
    gBuilding.batches.clear();
    gBuilding.part = 0;
}

void PortMetalPrelitSetPart(int part) {
    gBuilding.part = part;
}

void PortMetalPrelitAddQuads(void *texture, PortPrelitBlend blend, uint32_t flags, const PortPrelitVertex *vertices, uint32_t num_quads) {
    if (num_quads == 0 || blend < 0 || blend >= PORT_PRELIT_NUM_BLENDS) {
        return;
    }
    id<MTLTexture> t = (__bridge id<MTLTexture>)texture;
    Frame &f = gBuilding;
    if (f.batches.empty() || f.batches.back().part != f.part || f.batches.back().texture != t || f.batches.back().blend != blend ||
        f.batches.back().flags != flags) {
        f.batches.push_back({t, blend, flags, (uint32_t)f.vertices.size(), 0, f.part});
    }
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (uint32_t q = 0; q < num_quads; q++) {
        for (int i : order) {
            f.vertices.push_back(vertices[q * 4 + i]);
        }
    }
    f.batches.back().count += num_quads * 6;
}

bool PortMetalPrelitHasPart(int part) {
    std::lock_guard<std::mutex> guard(gLock);
    for (const Batch &b : gPublished.batches) {
        if (b.part == part) {
            return true;
        }
    }
    return false;
}

void PortMetalPrelitPublish() {
    std::lock_guard<std::mutex> guard(gLock);
    std::swap(gPublished, gBuilding);
}

void PortMetalPrelitDraw(void *encoder, void *device, const float view_proj[16], int samples, int colour_format, int depth_format,
                         int part, void *shadow_map, const float shadow_transform[16]) {
    std::vector<PortPrelitVertex> vertices;
    std::vector<Batch> batches;
    {
        std::lock_guard<std::mutex> guard(gLock);
        for (const Batch &b : gPublished.batches) {
            if (part < 0 ? b.part <= 1 : b.part == part) {
                batches.push_back(b);
            }
        }
        if (batches.empty()) {
            return;
        }
        vertices = gPublished.vertices;
    }
    id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)encoder;
    id<MTLDevice> dev = (__bridge id<MTLDevice>)device;
    if (!Setup(dev, (MTLPixelFormat)colour_format, (MTLPixelFormat)depth_format)) {
        return;
    }
    id<MTLBuffer> vb = [dev newBufferWithBytes:vertices.data() length:vertices.size() * sizeof(PortPrelitVertex) options:MTLResourceStorageModeShared];
    struct {
        float view_proj[16];
        float shadow[16];
        uint32_t alpha_test;
        uint32_t shadowed;
        uint32_t pad[2];
    } c = {};
    std::memcpy(c.view_proj, view_proj, sizeof(c.view_proj));
    id<MTLTexture> sm = (__bridge id<MTLTexture>)shadow_map;
    if (sm != nil && shadow_transform != nullptr) {
        std::memcpy(c.shadow, shadow_transform, sizeof(c.shadow)); // world identity (PlatStartParticleRender)
    } else {
        sm = nil;
    }
    [enc setCullMode:MTLCullModeNone];
    [enc setVertexBuffer:vb offset:0 atIndex:0];
    [enc setFragmentSamplerState:gP.sampler atIndex:0];
    for (const Batch &b : batches) {
        id<MTLRenderPipelineState> pso = Pipeline(samples, b.blend);
        if (pso == nil) {
            continue;
        }
        c.alpha_test = (b.flags & PORT_PRELIT_ALPHA_TEST) ? 1 : 0;
        c.shadowed = (sm != nil && (b.flags & PORT_PRELIT_SHADOWED)) ? 1 : 0;
        [enc setRenderPipelineState:pso];
        [enc setDepthStencilState:(b.flags & PORT_PRELIT_DEPTH_WRITE) ? gP.depth_write : gP.depth_test];
        [enc setVertexBytes:&c length:sizeof(c) atIndex:1];
        [enc setFragmentBytes:&c length:sizeof(c) atIndex:0];
        [enc setFragmentTexture:(b.texture ? b.texture : gP.white) atIndex:0];
        [enc setFragmentTexture:(c.shadowed ? sm : gP.lit_depth) atIndex:1];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:b.first vertexCount:b.count];
    }
}
