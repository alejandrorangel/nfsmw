#include "EEnableAIPhysics.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/IEngine.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISuspension.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Sim/SimTypes.h"

// PC: EEnableAIPhysics 0x6217E0 (vtable 0x8A6AA4), ~EEnableAIPhysics 0x638040,
// EEnableAIPhysics_MakeEvent_Callback 0x62BF10. AIVehicleHuman::SetAiControl
// queues one: while the AI drives the player's car the car runs the AI racers'
// SuspensionSimple instead of its own suspension (the model the race AI's
// speed and steering are tuned for), and goes back to its own when the player
// takes over again. The suspension and engine are matched to the car's speed.
EEnableAIPhysics::EEnableAIPhysics(uintptr_t phSimable, float pSpeed, unsigned int pEnable)
    : Event(0x20), //
      fhSimable(phSimable), fSpeed(pSpeed), fEnable(pEnable) {}

EEnableAIPhysics::~EEnableAIPhysics() {
    if (fhSimable == 0) {
        return;
    }
    ISimable *simable = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(fhSimable));
    if (simable == nullptr) {
        return;
    }
    IVehicle *vehicle;
    if (!simable->QueryInterface(&vehicle)) {
        return;
    }
    if (fEnable != 0) {
        vehicle->SetBehaviorOverride(UCrc32(BEHAVIOR_MECHANIC_SUSPENSION), UCrc32("SuspensionSimple"));
    } else {
        vehicle->RemoveBehaviorOverride(UCrc32(BEHAVIOR_MECHANIC_SUSPENSION));
    }
    vehicle->CommitBehaviorOverrides();
    ISuspension *suspension;
    if (vehicle->QueryInterface(&suspension)) {
        suspension->MatchSpeed(fSpeed);
    }
    IEngine *engine;
    if (vehicle->QueryInterface(&engine)) {
        engine->MatchSpeed(fSpeed);
    }
}

const char *EEnableAIPhysics::GetEventName() {
    return "EEnableAIPhysics";
}

void EEnableAIPhysics_MakeEvent_Callback(const void *staticData) {
    const EEnableAIPhysics::StaticData *data = static_cast<const EEnableAIPhysics::StaticData *>(staticData);
    new EEnableAIPhysics(data->fhSimable, data->fSpeed, data->fEnable);
}
