// C interface between the game's platform layer (plain C++, which includes
// game headers that clash with Objective-C) and the Metal renderer.
#pragma once

#include <cstddef>
#include <cstdint>

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

// 3D frames: meshes drawn under the frontend, published with it by
// PortMetalFEEndFrame. Matrices are the game's row-vector bMatrix4 layout
// (p' = p * M); view_proj maps world space to Metal clip space.
struct Port3DDraw {
    void *vertex_buffer; // vertices starting position, normal, D3DCOLOR, uv
    uint32_t vertex_stride; // bytes (multiple of 4, >= 36)
    void *index_buffer;  // 16-bit triangle list
    void *texture;       // id<MTLTexture>, or null for white
    uint32_t first_index;
    uint32_t num_indices;
    uint32_t flags;  // PORT_3D_*
    uint32_t effect; // PC effect index (eSolidPlatNative.hpp PORT_EFFECT_*)
    float world[16];
    // Light material (car effect constants): DiffuseMin, DiffuseRange,
    // SpecularMin, SpecularRange (rgba each), then EnvmapMin, EnvmapRange,
    // EnvmapPower, SpecularPower.
    float material[20];
    // Model-space lighting (eDynamicLightContext), when PORT_3D_LIT: three
    // light directions, three colours, then the eye position (xyzw each).
    float lights[28];
};
enum { PORT_3D_ALPHA_BLEND = 1, PORT_3D_ADDITIVE = 2, PORT_3D_ALPHA_TEST = 4, PORT_3D_LIT = 8 };

void PortMetal3DBeginFrame(const float view_proj[16]);
void PortMetal3DSetEye(const float eye[3]); // world-space camera position
float PortMetalViewAspect(); // drawable width / height
void PortMetal3DSetOverlay(bool on_top); // debug: draw this frame's 3D over the frontend
void PortMetal3DAdd(const Port3DDraw *draw);

// PC visual treatment (IDI_VISUALTREATMENT_FX technique 0, applied by
// speed.exe 1.3 sub_6D53D0 to the 3D scene before the frontend is drawn).
// Effect constants as set by sub_6D4EF0, plus the 256x1 SPLINE lookup
// (A8R8G8B8: r black bloom, g colour bloom, b detail map curve; sub_6C3B20)
// and the GAINMAP texture (UVESVIGNETTE).
struct PortVisualTreatment {
    float black_bloom[2]; // BlackBloomIntensity.xy
    float colour_bloom;   // ColourBloomIntensity
    float brightness;     // VisualEffectBrightness
    float tint[4];        // ColourBloomTint
    float desaturation[2]; // Desaturation.xy
    float vignette;       // VisualEffectVignette
    float pad;
    uint32_t spline[256];
    void *gain_map; // id<MTLTexture>, or null (white)
};
// Applies the treatment to this 3D frame (call after PortMetal3DBeginFrame).
void PortMetal3DSetTreatment(const PortVisualTreatment *treatment);

// PC world atmospheric scattering (IDI_WORLD_FX family vertex shaders:
// extinction and Rayleigh + Henyey-Greenstein Mie in-scattering), with the
// constants speed.exe 1.3 sub_6D6CF0 sets each frame from the 'effects'
// attributes, and the sun direction (TimeOfDay +0x180, sub_769360) that
// becomes LocalLightVec. Vectors are xyz plus padding. Also the same
// effects' TimeOfDay lighting (AmbientColour / DiffuseColour, set by the
// effect-begin code 0x6C6450 / 0x6C6300).
struct PortWorldFog {
    float br_plus_bm[4];    // Fog_Br_Plus_Bm
    float one_over_brbm[4]; // Fog_One_Over_BrBm
    float const1[4];        // Fog_Const_1: Br * 3/(16 pi)
    float const2[4];        // Fog_Const_2: Bm * 1/(4 pi)
    float const3[4];        // Fog_Const_3: (1-g)^2, 1+g^2, -2g, in-scatter scale
    float light_dir[4];     // world-space direction to the sun
    float fog_value;        // FogValue (distance scale)
    uint32_t enabled;       // scattering on
    float pad[2];
    float ambient[4];       // AmbientColour: rgb (<= 1), w scale (4)
    float diffuse[4];       // DiffuseColour: rgb (<= 1), w
    uint32_t lit;           // ambient/diffuse lighting on (else ambient 1x, no diffuse)
    float pad2[3];
};
// Scattering and lighting for this 3D frame (call after PortMetal3DBeginFrame; null = off).
void PortMetal3DSetFog(const PortWorldFog *fog);

// GPU buffers for model data (retained; release with PortMetalReleaseBuffer).
void *PortMetalCreateBuffer(const void *data, size_t length, const char *label);
void PortMetalReleaseBuffer(void *buffer);

// Maps a point in the view (0..1 from the top-left) to the frontend's 640x480
// virtual screen, using the aspect mode of the last frame drawn.
void PortMetalViewToFEScreen(float u, float v, float *x, float *y);
