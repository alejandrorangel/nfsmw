#ifndef PC_GRAPHICS_OPTIONS_H
#define PC_GRAPHICS_OPTIONS_H

// speed.exe 1.3's PC graphics options: the block at 0x9017B0 (current
// values), saved to and loaded from the registry key
// HKLM\Software\EA Games\Need for Speed Most Wanted under the value names in
// the comments (sub_6C1F10 / sub_6C1C00), and a block of the same layout at
// 0x901838 holding each option's maximum for this device (sub_6D2100). The
// Options > Video screen edits a copy (UIOptionsScreen +0x144).
//
// Fields without a registry name are internal toggles the presets set
// (sub_6C1990) but no menu exposes.

// total size: 0x84
struct PCGraphicsOptions {
    int Version;                     // +0x00 "VERSION" (1)
    int Size;                        // +0x04 "SIZE" (0x84)
    int CarEnvironmentMapEnable;     // +0x08 "g_CarEnvironmentMapEnable": Car Reflection Update Rate, 0 off .. 3
    int CarEnvironmentMapUpdateData; // +0x0C "g_CarEnvironmentMapUpdateData": Car Reflection Detail (Low/High)
    int Unknown10;                   // +0x10
    int Unknown14;                   // +0x14
    int Unknown18;                   // +0x18
    int Unknown1C;                   // +0x1C
    int Unknown20;                   // +0x20
    int RoadReflectionEnable;        // +0x24 "g_RoadReflectionEnable": Road Reflection Detail
    int Unknown28;                   // +0x28
    int MotionBlurEnable;            // +0x2C "g_MotionBlurEnable"
    int X360EffectsEnable;           // +0x30 g_X360EffectsEnable
    int Unknown34;                   // +0x34
    int AnimatedTextureEnable;       // +0x38 AnimatedTextureEnable
    int ParticleSystemEnable;        // +0x3C "g_ParticleSystemEnable"
    int Unknown40;                   // +0x40
    int WorldLodLevel;               // +0x44 "g_WorldLodLevel": World Level Of Detail
    int CarLodLevel;                 // +0x48 "g_CarLodLevel": Car Geometry Detail (Low/High)
    int OverBrightEnable;            // +0x4C "g_OverBrightEnable": Over Bright
    int Unknown50;                   // +0x50
    int Unknown54;                   // +0x54
    int FSAALevel;                   // +0x58 "g_FSAALevel": Full Screen Anti-Aliasing
    int Unknown5C;                   // +0x5C
    int RainEnable;                  // +0x60 "g_RainEnable": Rain Effect
    int Unknown64;                   // +0x64
    int TextureFiltering;            // +0x68 "g_TextureFiltering": 0 bilinear, 1 trilinear, 2 anisotropic
    int RacingResolution;            // +0x6C "g_RacingResolution": index into the 640x480..1600x1200 list
    int FirstTime;                   // +0x70 "FirstTime"
    int VSyncOn;                     // +0x74 "g_VSyncOn": Vsync
    int VisualTreatment;             // +0x78 "g_VisualTreatment": Visual Treatment
    int Unknown7C;                   // +0x7C
    int ShadowDetail;                // +0x80 "g_ShadowDetail": Shadow Detail
};

enum {
    PC_NUM_RESOLUTIONS = 6, // 0x8F9414 widths / 0x8F942C heights
};

extern PCGraphicsOptions gPCGraphicsOptions;    // 0x9017B0
extern PCGraphicsOptions gPCGraphicsOptionsMax; // 0x901838
extern int gPCPerformanceLevel;                 // 0x982C00 "g_PerformanceLevel": Level Of Detail 1..5 (0 on weak devices)
extern int gPCResolutionAvailable[PC_NUM_RESOLUTIONS]; // 0x93DBEC: the display has this mode
extern bool gPCGraphicsOptionsChanged;          // 0x982C39: reset the display next frame

void PCGetResolution(int index, int *width, int *height); // __getResolutionByIndex
void PCGraphicsOptionsDefault();                // Default__13VideoSettings 0x6DF8F0
void PCGraphicsOptionsApplyPerformanceLevel();  // sub_6C1B60: the Level Of Detail preset
void PCGraphicsOptionsSave();                   // sub_6C1F10
void PCGraphicsOptionsResetDisplay();           // eDisplayFrame 0x6E726B: applies gPCGraphicsOptionsChanged

#endif
