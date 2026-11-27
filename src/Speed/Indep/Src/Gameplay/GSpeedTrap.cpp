#include "Speed/Indep/Src/Gameplay/GSpeedTrap.h"
#include "Speed/Indep/Src/Gameplay/GIcon.h"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GTrigger.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/gameplay.h"

// The world's speed traps (career free roam): GManager keeps one GSpeedTrap
// (0x14 bytes) per gameplay collection derived from the "speedtrap" template
// under "speedtraps", in the order GatherInstanceKeys finds them.

// PC: 0x5E5890 (inlined into AllocateSpeedTraps / ResetSpeedTraps).
void GSpeedTrap::Init(Attrib::Key trapKey) {
    mSpeedTrapKey = trapKey;
    Attrib::Gen::gameplay trap(mSpeedTrapKey, 0, nullptr);
    mCameraMarkerKey = trap.CameraModelMarker().mCollectionKey;
    mFlags = 0;
    mBinNumber = static_cast<uint16>(trap.BinIndex());
    mRequiredValue = trap.ThreshholdSpeed() * 0.27778f; // km/h -> m/s
    mRecordedValue = 0.0f;
}

// PC: 0x5E56C0.
float GSpeedTrap::GetBounty() const {
    Attrib::Gen::gameplay trap(mSpeedTrapKey, 0, nullptr);
    if (!trap.IsValid()) {
        return 0.0f;
    }
    return static_cast<float>(trap.Bounty());
}

// PC: 0x5E5810.
Attrib::Key GSpeedTrap::GetJumpMarkerKey() const {
    Attrib::Gen::gameplay trap(mSpeedTrapKey, 0, nullptr);
    return trap.SpawnPoint().mCollectionKey;
}

// PC: 0x601730. An unlocked, active trap passed at `value` (m/s) is
// completed: it records the speed and refreshes its bin's progress and the
// map icons.
void GSpeedTrap::NotifyTriggered(float value) {
    if (!GetIsUnlocked() || !GetIsActive()) {
        return;
    }
    ClearFlag(kFlag_Active);
    SetFlag(kFlag_Completed);
    mRecordedValue = value;
    GRaceBin *bin = GRaceDatabase::Get().GetBinNumber(mBinNumber);
    if (bin != nullptr) {
        bin->RefreshProgress();
    }
    GManager::Get().RefreshSpeedTrapIcons();
}

// PC: 0x5F48E0. Adds the keys of every collection below `collection`
// (following the Children lists) that derives from templateKey.
void GManager::GatherInstanceKeys(Attrib::Gen::gameplay &collection, AttribKeyList &list, Attrib::Key templateKey) {
    Attrib::Key parent = collection.GetParent();
    while (parent != 0) {
        if (parent == templateKey) {
            list.push_back(collection.GetCollection());
            break;
        }
        Attrib::Gen::gameplay parentCollection(parent, 0, nullptr);
        parent = parentCollection.GetParent();
    }
    for (unsigned int i = 0; i < collection.Num_Children(); i++) {
        Attrib::Gen::gameplay child(collection.Children(i).mCollectionKey, 0, nullptr);
        GatherInstanceKeys(child, list, templateKey);
    }
}

// PC: 0x5F4E50 (from InitializeVaults).
void GManager::AllocateSpeedTraps() {
    Attrib::Gen::gameplay speedTraps(0x49511906, 0, nullptr); // "speedtraps"
    AttribKeyList keys;
    GatherInstanceKeys(speedTraps, keys, 0xB05871D3); // "speedtrap"
    mNumSpeedTraps = static_cast<unsigned int>(keys.size());
    mSpeedTraps = static_cast<GSpeedTrap *>(gFastMem.Alloc(mNumSpeedTraps * sizeof(GSpeedTrap), nullptr));
    bMemSet(mSpeedTraps, 0, mNumSpeedTraps * sizeof(GSpeedTrap));
    GSpeedTrap *trap = mSpeedTraps;
    for (AttribKeyList::iterator it = keys.begin(); it != keys.end(); ++it, ++trap) {
        trap->Init(*it);
    }
}

// PC: 0x5F4FA0 (from ResetAllGameplayData).
void GManager::ResetSpeedTraps() {
    if (mNumSpeedTraps == 0) {
        return;
    }
    Attrib::Gen::gameplay speedTraps(0x49511906, 0, nullptr);
    AttribKeyList keys;
    GatherInstanceKeys(speedTraps, keys, 0xB05871D3);
    GSpeedTrap *trap = mSpeedTraps;
    for (AttribKeyList::iterator it = keys.begin(); it != keys.end(); ++it, ++trap) {
        trap->Init(*it);
    }
}

// PC: 0x5DDCB0. Copies saved traps back over the ones with the same key.
void GManager::LoadSpeedTraps(GSpeedTrap *src, unsigned int count) {
    for (; count != 0; count--, src++) {
        for (unsigned int i = 0; i < mNumSpeedTraps; i++) {
            if (mSpeedTraps[i].GetSpeedTrapKey() == src->GetSpeedTrapKey()) {
                mSpeedTraps[i] = *src;
                break;
            }
        }
    }
}

// PC: 0x5E9D40.
GSpeedTrap *GManager::GetFirstSpeedTrap(bool activeOnly, unsigned int binNumber) {
    return GetNextSpeedTrap(mSpeedTraps - 1, activeOnly, binNumber);
}

// PC: 0x5E4770. binNumber 0 matches every bin.
GSpeedTrap *GManager::GetNextSpeedTrap(GSpeedTrap *current, bool activeOnly, unsigned int binNumber) {
    for (GSpeedTrap *trap = current + 1; trap < mSpeedTraps + mNumSpeedTraps; trap++) {
        if (activeOnly && !trap->GetIsActive()) {
            continue;
        }
        if (binNumber == 0 || trap->GetBinNumber() == binNumber) {
            return trap;
        }
    }
    return nullptr;
}

// PC: 0x5E9D60 (from EEnterBin). Unlocks and activates the bin's traps.
void GManager::EnableBinSpeedTraps(unsigned int binNumber) {
    for (GSpeedTrap *trap = GetFirstSpeedTrap(false, binNumber); trap != nullptr; trap = GetNextSpeedTrap(trap, false, binNumber)) {
        trap->SetFlag(GSpeedTrap::kFlag_Unlocked | GSpeedTrap::kFlag_Active);
    }
}

// PC: 0x5E9DE0. Shows the map/world icon of every active, uncompleted trap.
// TODO(port): the triggers' icons aren't allocated natively yet (GTrigger
// ctor, GManager::AllocIcon), so this finds no icons for now.
void GManager::RefreshSpeedTrapIcons() {
    for (GSpeedTrap *trap = mNumSpeedTraps != 0 ? mSpeedTraps : nullptr; trap != nullptr && trap < mSpeedTraps + mNumSpeedTraps; trap++) {
        GRuntimeInstance *instance = FindInstance(trap->GetSpeedTrapKey());
        if (instance == nullptr) {
            continue;
        }
        GIcon *icon = static_cast<GTrigger *>(instance)->GetIcon();
        if (icon == nullptr) {
            continue;
        }
        if (trap->GetIsActive() && !trap->GetIsCompleted()) {
            icon->Show();
            icon->ShowOnMap();
        } else {
            icon->Hide();
            icon->HideOnMap();
        }
    }
}
