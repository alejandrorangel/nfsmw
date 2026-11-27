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

// Frontend (FEng) 2D rendering. The game thread records a frame of textured
// quads in clip space and publishes it; the renderer draws the latest one.
enum PortBlendMode {
    PORT_BLEND_ALPHA,      // src*a + dst*(1-a)
    PORT_BLEND_ADDITIVE,   // src*a + dst
    PORT_BLEND_SUBTRACTIVE // dst - src*a
};

struct PortFEVertex {
    float x, y;            // clip space
    float u, v;
    unsigned char rgba[4]; // ePoly colour: R, G, B at half scale (0x80 = 1.0), A full
};

void PortMetalFEBeginFrame();
// Quad corners in order 0..3 around the perimeter (triangulated 0-1-2, 0-2-3).
void PortMetalFEAddQuad(void *texture, PortBlendMode blend, const PortFEVertex quad[4]);
// Returns the number of quads published.
int PortMetalFEEndFrame();

// Maps a point in the view (0..1 from the top-left) to the frontend's 640x480
// virtual screen, using the aspect mode of the last frame drawn.
void PortMetalViewToFEScreen(float u, float v, float *x, float *y);
