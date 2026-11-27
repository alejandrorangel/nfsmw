// Mac side of the benchmark mode (Platform/BenchmarkNative.cpp): the game
// thread has each frame drawn itself (the MTKView's own timer is paused).
// The main thread encodes frame N while the game thread builds frame N+1, as
// in normal play: the game thread waits only before it publishes N+1, until
// the renderer has taken N, so every frame is drawn exactly once, in order.
// NFSMW_BENCHMARK_SERIAL=1 waits for each frame's encode instead (the old
// lockstep: the frame is then build + encode). Frames go to an offscreen
// target the drawable's size: a window's
// drawables come back at the display's refresh even with the display sync
// off, which would cap the frame rate at two frames a refresh. Every
// NFSMW_BENCHMARK_PRESENT'th frame (15 by default; 1 presents every frame,
// display-bound) is copied to the window so the run can be watched. Each
// frame's command buffer reports its GPU time; screenshots are copies of the
// target written as PNG off the render thread.
#import "BenchmarkMac.h"

#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "../Platform/Benchmark.h"

#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

static MTKView *gBenchView;
static std::mutex gBenchLock;
static std::vector<double> gGPUms;    // by benchmark frame
static std::vector<double> gEncodeMs; // by benchmark frame: the main thread's draw
static bool gSerial;
// Frames handed to the main thread that the renderer hasn't taken yet (the
// game thread publishes the next one only when this is 0).
static std::mutex gTakeLock;
static std::condition_variable gTakeCond;
static int gUntaken;
static bool gTaken;  // main thread: this draw has taken its frame
static bool gInDraw; // main thread: drawing a frame the game thread handed over
static int gPendingFrame;
static int gDrawCount;
static int gPresentEvery = 15;
static std::string gPendingPNG;
static dispatch_group_t gBenchGroup;
// At most this many frames queued on the GPU: in a GPU-bound run the game
// thread waits here, as a swap chain would make it.
static dispatch_semaphore_t gInFlight;
static bool gEncoded;
static id<MTLTexture> gColor, gDepth;
static MTLRenderPassDescriptor *gPass;

void PortMacBenchmarkSetupView(void *mtk_view) {
    gBenchView = (__bridge MTKView *)mtk_view;
    gBenchView.paused = YES;
    gBenchView.enableSetNeedsDisplay = NO;
    gBenchView.framebufferOnly = NO; // the window copy blits into the drawable
    ((CAMetalLayer *)gBenchView.layer).displaySyncEnabled = PortBenchmarkVSync(false);
    if (const char *every = getenv("NFSMW_BENCHMARK_PRESENT")) {
        gPresentEvery = atoi(every);
    }
    if (const char *serial = getenv("NFSMW_BENCHMARK_SERIAL")) {
        gSerial = atoi(serial) != 0;
    }
    gBenchGroup = dispatch_group_create();
    gInFlight = dispatch_semaphore_create(2);
}

static void FrameTaken() {
    gTaken = true;
    std::lock_guard<std::mutex> guard(gTakeLock);
    gUntaken--;
    gTakeCond.notify_all();
}

void PortMacBenchmarkFrameTaken(MTKView *view) {
    if (gBenchView != nil && view == gBenchView && gInDraw && !gTaken) {
        FrameTaken();
    }
}

bool PortMacBenchmarkSkipDraw(MTKView *view) {
    return gBenchView != nil && view == gBenchView && !gInDraw;
}

void PortMacBenchmarkBeforePublish() {
    std::unique_lock<std::mutex> lock(gTakeLock);
    gTakeCond.wait(lock, [] { return gUntaken == 0; });
}

void PortMacBenchmarkDrawFrame(int frame, const char *png_path) {
    if (gBenchView == nil) {
        return;
    }
    std::string png = png_path ? png_path : "";
    dispatch_semaphore_wait(gInFlight, DISPATCH_TIME_FOREVER);
    {
        std::lock_guard<std::mutex> guard(gTakeLock);
        gUntaken++;
    }
    dispatch_group_enter(gBenchGroup);
    void (^draw)(void) = ^{
      gPendingFrame = frame;
      gPendingPNG = png;
      gEncoded = false;
      gTaken = false;
      gInDraw = true;
      uint64_t start = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
      [gBenchView draw];
      double ms = (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - start) / 1e6;
      gInDraw = false;
      if (!gTaken) {
          FrameTaken(); // nothing drawn (no size yet)
      }
      if (!gEncoded) {
          dispatch_semaphore_signal(gInFlight);
      }
      if (frame > 0) {
          std::lock_guard<std::mutex> guard(gBenchLock);
          if (gEncodeMs.size() <= static_cast<size_t>(frame)) {
              gEncodeMs.resize(frame + 1, 0.0);
          }
          gEncodeMs[frame] = ms;
      }
      dispatch_group_leave(gBenchGroup);
    };
    if (gSerial) {
        dispatch_sync(dispatch_get_main_queue(), draw);
    } else {
        dispatch_async(dispatch_get_main_queue(), draw);
    }
}

MTLRenderPassDescriptor *PortMacBenchmarkRenderPass(MTKView *view) {
    if (gBenchView == nil || view != gBenchView) {
        return nil;
    }
    NSUInteger width = (NSUInteger)view.drawableSize.width, height = (NSUInteger)view.drawableSize.height;
    if (width == 0 || height == 0) {
        return nil;
    }
    if (gColor == nil || gColor.width != width || gColor.height != height) {
        MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:view.colorPixelFormat
                                                                                      width:width
                                                                                     height:height
                                                                                  mipmapped:NO];
        cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        cd.storageMode = MTLStorageModePrivate;
        gColor = [view.device newTextureWithDescriptor:cd];
        gColor.label = @"benchmark color";
        MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:view.depthStencilPixelFormat
                                                                                      width:width
                                                                                     height:height
                                                                                  mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModePrivate;
        gDepth = [view.device newTextureWithDescriptor:dd];
        gDepth.label = @"benchmark depth";
    }
    gPass = [MTLRenderPassDescriptor renderPassDescriptor];
    gPass.colorAttachments[0].texture = gColor;
    gPass.colorAttachments[0].storeAction = MTLStoreActionStore;
    gPass.depthAttachment.texture = gDepth;
    gPass.depthAttachment.storeAction = MTLStoreActionDontCare;
    gPass.stencilAttachment.texture = gDepth;
    gPass.stencilAttachment.loadAction = MTLLoadActionClear;
    gPass.stencilAttachment.storeAction = MTLStoreActionDontCare;
    return gPass;
}

static void WritePNG(id<MTLBuffer> buffer, NSUInteger width, NSUInteger height, NSUInteger bytes_per_row, const std::string &path) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, buffer.contents, bytes_per_row * height, nullptr);
    // BGRA8Unorm: little-endian 32-bit words with the (unused) alpha first.
    CGImageRef image = CGImageCreate(width, height, 8, 32, bytes_per_row, space, kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst,
                                     provider, nullptr, false, kCGRenderingIntentDefault);
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    if (dest) {
        CGImageDestinationAddImage(dest, image, nullptr);
        if (!CGImageDestinationFinalize(dest)) {
            fprintf(stderr, "[benchmark] can't write %s\n", path.c_str());
        }
        CFRelease(dest);
    }
    CGImageRelease(image);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(space);
}

bool PortMacBenchmarkEndDraw(id<MTLCommandBuffer> cmd, MTKView *view) {
    if (gBenchView == nil || view != gBenchView) {
        return false;
    }
    int frame = gPendingFrame;
    std::string png = gPendingPNG;
    gPendingPNG.clear();
    id<MTLTexture> color = gColor;
    NSUInteger width = color.width, height = color.height, bytes_per_row = width * 4;
    id<MTLBuffer> pixels = nil;
    if (!png.empty()) {
        pixels = [cmd.device newBufferWithLength:bytes_per_row * height options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        [blit copyFromTexture:color
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(width, height, 1)
                            toBuffer:pixels
                   destinationOffset:0
              destinationBytesPerRow:bytes_per_row
            destinationBytesPerImage:bytes_per_row * height];
        [blit endEncoding];
    }
    gDrawCount++;
    if (gPresentEvery > 0 && gDrawCount % gPresentEvery == 0) {
        id<CAMetalDrawable> drawable = view.currentDrawable;
        if (drawable && drawable.texture.width == width && drawable.texture.height == height) {
            id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
            [blit copyFromTexture:color toTexture:drawable.texture];
            [blit endEncoding];
            [cmd presentDrawable:drawable];
        }
    }
    gEncoded = true;
    dispatch_group_enter(gBenchGroup);
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
      if (frame > 0) {
          std::lock_guard<std::mutex> guard(gBenchLock);
          if (gGPUms.size() <= static_cast<size_t>(frame)) {
              gGPUms.resize(frame + 1, 0.0);
          }
          gGPUms[frame] += (b.GPUEndTime - b.GPUStartTime) * 1000.0;
      }
      if (pixels) {
          // Off the GPU's completion thread, so the next frames don't wait.
          dispatch_group_async(gBenchGroup, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            WritePNG(pixels, width, height, bytes_per_row, png);
          });
      }
      dispatch_semaphore_signal(gInFlight);
      dispatch_group_leave(gBenchGroup);
    }];
    return true;
}

bool PortMacBenchmarkGPUTime(int frame, double *ms) {
    std::lock_guard<std::mutex> guard(gBenchLock);
    if (frame < 0 || static_cast<size_t>(frame) >= gGPUms.size()) {
        return false;
    }
    *ms = gGPUms[frame];
    return true;
}

bool PortMacBenchmarkEncodeTime(int frame, double *ms) {
    std::lock_guard<std::mutex> guard(gBenchLock);
    if (frame < 0 || static_cast<size_t>(frame) >= gEncodeMs.size()) {
        return false;
    }
    *ms = gEncodeMs[frame];
    return true;
}

void PortMacBenchmarkFinish() {
    if (gBenchGroup) {
        dispatch_group_wait(gBenchGroup, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));
    }
}
