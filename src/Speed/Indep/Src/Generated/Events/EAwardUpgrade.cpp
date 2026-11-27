#include "EAwardUpgrade.hpp"
#include "Speed/Indep/Src/Frontend/Careers/UnlockSystem.hpp"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/gameplay.h"

void AwardUnlockUpgrade(Attrib::Gen::gameplay &inst);

// PC: EAwardUpgrade 0x62AA90 (vtable 0x8A7A9C), ~EAwardUpgrade 0x62ABB0
// (trivial), EAwardUpgrade_MakeEvent_Callback 0x62ABE0. The constructor does
// the work: a race's (or milestone's) reward, a gameplay collection naming
// either an upgrade to unlock or a marker to award (straight away unless it's
// a marker race's own).
EAwardUpgrade::EAwardUpgrade(unsigned int pUpgradeKey)
    : Event(0x10), //
      fUpgradeKey(pUpgradeKey) {
    Attrib::Gen::gameplay upgrade(pUpgradeKey, 0, nullptr);
    const char *upgradeType = upgrade.UpgradeType();
    const char *markerType = upgrade.RewardMarkerType();
    if (upgradeType != nullptr) {
        AwardUnlockUpgrade(upgrade);
    } else if (markerType != nullptr) {
        bool immediate = true;
        if (GRaceStatus::Exists() && GRaceStatus::Get().GetRaceParameters() != nullptr) {
            immediate = !GRaceStatus::Get().GetRaceParameters()->GetIsMarkerRace();
        }
        TheFEMarkerManager.AwardMarker(upgrade, immediate);
    }
}

EAwardUpgrade::~EAwardUpgrade() {}

const char *EAwardUpgrade::GetEventName() {
    return "EAwardUpgrade";
}

void EAwardUpgrade_MakeEvent_Callback(const void *staticData) {
    new EAwardUpgrade(static_cast<const EAwardUpgrade::StaticData *>(staticData)->fUpgradeKey);
}
