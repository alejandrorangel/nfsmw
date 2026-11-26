// Frontend rendering on Metal: native counterparts of the PC platform's
// eViewPlatInterface::FE* entry points and eDisplayFrame.
//
// speed.exe 1.3: FEBeginBatchRender/FEEndBatchRender are no-ops (0x6BE6D0,
// 0x6BE740); FERender forwards to the poly renderer at 0x6D1260, which maps
// FEng's 640x480 virtual screen to clip space (x/320 - 1, -(y/240 - 1)) and
// sets blend state from the texture (0x6C6CC0).

#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/EcstasyData.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"

#include "../Mac/MetalBridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {

PortBlendMode BlendFor(TextureInfo *t) {
    if (t != nullptr) {
        switch (t->AlphaBlendType) {
        case TEXBLEND_ADDATIVE:
        case TEXBLEND_DEST_ADDATIVE:
            return PORT_BLEND_ADDITIVE;
        case TEXBLEND_SUBTRACTIVE:
        case TEXBLEND_DEST_SUBTRACTIVE:
            return PORT_BLEND_SUBTRACTIVE;
        }
    }
    return PORT_BLEND_ALPHA;
}

void *MetalTextureOf(TextureInfo *t) {
    if (t == nullptr || t->GetPlatInfo() == nullptr) {
        return nullptr;
    }
    return t->GetPlatInfo()->MetalTexture;
}

void SubmitPoly(ePoly *poly, TextureInfo *texture_info) {
    PortFEVertex quad[4];
    for (int i = 0; i < 4; i++) {
        quad[i].x = poly->Vertices[i].x * (1.0f / 320.0f) - 1.0f;
        quad[i].y = -(poly->Vertices[i].y * (1.0f / 240.0f) - 1.0f);
        quad[i].u = poly->UVs[i][0];
        quad[i].v = poly->UVs[i][1];
        std::memcpy(quad[i].rgba, poly->Colours[i], 4);
    }
    PortMetalFEAddQuad(MetalTextureOf(texture_info), BlendFor(texture_info), quad);
    if (std::getenv("NFSMW_TRACE_FE")) {
        std::fprintf(stderr, "[fe] tex=%s flags=%#x\n", texture_info ? texture_info->DebugName : "(none)", poly->GetFlags());
        for (int i = 0; i < 4; i++) {
            std::fprintf(stderr, "     v%d pos=(%.1f, %.1f, %.2f) uv=(%.3f, %.3f) rgba=%02x %02x %02x %02x\n", i, poly->Vertices[i].x,
                         poly->Vertices[i].y, poly->Vertices[i].z, poly->UVs[i][0], poly->UVs[i][1], poly->Colours[i][0],
                         poly->Colours[i][1], poly->Colours[i][2], poly->Colours[i][3]);
        }
    }
}

} // namespace

void eViewPlatInterface::FEBeginBatchRender(int numPolys) {}

void eViewPlatInterface::FEEndBatchRender() {}

void eViewPlatInterface::FERender(ePoly *poly, TextureInfo *texture_info, int use_previous_data) {
    SubmitPoly(poly, texture_info);
}

void eViewPlatInterface::FERender(ePoly *poly, TextureInfo *texture_info, TextureInfo *texture_info_mask, int use_previous_data) {
    SubmitPoly(poly, texture_info); // TODO: multi-texture mask (0x6D1930)
}

extern float RealTimeElapsed;

static int RenderFrontend() {
    PortMetalFEBeginFrame();
    if (FEManager *fe = FEManager::Get()) {
        fe->Render();
    }
    return PortMetalFEEndFrame();
}

// The PC eDisplayFrame (0x6E7220) renders the world views, then the frontend.
// Only the frontend exists natively so far.
void eDisplayFrame() {
    int quads = RenderFrontend();

    static int frame = 0;
    frame++;
    if (std::getenv("NFSMW_TRACE_FRAMES")) {
        std::fprintf(stderr, "[frame] %d: %d FE quads\n", frame, quads);
    }
    // NFSMW_HOLD_FRAME=<seconds>: once the frontend first draws, keep ticking
    // and drawing it at 60 Hz for that long, so its animations (fades) play.
    // The boot stops shortly after this point for now.
    static bool held = false;
    const char *at = std::getenv("NFSMW_HOLD_AT_FRAME"); // default: first frame that draws anything
    if (!held && quads > 0 && (at == nullptr || frame >= std::atoi(at))) {
        if (const char *hold = std::getenv("NFSMW_HOLD_FRAME")) {
            held = true;
            int frames = std::atoi(hold) * 60;
            float saved = RealTimeElapsed;
            for (int i = 0; i < frames; i++) {
                RealTimeElapsed = 1.0f / 60.0f;
                if (FEManager *fe = FEManager::Get()) {
                    fe->Update();
                }
                RenderFrontend();
                usleep(16667);
            }
            RealTimeElapsed = saved;
        }
    }
}
