#import "MetalRenderer.h"

@implementation MetalRenderer {
    id<MTLDevice> _device;
    id<MTLCommandQueue> _queue;
    uint64_t _frame;
}

- (instancetype)initWithView:(MTKView *)view {
    if ((self = [super init])) {
        _device = view.device;
        _queue = [_device newCommandQueue];
        view.colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        view.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        view.preferredFramesPerSecond = 60;
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

    id<MTLCommandBuffer> cmd = [_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
    [enc endEncoding];
    [cmd presentDrawable:view.currentDrawable];
    [cmd commit];
}

@end
