// Scene hooks for ParticlesNative.cpp (called from SceneNative.cpp's player
// view, in speed.exe 1.3 ___eDisplayFrame's order).
#pragma once

class eView;

// 0x6DEDA5: IDI_WORLDPRELIT_FX, RenderClans (the skid marks; sub_7268C0),
// before the cars.
void PortRenderSkids(eView *view);
// After the cars and the scene: 0x6DF2A3 the light flares (when enabled),
// 0x6DF472 the particles (g_ParticleSystemEnable, IDI_PARTICLES_FX).
void PortRenderWorldEffects(eView *view);
// The light flares of the road reflection / an env-map face, into prelit
// part (MetalPrelitBridge.h PORT_PRELIT_PART_*).
void PortRenderViewFlares(eView *view, int part);
