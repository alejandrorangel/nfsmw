#include "EPlayEndNIS.hpp"
#include "EPlayRaceNIS.hpp"

// PC: EPlayEndNIS 0x62F420, ~EPlayEndNIS 0x643D50, EPlayEndNIS_MakeEvent_Callback
// 0x62F480. Queued when a skippable NIS that leads into another one is
// skipped (NISActivity's mSkipToNIS); the destructor starts that scene.
EPlayEndNIS::EPlayEndNIS(const char *pSceneName)
    : Event(0x10), //
      fSceneName(EventManager::EmbedField(this, pSceneName)) {}

EPlayEndNIS::~EPlayEndNIS() {
    new EPlayRaceNIS(nullptr, fSceneName, "Intro", 0, 0, "", "");
}

const char *EPlayEndNIS::GetEventName() {
    return "EPlayEndNIS";
}

void EPlayEndNIS_MakeEvent_Callback(const void *staticData) {
    const EPlayEndNIS::StaticData *data = static_cast<const EPlayEndNIS::StaticData *>(staticData);
    new EPlayEndNIS(data->fSceneName);
}
