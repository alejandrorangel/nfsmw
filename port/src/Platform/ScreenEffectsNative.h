// PC screen effects of the 3D frame (ScreenEffectsNative.cpp).
#pragma once

class eView;

extern float gPortRadialBlur; // 0x982C74

// ___eDisplayFrame 0x6DEFCE: the tunnel-exit glare quad (IDI_WORLDMIN_FX).
void PortTunnelGlareFrame(eView *view);
// ___eDisplayFrame 0x6DF1CC: sub_6DBB20, the motion blur.
void PortMotionBlurFrame(eView *view);
