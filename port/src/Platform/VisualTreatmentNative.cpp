// The PC platform glue of the visual treatment (post-process "looks": heat,
// cop cam, frontend: bloom, desaturation, vignette).
//
// speed.exe 1.3:
//   IVisualTreatment::Get (0x6DFAF0) lazily creates an IVisualTreatmentPlat
//     (0x240 bytes; ctor 0x6DC350 = the IVisualTreatment ctor, the
//     UVESVIGNETTE texture (the effect's GAINMAP) and the effect parameter
//     handles (0x6D4DC0)). CloseVisualTreatment (0x6C39D0) is empty.
//   ___eDisplayFrame (0x6DE300) calls IVisualTreatment::Update(player view)
//     after drawing the world, then, with the visual treatment option on,
//     sub_6DBEC0/sub_6DBFE0 build the bloom source and sub_6D53D0 draws the
//     effect over the 3D scene (sub_6D4EF0 sets its constants). The frontend
//     is drawn afterwards, so it isn't graded.
// Natively the Metal renderer does the passes (MetalRenderer.mm); this file
// computes what sub_6D4EF0 and sub_6C3B20 hand to the effect.

#include "Speed/PC/Src/PCGraphicsOptions.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/World/VisualTreatment.h"
#include "Speed/Indep/Src/World/Rain.hpp"
#include "Speed/Indep/Src/World/ScreenEffects.hpp"

#include "../Mac/MetalBridge.h"

#include <cstdio>
#include <cstdlib>

namespace {

class IVisualTreatmentPlat : public IVisualTreatment {
  public:
    IVisualTreatmentPlat() {
        // Not initialised by the PC ctor (0x74B8B0) either; UpdateHeat only
        // applies it when >= 0 and nothing sets it, so make it "unset".
        this->ColourBloomIntensityTarget = -1.0f;
    }

    void Fill(PortVisualTreatment &out);
    void Trace();
};

IVisualTreatmentPlat *gVisualTreatment = nullptr;

// sub_6C3B20: one A8R8G8B8 texel per 1/255 step of the three curves.
uint32_t SplineTexel(float x, bMatrix4 *black, bMatrix4 *colour, bMatrix4 *detail) {
    auto channel = [x](bMatrix4 *curve) {
        float v = GetValueFromSpline(x, curve);
        v = v > 1.0f ? 1.0f : (v < 0.0f ? 0.0f : v);
        return static_cast<uint32_t>(v * 255.0f) & 0xFF; // __ftol2 truncates
    };
    return 0xFF000000u | channel(black) << 16 | channel(colour) << 8 | channel(detail);
}

} // namespace

// sub_6D4EF0 with the PS 2.0 technique (effect +0x28 != 1) and the always-set
// flag byte 0x8F9B5D, so the tuning blends at 0x9018F4..0x901928 drop out.
void IVisualTreatmentPlat::Fill(PortVisualTreatment &out) {
    float black = this->BlackBloomIntensity;
    out.black_bloom[0] = black;
    out.black_bloom[1] = 1.0f - black;
    out.colour_bloom = 1.0f /* 0x901914 */ * this->ColourBloomIntensity;

    // The tint is blended toward its own luminance by a timer (0x982C78 ms,
    // 0 past one second) that nothing writes, so it stays the look's tint.
    const float t = 0.0f;
    const bVector4 &tint = this->ColourBloomTint;
    float lum = tint.x * 0.2125f + tint.y * 0.7154f + tint.z * 0.0721f;
    out.tint[0] = (1.0f - t) * tint.x + t * lum;
    out.tint[1] = (1.0f - t) * tint.y + t * lum;
    out.tint[2] = (1.0f - t) * tint.z + t * lum;
    out.tint[3] = (1.0f - t) * tint.w + t * lum;

    out.desaturation[0] = this->Desaturation;
    out.desaturation[1] = 1.0f - this->Desaturation;
    out.brightness = this->PulseBrightness + 1.0f;
    out.vignette = this->PursuitBreakerBlend;
    out.pad = 0.0f;

    for (int i = 0; i < 256; i++) {
        out.spline[i] = SplineTexel(static_cast<float>(i) * (1.0f / 255.0f), &this->BlackBloomCurve, &this->ColourBloomCurve,
                                    &this->DetailMapCurve);
    }

    // 0x6DC350: GetTextureInfo(bStringHash("UVESVIGNETTE"), 1, 0); looked up
    // per frame here since texture packs come and go.
    static const uint32 vignette_hash = bStringHash("UVESVIGNETTE");
    TextureInfo *gain = GetTextureInfo(vignette_hash, 0, 0);
    out.gain_map = (gain != nullptr && gain->GetPlatInfo() != nullptr) ? gain->GetPlatInfo()->MetalTexture : nullptr;
}

void IVisualTreatmentPlat::Trace() {
    std::fprintf(stderr,
                 "[vt] state %d black %.3f colour %.3f desat %.3f tint (%.3f %.3f %.3f %.3f) pulse %.3f pb %.3f detail %.3f blur %.3f\n",
                 this->State, this->BlackBloomIntensity, this->ColourBloomIntensity, this->Desaturation, this->ColourBloomTint.x,
                 this->ColourBloomTint.y, this->ColourBloomTint.z, this->ColourBloomTint.w, this->PulseBrightness,
                 this->PursuitBreakerBlend, this->DetailMapIntensity, this->RadialBlur);
    bMatrix4 *curves[3] = {&this->BlackBloomCurve, &this->ColourBloomCurve, &this->DetailMapCurve};
    for (int i = 0; i < 3; i++) {
        bMatrix4 *c = curves[i];
        std::fprintf(stderr, "[vt]   curve %d: (%.3f %.3f) (%.3f %.3f) (%.3f %.3f) (%.3f %.3f)\n", i, c->v0.x, c->v0.y, c->v1.x, c->v1.y,
                     c->v2.x, c->v2.y, c->v3.x, c->v3.y);
    }
}

IVisualTreatment *IVisualTreatment::Get() {
    if (gVisualTreatment == nullptr) {
        gVisualTreatment = new IVisualTreatmentPlat();
    }
    return gVisualTreatment;
}

void OpenVisualTreatment() {
    IVisualTreatment::Get();
}

void CloseVisualTreatment() {}

// Called once a frame after the 3D scene was recorded: the tail of the PC
// ___eDisplayFrame (IVisualTreatment::Update, then sub_6D53D0's constants).
// NFSMW_NO_TREATMENT=1 leaves the scene ungraded, for comparison.
void PortVisualTreatmentFrame(int frame) {
    static const bool off = [] {
        const char *e = std::getenv("NFSMW_NO_TREATMENT");
        return e != nullptr && *e != '\0' && *e != '0';
    }();
    if (gVisualTreatment == nullptr) {
        return;
    }
    eView *view = eGetView(1, false);
    if (view == nullptr) {
        return;
    }
    gVisualTreatment->Update(view);
    static const bool trace = std::getenv("NFSMW_TRACE_TREATMENT") != nullptr;
    if (trace && frame % 120 == 0) {
        gVisualTreatment->Trace();
        TextureInfo *gain = GetTextureInfo(bStringHash("UVESVIGNETTE"), 0, 0);
        std::fprintf(stderr, "[vt]   gain map UVESVIGNETTE %p (%dx%d)\n", (void *)gain, gain ? gain->Width : 0, gain ? gain->Height : 0);
    }
    // ___eDisplayFrame 0x6DF5A4: over bright (sub_6DC040) with the Over Bright
    // option (0x9017FC, byte 0x982B26). Natively it rides on the treatment's
    // offscreen frame. TODO(port): with Visual Treatment off the PC adds it
    // to the back buffer directly; not done here.
    if (!off && gPCGraphicsOptions.OverBrightEnable != 0) {
        PortOverBright ob;
        ScreenEffectDB *se = view->ScreenEffects;
        float cloud = view->Precipitation ? view->Precipitation->GetCloudIntensity() : 0.0f; // Rain +0x290
        float intensity;
        if (se != nullptr && se->IsActive(SE_GLARE)) {
            // Glare (SE_inf[3].active, view ScreenEffects +0x30): offset
            // (1 - intensity) (0.3 + 0.2 cloud) (0x8F9B48/4C), at least 0;
            // blend factor intensity x 1 (0x8FAE64) x 200 (0x8970CC).
            float glare = se->GetIntensity(SE_GLARE);
            ob.offset = (1.0f - glare) * (0.3f + 0.2f * cloud);
            ob.offset = ob.offset > 0.0f ? ob.offset : 0.0f;
            intensity = glare * 200.0f;
        } else {
            // 0.35 + 0.5 cloud (0x8F9B50/54); blend factor 0.6 (0x8F9B30) with
            // the visual treatment, else 0.8 (0x8F9B2C), of 255.
            ob.offset = 0.35f + 0.5f * cloud;
            intensity = (gPCGraphicsOptions.VisualTreatment != 0 ? 0.6f : 0.8f) * 255.0f;
            intensity = intensity < 255.0f ? intensity : 255.0f;
        }
        ob.offset = ob.offset < 1.0f ? ob.offset : 1.0f;
        // __ftol2, then the low byte replicated into D3DRS_BLENDFACTOR.
        ob.intensity = static_cast<float>(static_cast<int>(intensity) & 0xFF) / 255.0f;
        PortMetal3DSetOverBright(&ob);
    }
    // sub_6D57C0: the treatment (sub_6D53D0) only with the Visual Treatment
    // option (0x901828) on.
    if (off || gPCGraphicsOptions.VisualTreatment == 0) {
        return;
    }
    static PortVisualTreatment params;
    gVisualTreatment->Fill(params);
    PortMetal3DSetTreatment(&params);
}
