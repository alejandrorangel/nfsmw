#include "Speed/Indep/Src/Render/RenderConn.h"
#include "Speed/Indep/Src/Animation/AnimPlayer.hpp"
#include "Speed/Indep/Src/Ecstasy/EmitterSystem.h"
#include "Speed/Indep/Src/World/VehicleRenderConn.h"

void SmackableRender_Init();
void SmackableRender_Shutdown();
void SmackableRender_Service(float dT);
void CarRender_Service(float dT);

namespace RenderConn {

// speed.exe 1.3: 0x6E79D0
void InitServices() {
    SmackableRender_Init();
}

// speed.exe 1.3: 0x6E79E0
void RestoreServices() {
    SmackableRender_Shutdown();
}

// speed.exe 1.3: 0x6E79F0 (VehicleRenderConn::UpdateLoading inlined): hands
// each car whose models finished loading its render info
void UpdateLoading() {
    VehicleRenderConn::UpdateLoading();
}

// speed.exe 1.3: 0x6E7A00
void UpdateServices(float dT) {
    SmackableRender_Service(dT);
    CarRender_Service(dT);
    gEmitterSystem.Update(dT);
    TheAnimPlayer.UpdateTime(dT);
}

} // namespace RenderConn
