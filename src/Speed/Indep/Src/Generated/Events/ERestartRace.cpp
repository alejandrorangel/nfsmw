#include "ERestartRace.hpp"
#include "EFadeScreenOn.hpp"
#include "Speed/Indep/Src/Camera/CameraMover.hpp"
#include "Speed/Indep/Src/Camera/ICE/ICEManager.hpp"
#include "Speed/Indep/Src/EAXSound/EAXSOund.hpp"
#include "Speed/Indep/Src/Frontend/HUD/FEPkg_Hud.hpp"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Generated/Messages/MRestartRace.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/IGameState.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/ITrafficMgr.h"
#include "Speed/Indep/Src/Misc/Rumble.hpp"
#include "Speed/Indep/Src/World/VisualTreatment.h"

void World_RestoreProps();
void CleanParticlesOnRaceRestart();
void KillSkidsOnRaceRestart();

// PC: ERestartRace 0x6307F0, ~ERestartRace 0x6308E0,
// ERestartRace_MakeEvent_Callback 0x630990. The constructor tells the sound
// and the race scripts (MRestartRace) and fades out; the destructor puts the
// world back (props, particles, skids, traffic, cameras) once the scripts
// have reset the racers.
ERestartRace::ERestartRace() : Event(0x10) {
    bool is321 = false;
    if (GRaceStatus::Exists() && GRaceStatus::Get().GetActivelyRacing()) {
        GRaceParameters *parms = GRaceStatus::Get().GetRaceParameters();
        is321 = parms == nullptr || parms->GetRaceType() != GRace::kRaceType_Challenge;
    }
    if (g_pEAXSound != nullptr) {
        g_pEAXSound->ReStartRace(is321);
    }
    GRaceStatus::Get().UpdateAdaptiveDifficulty(GRaceStatus::kAdaptiveGain_FromRestart, nullptr);
    FEngHud::bIsRestartingRace = true;
    MRestartRace().Send(UCrc32(UCRC32_Gameplay));
    new EFadeScreenOn(false);
}

ERestartRace::~ERestartRace() {
    World_RestoreProps();
    CleanParticlesOnRaceRestart();
    KillSkidsOnRaceRestart();
    if (IVisualTreatment::Get() != nullptr) {
        IVisualTreatment::Get()->Reset();
    }
    if (IGameState *gameState = UTL::Collections::Singleton<IGameState>::Get()) {
        gameState->RaceReset();
    }
    if (ITrafficMgr::Exists()) {
        ITrafficMgr::Get()->FlushAllTraffic(true);
    }
    ResetCameraShakers();
    CameraMoverRestartRace();
    TheICEManager.SetGenericCameraToPlay("", "");
}

const char *ERestartRace::GetEventName() {
    return "ERestartRace";
}

void ERestartRace_MakeEvent_Callback(const void *staticData) {
    new ERestartRace();
}
