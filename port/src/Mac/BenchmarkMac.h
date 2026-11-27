// Mac side of the benchmark mode (Platform/BenchmarkNative.cpp).
#pragma once
#import <MetalKit/MetalKit.h>

// MetalRenderer's drawInMTKView. In the benchmark the frame is drawn into an
// offscreen target the drawable's size (so frames aren't held to the
// display's refresh by waiting for drawables): this is its pass, nil when the
// benchmark is off.
MTLRenderPassDescriptor *PortMacBenchmarkRenderPass(MTKView *view);
// After the frame is encoded: its GPU time, its screenshot when asked for,
// and now and then a copy to the window. Returns false when the benchmark is
// off (the caller presents the drawable).
bool PortMacBenchmarkEndDraw(id<MTLCommandBuffer> cmd, MTKView *view);
