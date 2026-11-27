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
    float mu, mv;          // mask texture coordinates (ePoly::UVsMask)
};

void PortMetalFEBeginFrame();
// Quad corners in order 0..3 around the perimeter (triangulated 0-1-2, 0-2-3).
void PortMetalFEAddQuad(void *texture, PortBlendMode blend, const PortFEVertex quad[4]);
// IDI_FE_MASK_FX (eViewPlatInterface::Render 0x6D1930): as above, with the
// alpha also multiplied by the mask texture's alpha at the mask coordinates.
void PortMetalFEAddMaskedQuad(void *texture, void *mask, PortBlendMode blend, const PortFEVertex quad[4]);
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
    void *texture2;      // effect's second texture (GLOSSYWINDOW: WindowReflection;
                         // WORLDNORMALMAP/WORLDREFLECT: NormalMapTexture), or null
    void *texture3;      // WORLDNORMALMAP/WORLDREFLECT: SpecularMapTexture, or null
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
    // Car effect WorldView (eDynamicLightContext::EnvMapMatrix rows 0..2,
    // xyzw each), when PORT_3D_ENVMAP: takes the model-space reflection
    // vector into the space of the environment cube.
    float envmap[12];
    // PC shadow level (sub_6E0AB0): 0 no shadow map (technique #0), 1 one
    // hardware-compare tap, 2 four taps (PCF). Only effects WORLD,
    // WORLDREFLECT, WORLDNORMALMAP, CAR and GLOSSYWINDOW use it.
    uint32_t shadow_level;
};
enum { PORT_3D_ALPHA_BLEND = 1, PORT_3D_ADDITIVE = 2, PORT_3D_ALPHA_TEST = 4, PORT_3D_LIT = 8, PORT_3D_ENVMAP = 16 };

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

// PC OVERBRIGHT (speed.exe 1.3 sub_6DC040, with the Over Bright option):
// IDI_OVERBRIGHT_FX pContrast on a quarter-size copy of the frame
// (saturate(2 (c - offset)) times its grey level), the bloom blur, then
// added over the treated frame scaled by intensity (D3DRS_BLENDFACTOR / 255).
struct PortOverBright {
    float offset;    // OverBrightOffset (sub_6D2CD0 0x6D3230)
    float intensity; // 0..1
};
// Null = off for this frame (call after PortMetal3DBeginFrame).
void PortMetal3DSetOverBright(const PortOverBright *overbright);

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
    // The normal-mapped world effects' SpecularColour (TimeOfDay +0x40, the
    // blended 'timeofdaylighting' SpecularColour; sub_6D6AA0) and
    // SpecularPower (0x8FADC0 = 11).
    float specular[4];
    float specular_power;
    // IDI_WORLDREFLECT_FX's SpecularColour.rgb is the above times (1 - the
    // view's Rain cloud intensity) (its effect begin, sub_6D71F0); w (above)
    // is Rain +0x36A0, the road reflection strength, for both.
    float reflect_specular_scale;
    float pad3[2];
};
// Scattering and lighting for this 3D frame (call after PortMetal3DBeginFrame; null = off).
void PortMetal3DSetFog(const PortWorldFog *fog);

// IDI_SKYBOX_FX (effect 19) constants, set per effect begin by speed.exe 1.3
// sub_6C60F0: SKY_TEXTURE_MISC_1..3 (SKY_NEXGEN_CAP_A, SKY_NEXGEN_CLOUDS_B,
// SKY_NEXGEN_CAP_B, looked up by NotifySkyLoader 0x6D1CF0), CloudScroll and
// CloudIntensity (SKY_DIFFUSESCALE).
struct PortSky {
    void *misc[3]; // id<MTLTexture> or null
    float cloud_scroll;
    float pad[3];
    float cloud_intensity[4];
};
void PortMetal3DSetSky(const PortSky *sky);

// The car effect's EnvMapTex (IDI_CAR_FX envmap_sampler), a cube with faces
// 0..5 = +X, -X, +Y, -Y, +Z, -Z (D3D order and orientation). speed.exe 1.3
// (sub_6BD4B0) keeps two: a 256x256 render-target cube drawn every frame in
// game from the six env-map views, and a static one built from the garage
// textures for the frontend (sub_6C5B80 picks by game flow state).
enum PortEnvMapMode {
    PORT_ENVMAP_NONE,    // no cube this frame (shader fallback)
    PORT_ENVMAP_DYNAMIC, // the render-target cube (faces from PortMetal3DBeginEnvFace)
    PORT_ENVMAP_STATIC,  // a cube from PortMetalCreateCube
};
void PortMetal3DSetEnvMap(PortEnvMapMode mode, void *static_cube);
// Draws added until PortMetal3DEndEnvFace go to that face of the dynamic cube,
// rendered before this frame's scene (cleared to black, PC sub_6BD3B0).
void PortMetal3DBeginEnvFace(int face, const float view_proj[16], const float eye[3]);
void PortMetal3DEndEnvFace();
// PC shadow map (speed.exe 1.3 sub_6E44C0, NVIDIA path): draws added between
// PortMetal3DBeginShadow and PortMetal3DEndShadow go to the 1024x1024 shadow
// map (D24S8 depth texture 0x982CF8 plus an A8R8G8B8 colour map 0x982CF0,
// cleared to depth 1 / black), rendered before this frame's scene with
// view_proj (world -> shadow clip). CAR draws use IDI_CAR_SHADOW_MAP_FX
// (depth, and red = 1 in the colour map); everything else writes depth only
// (ColorWriteMode 0). Depth bias 4 / 2^24, slope scale 4 (sub_6E54E0).
void PortMetal3DBeginShadow(const float view_proj[16], int shadow_detail);
void PortMetal3DEndShadow();
// The effects' ShadowTransform (world -> shadow texture, projective) and
// InvShadowStrength for the shadowed techniques this frame.
void PortMetal3DSetShadowTransform(const float shadow_transform[16], float inv_shadow_strength);

// PC road reflection (speed.exe 1.3 ___eDisplayFrame 0x6DE3FA, Road
// Reflection Detail 0x9017D4 > 0): draws added between
// PortMetal3DBeginReflection and PortMetal3DEndReflection go to the 320x240
// reflection target (render target 1, 0x982A6C / surface 0x982A68, depth
// 0x982A78; cleared black, depth 1) with the player's view_proj, rendered
// before this frame's scene. Effects as in the other non-main views (WORLD
// -> WORLDNOFOG, WORLDREFLECT / WORLDNORMALMAP -> WORLDNORMALMAPNOFOG).
void PortMetal3DBeginReflection(const float view_proj[16]);
void PortMetal3DEndReflection();
// IDI_WORLDREFLECT_FX's ReflectedTex this frame: mode 0 the 1x1 white
// texture sub_6BCDC0 makes at Road Reflection Detail 0, 1 the reflection
// target (whatever it last held). blur: sub_6D4960 (detail 3, racing, wet)
// blurs the target this frame (kernels 0x8F9530 then 0x8F9500 via the
// second 320x240 target 0x982A74).
void PortMetal3DSetReflection(int mode, bool blur);

// IDI_PARTICLES_FX quads (technique 0: texture x vertex colour x 2, ZFUNC
// LESSEQUAL, rgb writes), drawn after the main view's world, without depth
// writes (___eDisplayFrame 0x6DF539 around Rain::UpdateAndRender). Each quad
// is 4 corners (triangles 0-1-2, 0-2-3) in the space world maps to world
// space; color is a D3DCOLOR (0xAARRGGBB). flags: PORT_3D_ALPHA_BLEND /
// _ADDITIVE / _ALPHA_TEST from the texture (sub_6C68B0). Rain::Render's
// streaks (batch 0x9877CC) go through here.
struct PortParticleVertex {
    float x, y, z;
    uint32_t color;
    float u, v;
};
void PortMetal3DAddParticles(void *texture, uint32_t flags, const float world[16], const PortParticleVertex *quads, int num_quads);

// IDI_RAIN_DROP_FX (speed.exe 1.3 sub_6D4620): drops on the lens, drawn over
// the frame before the visual treatment. Each is a size x size clip-space
// square from (2x - 1, 1 - 2y) up and right, refracting a copy of the frame
// through the RAINDROPPC displacement texture: frame uv = (x - size/4 +
// d.g size, y + size/4 - d.b size), 1.2 x its colour, alpha d.a x alpha.
struct PortRainDrop {
    float x, y;  // 0..1 from the top left
    float size;  // OnScreenRain Size x 0.12
    float alpha; // RAINDROPALPHA: timer / Maxtimer x 3
};
void PortMetal3DSetRainDrops(void *displace, const PortRainDrop *drops, int count);

// PC Full Screen Anti-Aliasing (g_FSAALevel): 0 off, else the 3D scene is
// rendered multisampled and resolved before the post chain. The PC slider's
// levels are D3DMULTISAMPLE_NONMASKABLE quality levels (sub_6BFAB0: type 1,
// quality g_FSAALevel; maximum = CheckDeviceMultiSampleType's count - 1,
// sub_6C1510); natively level n uses the n-th supported sample count of
// 2, 4, 8.
void PortSetFSAALevel(int level);
int PortMetalMaxFSAALevel(); // how many of 2x/4x/8x the GPU supports (0..3)
int PortMetalFSAASamples(int level); // the sample count a level maps to

// A cube texture from six 2D textures of one size and format (level 0 is
// copied; PC EnvMapTextureLoadedCallback copies each face's pixels).
void *PortMetalCreateCube(void *const faces[6], const char *label);

// GPU buffers for model data (retained; release with PortMetalReleaseBuffer).
void *PortMetalCreateBuffer(const void *data, size_t length, const char *label);
void PortMetalReleaseBuffer(void *buffer);

// Maps a point in the view (0..1 from the top-left) to the frontend's 640x480
// virtual screen, using the aspect mode of the last frame drawn.
void PortMetalViewToFEScreen(float u, float v, float *x, float *y);

// The PC display mode (graphics options): the window's content size in points
// (0 x 0 keeps the window as it is) and vsync. Any thread.
void PortSetDisplayMode(int width, int height, bool vsync);
// Whether a width x height window fits the main screen.
int PortDisplayModeFits(int width, int height);
// PC texture filtering for the 3D meshes: 0 bilinear, 1 trilinear, 2 anisotropic.
void PortSetTextureFiltering(int level);
