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
// After the renderer has taken the published frame (under the frame lock).
void PortMacBenchmarkFrameTaken(MTKView *view);
// A draw the benchmark didn't ask for (AppKit's, e.g. at a resize): left out,
// so it can't take a frame before the draw meant for it.
bool PortMacBenchmarkSkipDraw(MTKView *view);
// A command buffer of the frame being drawn besides the main one (the
// env-map faces'): its GPU time counts for the frame.
void PortMacBenchmarkExtraCommandBuffer(id<MTLCommandBuffer> cmd, MTKView *view);
