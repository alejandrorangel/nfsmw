// Immediate-mode "prelit" geometry for the 3D frame: world-space quads with a
// D3DCOLOR per vertex and one texture, drawn after the frame's meshes in
// submission order. The PC draws these through IDI_PARTICLES_FX (effect 18,
// EmitterSystem::Render -> PlatStartParticleRender / PlatAddParticle /
// PlatEndParticleRender) and IDI_WORLDPRELIT_FX (effect 17, ePoly quads:
// skid marks, light flares). Both techniques #0 (vs/ps #5/#4 and #1/#0) are
// out = saturate(2 * texture * colour), rgb written (COLORWRITEENABLE 7),
// ZFUNC LESSEQUAL, no culling, blend state from the texture (sub_6C68B0).
#pragma once

#include <cstdint>

struct PortPrelitVertex {
    float x, y, z;   // world space
    uint32_t colour; // D3DCOLOR (0xAARRGGBB)
    float u, v;
};

// sub_6C68B0's blend states by TextureInfo::AlphaBlendType.
enum PortPrelitBlend {
    PORT_PRELIT_OPAQUE,         // SRCCOPY: no blending, depth writes
    PORT_PRELIT_ALPHA,          // SrcAlpha / InvSrcAlpha
    PORT_PRELIT_ADDITIVE,       // SrcAlpha / One
    PORT_PRELIT_SUBTRACTIVE,    // Zero / InvSrcColor
    PORT_PRELIT_DEST_ALPHA,     // DestAlpha / InvDestAlpha
    PORT_PRELIT_DEST_ADDITIVE,  // DestAlpha / One
    // IDI_WORLDMIN_FX (effect 7, fixed function): no blending, alpha only
    // (COLORWRITEENABLE 8), z test without writes; the tunnel glare quad.
    PORT_PRELIT_ALPHA_ONLY,
    PORT_PRELIT_NUM_BLENDS,
};
enum { PORT_PRELIT_ALPHA_TEST = 1, PORT_PRELIT_DEPTH_WRITE = 2 };

// Clears the building frame (PortMetal3DBeginFrame calls it).
void PortMetalPrelitBeginFrame();
// Adds num_quads quads (4 vertices each, corners in order around the
// perimeter) with texture (id<MTLTexture>, null = white).
void PortMetalPrelitAddQuads(void *texture, PortPrelitBlend blend, uint32_t flags, const PortPrelitVertex *vertices, uint32_t num_quads);
// Quads added from now on are in this part of the frame: 0 (the start) up
// to the motion blur, 1 after it (PortMetal3DSetMotionBlur), 2 the X360
// flares (PortMetal3DBeginX360Flares).
void PortMetalPrelitSetPart(int part);
// Publishes the building frame (PortMetalFEEndFrame calls it).
void PortMetalPrelitPublish();
// Render thread: draws the published frame into encoder (an
// id<MTLRenderCommandEncoder> of the main scene pass, samples per pixel)
// with view_proj (world -> clip, row vectors). part: 0, 1 or 2 (see
// PortMetalPrelitSetPart), -1 parts 0 and 1.
void PortMetalPrelitDraw(void *encoder, void *device, const float view_proj[16], int samples, int colour_format, int depth_format,
                         int part = -1);
