#include "EPlayRaceNIS.hpp"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/Animation/AnimChooser.hpp"
#include "Speed/Indep/Src/Camera/ICE/ICEManager.hpp"
#include "Speed/Indep/Src/Gameplay/GMarker.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/INIS.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Sim/SimActivity.h"
#include "Speed/Indep/bWare/Inc/Strings.hpp"
#include "Speed/Indep/bWare/Inc/bPrintf.hpp"

// PC: EPlayRaceNIS 0x640F00, ~EPlayRaceNIS 0x6414F0 (trivial),
// EPlayRaceNIS_MakeEvent_Callback 0x641520. The constructor does the work: it
// creates the NISActivity, gives it the race's cars ("car1".. in racer order)
// and the start location (the start marker, or the player's car), and loads
// the scene (it plays as soon as it's loaded).
EPlayRaceNIS::EPlayRaceNIS(GMarker *pStartMarker, const char *pSceneName, const char *pSceneType, int pPlayerIdx, int pCameraTrack,
                           const char *pPreMovie, const char *pPostMovie)
    : Event(0x30),                                          //
      fStartMarker(pStartMarker),                           //
      fSceneName(EventManager::EmbedField(this, pSceneName)), //
      fSceneType(EventManager::EmbedField(this, pSceneType)), //
      fPlayerIdx(pPlayerIdx),                               //
      fCameraTrack(pCameraTrack),                           //
      fPreMovie(EventManager::EmbedField(this, pPreMovie)), //
      fPostMovie(EventManager::EmbedField(this, pPostMovie)) {
    if (fSceneName == nullptr || bStrLen(fSceneName) == 0 || INIS::Exists()) {
        return;
    }
    CAnimChooser::eType type = CAnimChooser::Intro;
    if (bStrICmp("Outro", fSceneType) == 0) {
        type = CAnimChooser::Ending;
    } else if (bStrICmp("Arrest", fSceneType) == 0) {
        type = CAnimChooser::Arrest;
    } else if (bStrICmp("Moment", fSceneType) == 0) {
        // TODO(port): world moments (the cinematic moments option, the
        // checks on the race and the nearby cars, 0x640FF0..0x641280) aren't
        // ported; this is PC's path with the option off.
        return;
    } else if (bStrICmp("Intro", fSceneType) != 0 && fSceneType != nullptr) {
        return;
    }

    TheICEManager.SetGenericCameraToPlay("", "");
    Sim::IActivity *activity = Sim::IActivity::CreateInstance("NISActivity", Sim::Param());
    INIS *nis;
    if (activity == nullptr || !activity->QueryInterface(&nis)) {
        return;
    }

    const IVehicle::List &racers = IVehicle::GetList(VEHICLE_RACERS);
    int numCars = static_cast<int>(racers.size());
    if (numCars > 7) {
        numCars = 8;
    }
    IVehicle::List::const_iterator racer = racers.begin();
    for (int i = 0; i < numCars; i++, ++racer) {
        char channelName[32];
        bSPrintf(channelName, "car%d", i + 1);
        nis->AddCar(UCrc32(channelName), *racer);
    }

    UMath::Vector3 position = UMath::Vector3::kZero;
    UMath::Vector3 direction = UMath::Vector3::kZero;
    direction.z = 1.0f;
    if (fStartMarker != nullptr) {
        position = fStartMarker->GetPosition();
        direction = fStartMarker->GetDirection();
    } else {
        IVehicle *player = IVehicle::First(VEHICLE_PLAYERS);
        ISimable *simable = player != nullptr ? player->GetSimable() : nullptr;
        if (simable != nullptr) {
            UMath::Matrix4 transform;
            simable->GetTransform(transform);
            position = UMath::Vector4To3(transform.v3);
            direction = UMath::Vector4To3(transform.v0);
        }
    }
    float angle = 1.0f - VU0_Atan2(direction.x, direction.z);

    nis->SetPreMovie(fPreMovie);
    nis->SetPostMovie(fPostMovie);
    nis->StartLocation(position, angle);
    nis->Load(type, fSceneName, fCameraTrack, true);

    IPlayer *player = IPlayer::First(PLAYER_LOCAL);
    if (player != nullptr && player->InGameBreaker()) {
        player->ToggleGameBreaker();
    }
}

EPlayRaceNIS::~EPlayRaceNIS() {}

const char *EPlayRaceNIS::GetEventName() {
    return "EPlayRaceNIS";
}

void EPlayRaceNIS_MakeEvent_Callback(const void *staticData) {
    const EPlayRaceNIS::StaticData *data = static_cast<const EPlayRaceNIS::StaticData *>(staticData);
    new EPlayRaceNIS(data->fStartMarker, data->fSceneName, data->fSceneType, data->fPlayerIdx, data->fCameraTrack, data->fPreMovie,
                     data->fPostMovie);
}
