// C interface between the game's platform layer (plain C++, which includes
// game headers that clash with Objective-C) and the Metal renderer.
#pragma once

#include <cstddef>

enum PortPixelFormat {
    PORT_PIXEL_BGRA8,
    PORT_PIXEL_BC1,
    PORT_PIXEL_BC2,
    PORT_PIXEL_BC3,
    PORT_PIXEL_B5G6R5,
    PORT_PIXEL_BGR5A1,
    PORT_PIXEL_ABGR4,
};

// Creates a 2D texture; returns a retained id<MTLTexture> as an opaque handle.
void *PortMetalCreateTexture(int width, int height, int mip_levels, PortPixelFormat format, const char *label);
void PortMetalUploadMip(void *texture, int level, int width, int height, const void *data, size_t bytes_per_row);
void PortMetalReleaseTexture(void *texture);

// Debug viewer: the renderer shows the most recently created texture.
void PortMetalSetDebugTexture(void *texture);
