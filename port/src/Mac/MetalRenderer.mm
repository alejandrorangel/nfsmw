#import "MetalRenderer.h"

#include "MetalBridge.h"

#include <mutex>

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
// Renderer. For now it shows the most recently loaded game texture, centred
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
        }
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

    id<MTLCommandBuffer> cmd = [_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
    if (texture && _debugPipeline) {
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
