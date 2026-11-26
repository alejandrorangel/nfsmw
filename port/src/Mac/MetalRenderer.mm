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

int PortMetalFEEndFrame() {
    std::lock_guard<std::mutex> guard(gFELock);
    std::swap(gFEPublished, gFEBuilding);
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

- (void)drawInMTKView:(MTKView *)view {
    MTLRenderPassDescriptor *pass = view.currentRenderPassDescriptor;
    if (!pass) {
        return;
    }
    // Slow colour cycle so it's obvious the frame loop is alive.
    double t = (double)(_frame++) / 60.0;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.05, 0.05 + 0.05 * (1.0 + sin(t)), 0.12, 1.0);
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;

    id<MTLTexture> texture;
    {
        std::lock_guard<std::mutex> guard(gDebugLock);
        texture = gDebugTexture;
    }

    FEFrame fe;
    {
        std::lock_guard<std::mutex> guard(gFELock);
        fe.vertices = gFEPublished.vertices;
        fe.batches = gFEPublished.batches;
    }

    id<MTLCommandBuffer> cmd = [_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
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
        id<MTLBuffer> vb = [_device newBufferWithBytes:fe.vertices.data()
                                                length:fe.vertices.size() * sizeof(PortFEVertex)
                                               options:MTLResourceStorageModeShared];
        [enc setVertexBuffer:vb offset:0 atIndex:0];
        for (const FEBatch &b : fe.batches) {
            [enc setRenderPipelineState:_fePipelines[b.blend]];
            [enc setFragmentTexture:(b.texture ? b.texture : _white) atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:b.first vertexCount:b.count];
        }
    } else if (texture && _debugPipeline) {
        // Fit the texture inside 80% of the view, keeping its aspect ratio.
        CGSize size = view.drawableSize;
        float view_aspect = size.width / size.height;
        float tex_aspect = (float)texture.width / (float)texture.height;
        float scale[2] = {0.8f, 0.8f};
        if (tex_aspect > view_aspect) {
            scale[1] = 0.8f * view_aspect / tex_aspect;
        } else {
            scale[0] = 0.8f * tex_aspect / view_aspect;
        }
        [enc setRenderPipelineState:_debugPipeline];
        [enc setVertexBytes:scale length:sizeof(scale) atIndex:0];
        [enc setFragmentTexture:texture atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }
    [enc endEncoding];
    [cmd presentDrawable:view.currentDrawable];
    [cmd commit];
}

@end
