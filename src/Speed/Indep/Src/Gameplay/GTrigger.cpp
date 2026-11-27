#include "Speed/Indep/Src/Gameplay/GTrigger.h"

#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Gameplay/GIcon.h"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Generated/Messages/MTriggerEnter.h"
#include "Speed/Indep/Src/Generated/Messages/MTriggerExit.h"
#include "Speed/Indep/Src/Generated/Messages/MTriggerInside.h"
#include "Speed/Indep/Src/World/WCollisionAssets.h"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"

#include <algorithm>

// EGTriggerInternal (fires a gameplay trigger's NotifySimableTrigger).
static const unsigned int kEGTriggerInternalID = 0xc34649c0;

// Puts the trigger in (or takes it out of) the world grid.
static void AddWorldTrigger(WTrigger *trigger) {
    WCollisionAssets::Get().AddTrigger(trigger);
}

static void RemoveWorldTrigger(WTrigger *trigger) {
    WCollisionAssets::Get().RemoveTrigger(trigger);
}

// PC: 0x601790. The world trigger is a box (Dimensions, or Width by 1m) or a
// cylinder (Radius) at the collection's position, turned by its Rotation.
// Firing it queues EGTriggerInternal with the trigger's key.
GTrigger::GTrigger(const Attrib::Key &triggerKey)
    : GRuntimeInstance(triggerKey, kGameplayObjType_Trigger), //
      mTriggerEnabled(0),                                     //
      mIcon(nullptr),                                         //
      mEnabled(false),                                        //
      mActivationReferences(0) {
    mParticleEffect[0] = nullptr;
    mParticleEffect[1] = nullptr;

    UMath::Matrix4 rotMat = UMath::Matrix4::kIdentity;
    UMath::MultYRot(rotMat, Rotation() * (-1.0f / 360.0f), rotMat);
    UMath::Vector3 dir = {0.0f, 0.0f, 1.0f};
    UMath::Rotate(dir, rotMat, dir);
    mDirection = dir;
    mSimObjInside.reserve(8);

    const UMath::Vector3 &vaultPos = Position();
    const UMath::Vector3 *dims = static_cast<const UMath::Vector3 *>(GetAttributePointer(0x6d9e21ad, 0)); // Dimensions
    const float *radiusAttr = static_cast<const float *>(GetAttributePointer(0x39bf8002, 0));             // Radius

    float width;
    float height;
    float length;
    float radius;
    unsigned int shape;
    if (radiusAttr == nullptr) {
        if (dims == nullptr) {
            width = Width();
            height = 50.0f;
            length = 1.0f;
        } else {
            // vault axes: PC takes width, height, length from y, z, x
            width = dims->y;
            height = dims->z;
            length = dims->x;
        }
        shape = 1;
        radius = UMath::Sqrt(width * 0.5f * (width * 0.5f) + length * 0.5f * (length * 0.5f));
    } else {
        radius = *radiusAttr;
        width = radius + radius;
        height = width;
        length = width;
        shape = 3;
    }

    UMath::Matrix4 mat = UMath::Matrix4::kIdentity;
    UMath::MultYRot(mat, Rotation() * (-1.0f / 360.0f), mat);
    mWorldTrigger.fMatRow0Width.x = mat.v0.x;
    mWorldTrigger.fMatRow0Width.y = mat.v0.y;
    mWorldTrigger.fMatRow0Width.z = mat.v0.z;
    mWorldTrigger.fMatRow0Width.w = width;
    mWorldTrigger.fHeight = height + height;
    mWorldTrigger.fMatRow2Length.x = mat.v2.x;
    mWorldTrigger.fMatRow2Length.y = mat.v2.y;
    mWorldTrigger.fMatRow2Length.z = mat.v2.z;
    mWorldTrigger.fMatRow2Length.w = length;
    mWorldTrigger.fPosRadius.x = -vaultPos.y;
    mWorldTrigger.fPosRadius.y = vaultPos.z;
    mWorldTrigger.fPosRadius.z = vaultPos.x;
    mWorldTrigger.fPosRadius.w = radius;
    mWorldTrigger.fType = 1;
    mWorldTrigger.fShape = shape;
    mWorldTrigger.fFlags = 0x04810d;
    mWorldTrigger.fIterStamp = 0;
    mWorldTrigger.fFingerprint = 0;
    if (OneShot()) {
        mWorldTrigger.fFlags |= 2;
    }

    bMemSet(&mEventList, 0, sizeof(mEventList));
    bMemSet(&mEventStaticData, 0, sizeof(mEventStaticData));
    bMemSet(mTriggerEventData, 0, sizeof(mTriggerEventData));
    mEventList.fNumEvents = 1;
    mEventStaticData.fEventID = kEGTriggerInternalID;
    mEventStaticData.fEventSize = 8;
    mEventStaticData.fDataOffset = 0x10;
    reinterpret_cast<unsigned int *>(mTriggerEventData)[1] = GetCollection();
    mWorldTrigger.fEvents = &mEventList;

    // The template's flag and the trigger's icon (at the vault position):
    // an event's by its race type (the rival's for a boss race), the gates',
    // the speed traps' (in a speed trap race unless OpenWorldSpeedTrap), an
    // area unlock's. A gate / speed trap / area icon with a BinIndex shows
    // while the career is at that bin or a lower one.
    GIcon::Type iconType = GIcon::kType_Invalid;
    if (IsDerivedFromTemplate(0xf05931ab)) { // engagetrigger
        SetFlag(kFlag_TriggerEngage);
        GRaceParameters *race = GRaceDatabase::Get().GetRaceFromKey(TargetActivity().mCollectionKey);
        if (race == nullptr) {
            return;
        }
        if (race->GetIsBossRace()) {
            iconType = GIcon::kType_RaceRival;
        } else {
            switch (race->GetRaceType()) {
            case GRace::kRaceType_P2P:
                iconType = GIcon::kType_RaceSprint;
                break;
            case GRace::kRaceType_Circuit:
                iconType = GIcon::kType_RaceCircuit;
                break;
            case GRace::kRaceType_Drag:
                iconType = GIcon::kType_RaceDrag;
                break;
            case GRace::kRaceType_Knockout:
                iconType = GIcon::kType_RaceKnockout;
                break;
            case GRace::kRaceType_Tollbooth:
                iconType = GIcon::kType_RaceTollbooth;
                break;
            case GRace::kRaceType_SpeedTrap:
                iconType = GIcon::kType_RaceSpeedtrap;
                break;
            default:
                return;
            }
        }
        mIcon = GManager::Get().AllocIcon(iconType, vaultPos, 0.0f, false);
        return;
    } else if (IsDerivedFromTemplate(0x73049919)) {
        SetFlag(kFlag_TriggerCarLot);
        iconType = GIcon::kType_GateCarLot;
    } else if (IsDerivedFromTemplate(0x4698966b)) {
        SetFlag(kFlag_TriggerCustomShop);
        iconType = GIcon::kType_GateCustomShop;
    } else if (IsDerivedFromTemplate(0x326427bb)) {
        SetFlag(kFlag_TriggerSafeHouse);
        iconType = GIcon::kType_GateSafehouse;
    } else if (IsDerivedFromTemplate(0xb05871d3)) {
        SetFlag(kFlag_TriggerSpeedTrap);
        iconType = OpenWorldSpeedTrap() ? GIcon::kType_SpeedTrap : GIcon::kType_SpeedTrapInRace;
    } else if (IsDerivedFromTemplate(0x45e28759)) {
        SetFlag(kFlag_TriggerFinishLine);
    } else if (IsDerivedFromTemplate(0x76720f56)) {
        SetFlag(kFlag_TriggerTimeBonus);
    } else if (IsDerivedFromTemplate(0xa3e939e9)) {
        SetFlag(kFlag_TriggerCashBag);
    } else if (IsDerivedFromTemplate(0x98217dbf)) {
        iconType = GIcon::kType_AreaUnlock;
    }
    if (iconType != GIcon::kType_Invalid) {
        mIcon = GManager::Get().AllocIcon(iconType, vaultPos, 0.0f, false);
    }
    if (mIcon != nullptr && BinIndex() > 0 && FEDatabase->GetCareerSettings()->GetCurrentBin() <= BinIndex()) {
        mIcon->Show();
        mIcon->ShowOnMap();
    }
}

// PC: 0x5F1750 (0x5EF2D0).
GTrigger::~GTrigger() {
    if (mTriggerEnabled != 0) {
        RemoveWorldTrigger(&mWorldTrigger);
        mTriggerEnabled = 0;
    }
    ClearParticleEffects();
    if (mIcon != nullptr) {
        GManager::Get().FreeIcon(mIcon);
        mIcon = nullptr;
    }
    // The event list is the trigger's own (the WTrigger dtor leaves it alone
    // because of flag 0x100).
    mWorldTrigger.fEvents = nullptr;
}

// PC: 0x5DB5E0.
void GTrigger::GetPosition(UMath::Vector3 &pos) {
    pos.x = mWorldTrigger.fPosRadius.x;
    pos.y = mWorldTrigger.fPosRadius.y;
    pos.z = mWorldTrigger.fPosRadius.z;
}

float GTrigger::GetRadius() {
    return mWorldTrigger.fPosRadius.w;
}

// PC: 0x5E2A40.
GActivity *GTrigger::GetTargetActivity() {
    Attrib::Key key = 0x277566f3; // TargetActivity
    return static_cast<GActivity *>(GetConnectedInstance(key, 0));
}

// PC: 0x5E2C90.
void GTrigger::Enable(bool setEnabled) {
    if (mTriggerEnabled == 0) {
        if (setEnabled) {
            AddWorldTrigger(&mWorldTrigger);
            mTriggerEnabled = 1;
        }
    } else if (!setEnabled) {
        RemoveWorldTrigger(&mWorldTrigger);
        mTriggerEnabled = 0;
    }
    if (setEnabled) {
        mWorldTrigger.Enable();
        CreateAllParticleEffects();
    } else {
        mWorldTrigger.Disable();
        ClearParticleEffects();
    }
    mEnabled = setEnabled;
}

// PC: 0x5E8540.
void GTrigger::AddActivationReference() {
    mActivationReferences++;
    if (!mEnabled) {
        if (mTriggerEnabled == 0) {
            AddWorldTrigger(&mWorldTrigger);
            mTriggerEnabled = 1;
        }
        mWorldTrigger.Enable();
        CreateAllParticleEffects();
        mEnabled = true;
    }
}

// PC: 0x5E85A0.
void GTrigger::RemoveActivationReference() {
    if (mActivationReferences > 0) {
        mActivationReferences--;
    }
    if (mActivationReferences < 1) {
        if (mTriggerEnabled != 0) {
            RemoveWorldTrigger(&mWorldTrigger);
            mTriggerEnabled = 0;
        }
        mWorldTrigger.Disable();
        ClearParticleEffects();
        mEnabled = false;
    }
}

// PC: 0x5ED640.
void GTrigger::Reset() {
    SimObjList().swap(mSimObjInside);
}

// PC: 0x5F5B50.
void GTrigger::MarkAsInside(ISimable *simable) {
    if (!IsInside(simable)) {
        mSimObjInside.push_back(simable);
    }
}

// PC: 0x5ED690.
void GTrigger::MarkAsOutside(ISimable *simable) {
    SimObjList::iterator iter = std::find(mSimObjInside.begin(), mSimObjInside.end(), simable);
    if (iter != mSimObjInside.end()) {
        mSimObjInside.erase(iter);
    }
}

bool GTrigger::IsInside(ISimable *simable) {
    return std::find(mSimObjInside.begin(), mSimObjInside.end(), simable) != mSimObjInside.end();
}

// PC: 0x5F6A30. Stimulus 1 is inside, 2 is leaving.
void GTrigger::NotifySimableTrigger(ISimable *isim, int triggerStimulus) {
    if (isim == nullptr) {
        return;
    }
    bool wasOutside = !IsInside(isim);
    GCollectionKey sender;
    sender.mCollectionKey = GetCollection();
    if (triggerStimulus == 2) {
        MarkAsOutside(isim);
        if (FireOnExit()) {
            MTriggerExit(sender, isim->GetInstanceHandle()).Post(UCrc32(UCRC32_Gameplay));
        }
    }
    if (triggerStimulus == 1) {
        if (wasOutside) {
            MarkAsInside(isim);
            MTriggerEnter(sender, isim->GetInstanceHandle()).Post(UCrc32(UCRC32_Gameplay));
        }
        MTriggerInside(sender, isim->GetInstanceHandle()).Post(UCrc32(UCRC32_Gameplay));
    }
}

// PC: 0x5E2A50. The effect group at pos (sim space, made bWare's x = z,
// y = -x, z = y), auto-updating, disabled until EnableParticleEffects.
EmitterGroup *GTrigger::CreateParticleEffect(const char *effectName, UMath::Vector3 &pos) {
    EmitterGroup *group = gEmitterSystem.CreateEmitterGroup(Attrib::StringKey(effectName), 0x8040000);
    if (group != nullptr) {
        bVector3 translation(pos.z, -pos.x, pos.y);
        bMatrix4 localWorld;
        eCreateTranslationMatrix(&localWorld, translation);
        group->SetLocalWorld(&localWorld);
        group->SetAutoUpdate(true);
        group->SubscribeToDeletion(this, NotifyEmitterGroupDelete);
        group->Disable();
        gEmitterSystem.AddEmitterGroup(group);
    }
    return group;
}

// PC: 0x5E2B10. The ParticleEffect at the trigger, or with a FlareSpacing a
// pair of them that far either side across its direction.
void GTrigger::CreateAllParticleEffects() {
    const char *effectName = ParticleEffect();
    if (effectName == nullptr || *effectName == '\0') {
        return;
    }
    UMath::Vector3 pos = {mWorldTrigger.fPosRadius.x, mWorldTrigger.fPosRadius.y, mWorldTrigger.fPosRadius.z};
    const float spacing = FlareSpacing();
    if (spacing > 0.0f) {
        const UMath::Vector3 up = {0.0f, 1.0f, 0.0f};
        UMath::Vector3 across;
        UMath::Cross(up, mDirection, across);
        UMath::Scale(across, spacing);
        UMath::Vector3 left;
        UMath::Vector3 right;
        UMath::ScaleAdd(across, -1.0f, pos, left);
        UMath::ScaleAdd(across, 1.0f, pos, right);
        mParticleEffect[0] = CreateParticleEffect(effectName, left);
        mParticleEffect[1] = CreateParticleEffect(effectName, right);
        return;
    }
    mParticleEffect[0] = CreateParticleEffect(effectName, pos);
}

// PC: 0x5DB520.
void GTrigger::ClearParticleEffects() {
    for (int i = 0; i < 2; i++) {
        if (mParticleEffect[i] != nullptr) {
            mParticleEffect[i]->UnSubscribe();
            delete mParticleEffect[i];
        }
    }
    mParticleEffect[0] = nullptr;
    mParticleEffect[1] = nullptr;
}

// PC: 0x5DB570.
void GTrigger::EnableParticleEffects(bool enabled) {
    for (int i = 0; i < 2; i++) {
        if (mParticleEffect[i] != nullptr) {
            if (enabled) {
                mParticleEffect[i]->Enable();
            } else {
                mParticleEffect[i]->Disable();
            }
        }
    }
}

// PC: 0x5DB5B0.
void GTrigger::NotifyEmitterGroupDelete(void *obj, EmitterGroup *group) {
    GTrigger *trigger = static_cast<GTrigger *>(obj);
    if (trigger->mParticleEffect[0] == group) {
        trigger->mParticleEffect[0] = nullptr;
    }
    if (trigger->mParticleEffect[1] == group) {
        trigger->mParticleEffect[1] = nullptr;
    }
}

// PC: 0x5E2C60.
void GTrigger::RefreshParticleEffects() {
    ClearParticleEffects();
    CreateAllParticleEffects();
    EnableParticleEffects(mEnabled);
}

// PC: 0x5E2D10 / 0x5E2D30.
void GTrigger::ShowIcon() {
    if (mIcon != nullptr) {
        mIcon->Show();
        mIcon->ShowOnMap();
    }
}

void GTrigger::HideIcon() {
    if (mIcon != nullptr) {
        mIcon->Hide();
        mIcon->HideOnMap();
    }
}
