#include "ECommitAudioAssets.hpp"
#include "Speed/Indep/Src/EAXSound/EAXSOund.hpp"

// PC: ECommitAudioAssets 0x621210, ~ECommitAudioAssets 0x621220,
// ECommitAudioAssets_MakeEvent_Callback 0x62B520.

ECommitAudioAssets::ECommitAudioAssets() : Event(0x10) {}

ECommitAudioAssets::~ECommitAudioAssets() {
    if (g_pEAXSound) {
        g_pEAXSound->CommitAssets();
    }
}

const char *ECommitAudioAssets::GetEventName() {
    return "ECommitAudioAssets";
}

void ECommitAudioAssets_MakeEvent_Callback(const void *staticData) {
    new ECommitAudioAssets();
}
