// Per-frame upload buffers (the frontend's, the particles' and the prelit
// quads' vertices), reused instead of made anew each frame: a buffer goes
// back to the pool when the command buffer it is retired on completes.
#pragma once
#import <Metal/Metal.h>

#include <cstring>
#include <mutex>
#include <vector>

namespace port_upload {

inline std::mutex gLock;
inline std::vector<id<MTLBuffer>> gFree;
constexpr size_t kKeep = 32; // free buffers kept

// A shared buffer of at least length bytes holding data.
inline id<MTLBuffer> Acquire(id<MTLDevice> device, const void *data, size_t length) {
    id<MTLBuffer> buffer = nil;
    {
        std::lock_guard<std::mutex> guard(gLock);
        size_t best = gFree.size();
        for (size_t i = 0; i < gFree.size(); i++) {
            if (gFree[i].length >= length && (best == gFree.size() || gFree[i].length < gFree[best].length)) {
                best = i;
            }
        }
        if (best < gFree.size()) {
            buffer = gFree[best];
            gFree[best] = gFree.back();
            gFree.pop_back();
        }
    }
    if (buffer == nil) {
        size_t size = 4096;
        while (size < length) {
            size *= 2;
        }
        buffer = [device newBufferWithLength:size options:MTLResourceStorageModeShared];
    }
    std::memcpy(buffer.contents, data, length);
    return buffer;
}

// The buffer's last use is in cmd: back to the pool when cmd completes
// (command buffers of a queue complete in order, so a later one works too).
inline void Retire(id<MTLBuffer> buffer, id<MTLCommandBuffer> cmd) {
    if (buffer == nil) {
        return;
    }
    [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {
      std::lock_guard<std::mutex> guard(gLock);
      if (gFree.size() < kKeep) {
          gFree.push_back(buffer);
      }
    }];
}

} // namespace port_upload
