// The PC platform glue of the visual treatment (post-process "looks": heat,
// cop cam, frontend, bloom/desaturation/radial blur parameters).
//
// speed.exe 1.3: IVisualTreatment::Get (0x6DFAF0) lazily creates an
// IVisualTreatmentPlat (0x240 bytes; 0x6DC350 = the IVisualTreatment ctor plus
// a texture lookup and the D3D render-target setup at 0x6D4DC0);
// CloseVisualTreatment (0x6C39D0) is empty. Natively the effect isn't drawn
// yet, so the singleton is the decompiled IVisualTreatment itself: the game
// still updates its parameters every frame.

#include "Speed/Indep/Src/World/VisualTreatment.h"

namespace {
IVisualTreatment *gVisualTreatment = nullptr;
}

IVisualTreatment *IVisualTreatment::Get() {
    if (gVisualTreatment == nullptr) {
        gVisualTreatment = new IVisualTreatment();
    }
    return gVisualTreatment;
}

void OpenVisualTreatment() {
    IVisualTreatment::Get();
}

void CloseVisualTreatment() {}
