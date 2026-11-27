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
#include "Speed/Indep/Src/Misc/GameFlow.hpp"

#include "../Mac/MetalBridge.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
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

void SubmitPoly(ePoly *poly, TextureInfo *texture_info, TextureInfo *mask = nullptr) {
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
        std::fprintf(stderr, "[fe] tex=%s mask=%s flags=%#x\n", texture_info ? texture_info->DebugName : "(none)",
                     mask ? mask->DebugName : "-", poly->GetFlags());
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
    SubmitPoly(poly, texture_info, texture_info_mask); // TODO: multi-texture mask (0x6D1930)
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
void PortTraceGameFlow(int frame) {
    const GameFlowManager &g = TheGameFlowManager;
    std::fprintf(stderr, "[flow] frame %d state=%d single=%s looping=%s waiting=%d(%s)\n", frame, g.CurrentGameFlowState,
                 g.pSingleFunctionName ? g.pSingleFunctionName : "-", g.pLoopingFunctionName ? g.pLoopingFunctionName : "-",
                 g.WaitingForCallback, g.pCallbackName ? g.pCallbackName : "-");
}

// The original presented with vsync, which paced the game loop; pace frames
// to the display rate (60 Hz) here so the loop doesn't spin.
static void PaceFrame() {
    static uint64_t next_ns;
    const uint64_t period_ns = 1000000000ull / 60;
    uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    if (next_ns == 0 || now > next_ns + period_ns) {
        next_ns = now; // first frame, or we fell behind: resync
    }
    if (next_ns > now) {
        usleep(static_cast<useconds_t>((next_ns - now) / 1000));
    }
    next_ns += period_ns;
}

void PortRenderScene(int frame);           // SceneNative.cpp
void PortVisualTreatmentFrame(int frame); // VisualTreatmentNative.cpp

// NFSMW_TRACE_FRAMETIME: logs frames whose period exceeds 20 ms, split into
// the game loop (since the last frame was presented) and the render build,
// plus a once-a-second summary.
int gPortLastSchedulerTicks = 0; // set by Scheduler::Run

struct FrameTimer {
    bool on = std::getenv("NFSMW_TRACE_FRAMETIME") != nullptr;
    int ticks[3] = {};
    uint64_t last_end = 0, second_start = 0;
    double worst = 0, sum = 0;
    int count = 0, slow = 0;
};

static double MsSince(uint64_t t) {
    return (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t) / 1e6;
}

void eDisplayFrame() {
    static int frame = 0;
    static FrameTimer ft;
    frame++;
    uint64_t render_start = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    double game_ms = ft.last_end ? (render_start - ft.last_end) / 1e6 : 0;
    PortRenderScene(frame);
    double scene_ms = MsSince(render_start);
    PortVisualTreatmentFrame(frame);
    int quads = RenderFrontend();
    if (ft.on) {
        double render_ms = MsSince(render_start);
        double period = game_ms + render_ms;
        if (ft.last_end && period > 20.0) {
            std::fprintf(stderr, "[frametime] frame %d: %.1f ms (game %.1f, scene %.1f, render %.1f)\n", frame, period, game_ms, scene_ms,
                         render_ms);
            ft.slow++;
        }
        ft.ticks[std::min(gPortLastSchedulerTicks, 2)]++;
        ft.worst = std::max(ft.worst, period);
        ft.sum += period;
        ft.count++;
        if (ft.second_start == 0) {
            ft.second_start = render_start;
        } else if (MsSince(ft.second_start) >= 1000.0) {
            std::fprintf(stderr, "[frametime] 1s: %d frames, avg work %.1f ms, worst %.1f ms, %d over 20 ms, sim ticks/frame 0:%d 1:%d 2+:%d\n",
                         ft.count, ft.sum / ft.count, ft.worst, ft.slow, ft.ticks[0], ft.ticks[1], ft.ticks[2]);
            ft = FrameTimer();
            ft.second_start = render_start;
        }
    }
    // The PC frame ends by recycling the per-frame scratch memory
    // (eFrameMalloc), which matrices for this frame's draws came from.
    void eSwapFrameMallocBuffers();
    eSwapFrameMallocBuffers();
    // eDisplayFrame 0x6E764B: the frame ends by advancing eFrameCounter
    // (Main_AnimateFrame services the render conns once per frame; car render
    // conns compare it against their last render frame).
    extern uint32 eFrameCounter;
    eFrameCounter++;
    PaceFrame();
    ft.last_end = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);

    if (std::getenv("NFSMW_TRACE_FLOW") && frame % 60 == 0) {
        PortTraceGameFlow(frame);
    }
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
