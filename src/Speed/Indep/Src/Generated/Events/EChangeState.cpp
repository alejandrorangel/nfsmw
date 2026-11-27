#include "EChangeState.hpp"
#include "Speed/Indep/Src/Gameplay/GActivity.h"
#include "Speed/Indep/Src/Gameplay/GManager.h"

// PC: EChangeState 0x621070, ~EChangeState 0x621090,
// EChangeState_MakeEvent_Callback 0x62B3F0. Running it moves the activity into
// the state (queued so a script never changes state while it runs).

EChangeState::EChangeState(unsigned int pActivityKey, unsigned int pStateKey) : Event(0x10), fActivityKey(pActivityKey), fStateKey(pStateKey) {}

EChangeState::~EChangeState() {
    GRuntimeInstance *activity = GManager::Get().FindInstance(fActivityKey);
    GRuntimeInstance *state = GManager::Get().FindInstance(fStateKey);
    if (activity != nullptr && state != nullptr && activity->GetType() == kGameplayObjType_Activity && state->GetType() == kGameplayObjType_State) {
        static_cast<GActivity *>(activity)->EnterState(static_cast<GState *>(state));
    }
}

const char *EChangeState::GetEventName() {
    return "EChangeState";
}

void EChangeState_MakeEvent_Callback(const void *staticData) {
    const EChangeState::StaticData *data = static_cast<const EChangeState::StaticData *>(staticData);
    new EChangeState(data->fActivityKey, data->fStateKey);
}
