#include "GIcon.h"

#if defined(EA_PORT_NATIVE)
#include "Speed/Indep/Src/Ecstasy/EmitterSystem.h"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"
#include "Speed/Indep/Src/World/Common/WGrid.h"
#include "Speed/Indep/Src/World/TrackStreamer.hpp"
#include "Speed/Indep/Src/World/VisibleSection.hpp"
#include "Speed/Indep/Src/World/WCollisionMgr.h"
#include "Speed/Indep/Src/World/WorldModel.hpp"

// The world/map icons of the gameplay objects (events, gates, speed traps,
// hiding spots, ...), kept by GManager. The position is in bWare space (the
// vault's), the rotation a bAngle. Flags (PC): 1 shown in the world, 2 on the
// map, 4 spawned (its section loaded), 8 enabled (model and effect on), 0x10
// disposable, 0x20 snapped to the ground, 0x40 shown when spawned, 0x80 the
// GPS target.

int GIcon::sNumSpawned;

// PC: 0x8F5A58. The model and the effect group of each type.
GIcon::EffectInfo GIcon::kEffectInfo[] = {
    {kType_Invalid, 0, 0},
    {kType_RaceSprint, 0x9e535570, 0xe1eb3b0f},
    {kType_RaceCircuit, 0x3a583ca3, 0x3ccb52c5},
    {kType_RaceDrag, 0xc8e0972e, 0xc0087531},
    {kType_RaceKnockout, 0x734a6a5e, 0xb8e326bf},
    {kType_RaceTollbooth, 0xca67b527, 0x5a4699bf},
    {kType_RaceSpeedtrap, 0x3232e2f8, 0x60bb33ec},
    {kType_RaceRival, 0x4c471d8e, 0x8a2709be},
    {kType_GateSafehouse, 0x9e3223d3, 0x6d018122},
    {kType_GateCarLot, 0x85c25e95, 0xbe1ef064},
    {kType_GateCustomShop, 0x868c2e14, 0x6699d23d},
    {kType_HidingSpot, 0xd2592f60, 0xf9f71422},
    {kType_PursuitBreaker, 0, 0x7546c031},
    {kType_SpeedTrap, 0xc1fd605b, 0x60bb33ec},
    {kType_SpeedTrapInRace, 0xc1fd605b, 0x60bb33ec},
    {kType_AreaUnlock, 0, 0},
    {kType_Checkpoint, 0x738b1f9b, 0},
};

// PC: 0x5E5970.
GIcon::GIcon(Type type, const UMath::Vector3 &pos, float rotDeg)
    : mType(static_cast<uint16>(type)), //
      mFlags(0),                        //
      mSectionID(-1),                   //
      mCombSectionID(-1),               //
      mModel(nullptr),                  //
      mEmitter(nullptr),                //
      mPosition(pos) {
    mRotation = static_cast<bAngle>(static_cast<int>(rotDeg * 65536.0f) / 360);
    FindSection();
}

// PC: inlined in GManager::FreeIconAt (0x5EDD60, 0x5EBED0).
GIcon::~GIcon() {
    Unspawn();
}

// PC: 0x5DE590. The drivable section under the icon and its combined one.
void GIcon::FindSection() {
    if (mSectionID < 0 && TheVisibleSectionManager.GetInfo() != nullptr) {
        bVector2 pos(mPosition.x, mPosition.y);
        DrivableScenerySection *section = TheVisibleSectionManager.FindDrivableSection(&pos);
        if (section != nullptr) {
            mSectionID = section->SectionNumber;
            mCombSectionID = static_cast<int16>(TheTrackStreamer.GetCombinedSectionNumber(mSectionID));
        }
    }
}

// PC: 0x5DE5F0.
void GIcon::NotifyEmitterGroupDelete(void *obj, EmitterGroup *group) {
    GIcon *icon = static_cast<GIcon *>(obj);
    if (icon->mEmitter == group) {
        icon->mEmitter = nullptr;
    }
}

// PC: 0x5DE610. Disabled until the icon is enabled.
AcidEffect *GIcon::CreateParticleEffect(uint32 particleHash) {
    Attrib::Key key = particleHash;
    EmitterGroup *group = gEmitterSystem.CreateEmitterGroup(key, 0x8040000);
    if (group != nullptr) {
        group->SetAutoUpdate(true);
        group->SubscribeToDeletion(this, NotifyEmitterGroupDelete);
        group->Disable();
        gEmitterSystem.AddEmitterGroup(group);
    }
    return reinterpret_cast<AcidEffect *>(group);
}

// PC: 0x5DE690. Hidden (and casting no shadow) until the icon is enabled.
WorldModel *GIcon::CreateGeometry(uint32 modelHash) {
    WorldModel *model = new WorldModel(modelHash, nullptr, false);
    if (model != nullptr) {
        model->mEnabled = false;
        model->SetCastsShadow(0);
    }
    return model;
}

// PC: 0x5E5A90. Rotated about z, at the position.
void GIcon::SetPosition() {
    if (!IsFlagSet(4)) {
        return;
    }
    bMatrix4 m;
    bIdentity(&m);
    eRotateZ(&m, &m, mRotation);
    m.v3.x = mPosition.x;
    m.v3.y = mPosition.y;
    m.v3.z = mPosition.z;
    m.v3.w = 1.0f;
    if (mModel != nullptr) {
        mModel->SetMatrix(&m);
        if (!IsFlagSet(8)) {
            mModel->mEnabled = false;
        }
    }
    if (mEmitter != nullptr) {
        mEmitter->SetLocalWorld(&m);
    }
}

// PC: 0x5EA0F0. Hiding spots placed at height 0 get the ground's height.
void GIcon::SnapToGround() {
    if (!WGrid::Initialized() || IsFlagSet(0x20) || mPosition.z != 0.0f) {
        return;
    }
    UMath::Vector3 pt = {-mPosition.y, mPosition.z, mPosition.x};
    float height = 0.0f;
    if (WCollisionMgr(0, 3).GetWorldHeightAtPointRigorous(pt, height, nullptr)) {
        SetFlag(0x20);
        mPosition.z = height;
        SetPosition();
    }
}

// PC: 0x5EC270.
void GIcon::Spawn() {
    const EffectInfo &info = kEffectInfo[mType];
    if (mType == kType_HidingSpot) {
        SnapToGround();
    }
    if (mModel == nullptr && info.mModelHash != 0) {
        mModel = CreateGeometry(info.mModelHash);
    }
    if (mEmitter == nullptr && info.mParticleHash != 0) {
        mEmitter = reinterpret_cast<EmitterGroup *>(CreateParticleEffect(info.mParticleHash));
    }
    const uint16 flags = mFlags;
    if ((flags & 4) == 0) {
        sNumSpawned++;
    }
    mFlags = flags | 4;
    if ((mFlags & 0x40) != 0) {
        mFlags = (flags & ~0x40) | 5;
    }
}

// PC: 0x5E5A00 (Disable inlined).
void GIcon::Unspawn() {
    Disable();
    if (mModel != nullptr) {
        delete mModel;
        mModel = nullptr;
    }
    if (mEmitter != nullptr) {
        delete mEmitter;
        mEmitter = nullptr;
    }
    if (IsFlagSet(4)) {
        sNumSpawned--;
    }
    ClearFlag(4);
}

// PC: inlined (UpdateIconVisibility 0x5FDAE0, RefreshEffects 0x5EA190).
void GIcon::Enable() {
    SetPosition();
    if (mModel != nullptr) {
        mModel->SetAddLighting(true);
        mModel->mEnabled = true;
    }
    if (mEmitter != nullptr) {
        mEmitter->Enable();
    }
    SetFlag(8);
}

void GIcon::Disable() {
    if (mModel != nullptr) {
        mModel->mEnabled = false;
    }
    if (mEmitter != nullptr) {
        mEmitter->Disable();
    }
    ClearFlag(8);
}

// PC: 0x5EA190. Drops the effect group (it isn't made again until the
// icon respawns) and re-enables what's left if the icon was on.
void GIcon::RefreshEffects() {
    const bool wasEnabled = IsFlagSet(8);
    Disable();
    if (mEmitter != nullptr) {
        delete mEmitter;
        mEmitter = nullptr;
    }
    if (wasEnabled && IsFlagSet(4)) {
        Enable();
    }
}
#endif
