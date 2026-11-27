// speed.exe 1.3's PC graphics options (see src/Speed/PC/Src/PCGraphicsOptions.hpp):
// defaults, the Level Of Detail presets, the device maxima, and their
// persistence. The PC keeps them in the registry
// (HKLM\Software\EA Games\Need for Speed Most Wanted); the port writes the same
// value names to a text file, $NFSMW_VIDEO_CONFIG or
// ~/Library/Application Support/NFSMW/VideoOptions.cfg.

#include "Speed/PC/Src/PCGraphicsOptions.hpp"

#include "../Mac/MetalBridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

// 0x9017B0's initial data.
PCGraphicsOptions gPCGraphicsOptions = {
    1, 0x84, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0,
};
// 0x901838's initial data; sub_6D2100 sets the maxima from the device caps.
PCGraphicsOptions gPCGraphicsOptionsMax = {
    1, 0x84, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0,
};
int gPCPerformanceLevel;
int gPCResolutionAvailable[PC_NUM_RESOLUTIONS];
bool gPCGraphicsOptionsChanged;

namespace {

int gFirstTime = 1; // __first_time 0x901820

const int kWidths[PC_NUM_RESOLUTIONS] = {640, 800, 1024, 1280, 1280, 1600};  // 0x8F9414
const int kHeights[PC_NUM_RESOLUTIONS] = {480, 600, 768, 960, 1024, 1200}; // 0x8F942C

// sub_6DD380: min(GPU score from the table at 0x8FAF20, CPU score from its
// MHz: <1400 1, <1800 2, <2400 3, else 4), 0 for some integrated parts. An
// Apple GPU is not in the table (which would give 1); the port scores the Mac
// like the table's best cards. TODO(port): guess.
int DetectPerformanceLevel() {
    return 4;
}

// sub_6C1990: the Level Of Detail preset.
void ApplyPreset() {
    PCGraphicsOptions &o = gPCGraphicsOptions;
    const int level = gPCPerformanceLevel;
    o.RacingResolution = 0;
    o.CarEnvironmentMapEnable = 0;
    o.CarEnvironmentMapUpdateData = 0;
    o.Unknown10 = 1;
    o.Unknown14 = 0;
    o.Unknown20 = 1;
    o.RoadReflectionEnable = 0;
    o.ParticleSystemEnable = 1;
    o.WorldLodLevel = 0;
    o.CarLodLevel = 0;
    o.RainEnable = 1;
    o.MotionBlurEnable = 1;
    o.X360EffectsEnable = 1;
    o.Unknown34 = 1;
    o.Unknown40 = 0;
    o.OverBrightEnable = 0;
    o.Unknown54 = 0;
    o.Unknown5C = 1;
    o.FSAALevel = 0;
    o.TextureFiltering = 0;
    o.Unknown28 = 0;
    o.VSyncOn = 0;
    o.VisualTreatment = 0;
    o.ShadowDetail = 0;
    if (level > 1) {
        o.Unknown10 = 2;
        o.Unknown14 = 1;
        o.WorldLodLevel = 1;
    }
    if (level > 2) {
        o.CarEnvironmentMapEnable = 1;
        o.TextureFiltering = 1;
        o.RoadReflectionEnable = 1;
        o.Unknown28 = 1;
        o.ParticleSystemEnable = 1;
        o.WorldLodLevel = 2;
        o.Unknown5C = 1;
        o.Unknown40 = 1;
        o.Unknown34 = 1;
    }
    if (level > 3) {
        o.RainEnable = 1;
        o.Unknown50 = 1;
        o.CarEnvironmentMapUpdateData = 1;
        // (__getUnkMultByGPU is called here; its result is dropped.)
        o.RacingResolution = 1;
        o.FSAALevel = 1;
        o.VisualTreatment = 1;
        o.ShadowDetail = 1;
        o.OverBrightEnable = 1;
        o.X360EffectsEnable = 1;
        o.MotionBlurEnable = 1;
    }
    if (level >= 4) {
        o.CarEnvironmentMapUpdateData = 2;
        o.RoadReflectionEnable = 2;
        o.CarLodLevel = 1;
        // Port deviation: PC's top preset leaves the car reflection update
        // rate at 1 (one env-map face per frame), which reads as laggy
        // reflections; Apple GPUs render all six faces per frame in ~2 ms,
        // so the top preset uses the slider's maximum (3). The slider still
        // works as on PC.
        o.CarEnvironmentMapEnable = 3;
        // Same deviation for the rest of the quality sliders PC's top preset
        // leaves below their maxima: anisotropic texture filtering (sharper
        // roads at grazing angles) and the full world level of detail (the
        // high-detail scenery set instead of the low-detail stand-ins).
        o.TextureFiltering = 2;
        o.WorldLodLevel = 3;
        // And anti-aliasing: PC's top preset picks FSAA level 1 (the first
        // non-maskable quality, 2x on most cards); the top preset here
        // takes the slider's maximum (the GPU's highest MSAA sample count,
        // 8x or 4x), which Apple GPUs resolve on tile memory at little cost.
        o.FSAALevel = PortMetalMaxFSAALevel();
    }
    if (gPCGraphicsOptionsMax.VisualTreatment != 0) {
        o.VisualTreatment = 1;
    }
    o.Unknown50 = 0;
    o.Unknown40 = 0;
    o.Unknown54 = 0;
    // (Under 400 MB of physical memory the world and car detail drop to 0.)
}

// sub_6C1780: every option to at most the device's maximum; the resolution
// down to the nearest mode the display has.
void ClampToMaxima() {
    int *o = &gPCGraphicsOptions.CarEnvironmentMapEnable;
    const int *m = &gPCGraphicsOptionsMax.CarEnvironmentMapEnable;
    // +0x08..+0x68 but +0x54, and +0x78, +0x80.
    for (int off = 0x08; off <= 0x80; off += 4) {
        if (off == 0x54 || off == 0x6C || off == 0x70 || off == 0x74 || off == 0x7C) {
            continue;
        }
        int i = (off - 0x08) / 4;
        if (m[i] < o[i]) {
            o[i] = m[i];
        }
    }
    int r = gPCGraphicsOptions.RacingResolution;
    if (r >= 0 && r < PC_NUM_RESOLUTIONS && gPCResolutionAvailable[r] == 0) {
        while (r >= 0 && gPCResolutionAvailable[r] == 0) {
            r--;
        }
        if (r < 0) {
            return;
        }
    }
    gPCGraphicsOptions.RacingResolution = r;
}

// sub_6D2100: the maxima from the D3D caps. The port reports what a capable
// D3D9 card gives (all effects available, depth-texture shadows, anisotropic
// filtering). FSAA: sub_6C1510 sets the maximum to the adapter's
// D3DMULTISAMPLE_NONMASKABLE quality count - 1 (CheckDeviceMultiSampleType,
// A8R8G8B8, fullscreen); the qualities are vendor-defined and grow with
// the sample count. Natively the levels are the GPU's MSAA sample counts
// beyond 1 (2x, 4x, 8x), so the maximum is how many it has.
void SetDeviceMaxima() {
    PCGraphicsOptions &m = gPCGraphicsOptionsMax;
    m.CarEnvironmentMapEnable = 3;     // 0x901840 (0 on Matrox)
    m.CarEnvironmentMapUpdateData = 1; // 0x901844
    m.Unknown20 = 1;                   // 0x901858: two effects available
    m.RoadReflectionEnable = 3;        // 0x90185C: (road reflection effect) + 2
    m.OverBrightEnable = 1;            // 0x901884
    m.MotionBlurEnable = 1;            // 0x901864
    m.Unknown54 = 1;                   // 0x90188C
    m.Unknown5C = 1;                   // 0x901894
    m.Unknown50 = 1;                   // 0x901888
    m.RainEnable = 1;                  // 0x901898
    m.Unknown64 = 1;                   // 0x90189C
    m.TextureFiltering = 2;            // 0x9018A0: anisotropic min and mag filter caps
    m.Unknown28 = 1;                   // 0x901860
    m.Unknown10 = 2;                   // 0x901848
    m.Unknown34 = 1;                   // 0x90186C
    m.AnimatedTextureEnable = 1;       // 0x901870
    m.ParticleSystemEnable = 1;        // 0x901874
    m.WorldLodLevel = 3;               // 0x90187C
    m.CarLodLevel = 1;                 // 0x901880
    m.VSyncOn = 1;                     // 0x9018AC
    m.Unknown40 = 0;                   // 0x901878
    // 0x901868: 0 on every device (0x6D2234), so the X360 effects path
    // (sub_6DAE20's light flare trails) never runs on PC. NFSMW_X360_EFFECTS=1
    // turns the path on to see it (maximum and option; no registry value
    // holds it).
    m.X360EffectsEnable = 0;
    if (const char *e = std::getenv("NFSMW_X360_EFFECTS")) {
        m.X360EffectsEnable = *e != '\0' && *e != '0' ? 1 : 0;
        gPCGraphicsOptions.X360EffectsEnable = m.X360EffectsEnable;
    }
    m.Unknown14 = 0;                   // 0x90184C
    m.VisualTreatment = 1;             // 0x9018B0: the visual treatment effect loaded
    m.Unknown7C = 0;                   // 0x9018B4
    m.ShadowDetail = 2;                // 0x9018B8: depth-texture shadows
    m.FSAALevel = PortMetalMaxFSAALevel(); // 0x901890, sub_6C1510
}

std::string ConfigPath() {
    if (const char *p = std::getenv("NFSMW_VIDEO_CONFIG")) {
        return p;
    }
    const char *home = std::getenv("HOME");
    std::string dir = std::string(home ? home : ".") + "/Library/Application Support/NFSMW";
    mkdir(dir.c_str(), 0755);
    return dir + "/VideoOptions.cfg";
}

// The registry values sub_6C1F10 writes (and sub_6C1C00 reads back, except
// "FirstTime").
struct Value {
    const char *name;
    int *value;
};
const Value kValues[] = {
    {"VERSION", &gPCGraphicsOptions.Version},
    {"SIZE", &gPCGraphicsOptions.Size},
    {"g_CarEnvironmentMapEnable", &gPCGraphicsOptions.CarEnvironmentMapEnable},
    {"g_CarEnvironmentMapUpdateData", &gPCGraphicsOptions.CarEnvironmentMapUpdateData},
    {"g_RoadReflectionEnable", &gPCGraphicsOptions.RoadReflectionEnable},
    {"g_MotionBlurEnable", &gPCGraphicsOptions.MotionBlurEnable},
    {"g_ParticleSystemEnable", &gPCGraphicsOptions.ParticleSystemEnable},
    {"g_WorldLodLevel", &gPCGraphicsOptions.WorldLodLevel},
    {"g_CarLodLevel", &gPCGraphicsOptions.CarLodLevel},
    {"g_OverBrightEnable", &gPCGraphicsOptions.OverBrightEnable},
    {"g_FSAALevel", &gPCGraphicsOptions.FSAALevel},
    {"g_RainEnable", &gPCGraphicsOptions.RainEnable},
    {"g_TextureFiltering", &gPCGraphicsOptions.TextureFiltering},
    {"g_RacingResolution", &gPCGraphicsOptions.RacingResolution},
    {"FirstTime", &gPCGraphicsOptions.FirstTime},
    {"g_PerformanceLevel", &gPCPerformanceLevel},
    {"g_VSyncOn", &gPCGraphicsOptions.VSyncOn},
    {"g_ShadowDetail", &gPCGraphicsOptions.ShadowDetail},
    {"g_VisualTreatment", &gPCGraphicsOptions.VisualTreatment},
};

// sub_6C1C00. Returns false when there is no saved key (first run).
bool Load() {
    FILE *f = std::fopen(ConfigPath().c_str(), "r");
    if (f == nullptr) {
        return false;
    }
    char name[64];
    int v;
    while (std::fscanf(f, "%63s %d", name, &v) == 2) {
        for (const Value &value : kValues) {
            if (std::strcmp(value.name, "FirstTime") != 0 && std::strcmp(value.name, name) == 0) {
                *value.value = v;
            }
        }
    }
    std::fclose(f);
    return true;
}

} // namespace

void PCGetResolution(int index, int *width, int *height) {
    if (index < 0 || index >= PC_NUM_RESOLUTIONS) {
        index = 0;
    }
    *width = kWidths[index];
    *height = kHeights[index];
}

void PCGraphicsOptionsSave() {
    std::string path = ConfigPath();
    FILE *f = std::fopen(path.c_str(), "w");
    if (f == nullptr) {
        std::fprintf(stderr, "[options] can't write %s\n", path.c_str());
        return;
    }
    gPCGraphicsOptions.FirstTime = gFirstTime;
    for (const Value &value : kValues) {
        std::fprintf(f, "%s %d\n", value.name, *value.value);
    }
    std::fclose(f);
}

void PCGraphicsOptionsApplyPerformanceLevel() {
    ApplyPreset();
    ClampToMaxima();
}

void PCGraphicsOptionsDefault() {
    gPCPerformanceLevel = DetectPerformanceLevel();
    gPCGraphicsOptions.RacingResolution = 0;
    ApplyPreset();
    ClampToMaxima();
}

// eDisplayFrame 0x6E726B: with the options changed, the PC resets the device
// at the racing resolution with the vsync setting. Natively the window's
// content becomes that size (in points; the drawable follows the window, so
// Retina screens render at twice it), vsync sets the layer's display sync and
// the texture filtering the mesh sampler.
// NFSMW_NO_DISPLAY_MODE=1 leaves the window alone.
void PCGraphicsOptionsResetDisplay() {
    gPCGraphicsOptionsChanged = false;
    int w, h;
    PCGetResolution(gPCGraphicsOptions.RacingResolution, &w, &h);
    static const bool keep_window = std::getenv("NFSMW_NO_DISPLAY_MODE") != nullptr;
    PortSetDisplayMode(keep_window ? 0 : w, keep_window ? 0 : h, gPCGraphicsOptions.VSyncOn != 0);
    // (The PC sets the filtering on each effect as it begins, 0x6C5EE9.)
    PortSetTextureFiltering(gPCGraphicsOptions.TextureFiltering);
    // The FSAA level is part of the PC device's present parameters
    // (sub_6BFAB0 0x6BFB81: MultiSampleType NONMASKABLE, quality g_FSAALevel).
    PortSetFSAALevel(gPCGraphicsOptions.FSAALevel);
}

// The graphics-options part of eInitEnginePlat (0x6E69F0) and sub_6DF9C0:
// the display modes (sub_6C1510), the saved options unless this is the first
// run, the device maxima, and on the first run the defaults, saved. The rest
// of the PC function (window, D3D device) is the Mac layer's.
int eInitEnginePlat() {
    for (int i = 0; i < PC_NUM_RESOLUTIONS; i++) {
        gPCResolutionAvailable[i] = PortDisplayModeFits(kWidths[i], kHeights[i]);
    }
    gPCResolutionAvailable[0] = 1;
    gFirstTime = Load() ? 0 : 1;
    if (gFirstTime) {
        gPCPerformanceLevel = DetectPerformanceLevel();
        if (gPCPerformanceLevel > 3) {
            gPCGraphicsOptions.FSAALevel = 1;
        }
    }
    SetDeviceMaxima();
    if (gFirstTime) {
        PCGraphicsOptionsDefault();
        PCGraphicsOptionsSave();
    }
    ClampToMaxima();
    PCGraphicsOptionsResetDisplay();
    if (std::getenv("NFSMW_TRACE_OPTIONS")) {
        std::fprintf(stderr, "[options] %s: level %d res %d envmap %d/%d world %d shadow %d vt %d vsync %d fsaa %d (%dx)\n",
                     gFirstTime ? "defaults" : "loaded", gPCPerformanceLevel, gPCGraphicsOptions.RacingResolution,
                     gPCGraphicsOptions.CarEnvironmentMapEnable, gPCGraphicsOptions.CarEnvironmentMapUpdateData,
                     gPCGraphicsOptions.WorldLodLevel, gPCGraphicsOptions.ShadowDetail, gPCGraphicsOptions.VisualTreatment,
                     gPCGraphicsOptions.VSyncOn, gPCGraphicsOptions.FSAALevel, PortMetalFSAASamples(gPCGraphicsOptions.FSAALevel));
    }
    return 0;
}
