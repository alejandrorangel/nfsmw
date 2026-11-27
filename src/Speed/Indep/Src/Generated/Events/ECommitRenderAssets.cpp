#include "ECommitRenderAssets.hpp"
#include "Speed/Indep/Src/World/CarLoader.hpp"

// PC: ECommitRenderAssets 0x621280, ~ECommitRenderAssets 0x621290,
// GetEventName 0x6212E0, ECommitRenderAssets_MakeEvent_Callback 0x62B580.

ECommitRenderAssets::ECommitRenderAssets() : Event(0x10) {}

ECommitRenderAssets::~ECommitRenderAssets() {
    TheCarLoader.BeginLoading(nullptr, 0);
}

const char *ECommitRenderAssets::GetEventName() {
    return "ECommitRenderAssets";
}

void ECommitRenderAssets_MakeEvent_Callback(const void *staticData) {
    new ECommitRenderAssets();
}
