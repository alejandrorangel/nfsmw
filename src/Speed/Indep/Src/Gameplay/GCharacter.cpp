#include "Speed/Indep/Src/Gameplay/GCharacter.h"

#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"

// PC: 0x5EF450.
GCharacter::GCharacter(const Attrib::Key &triggerKey)
    : GRuntimeInstance(triggerKey, kGameplayObjType_Character), //
      UTL::COM::Object(1),                                      //
      IAttachable(this),                                        //
      mSpawnPos(UMath::Vector3::kZero),                         //
      mState(1),                                                //
      mFlags(0),                                                //
      mCreateAttemptsMade(0),                                   //
      mSpawnDir(UMath::Vector3::kZero),                         //
      mSpawnSpeed(0.0f),                                        //
      mTargetPos(UMath::Vector3::kZero),                        //
      mVehicle(nullptr),                                        //
      mTargetDir(UMath::Vector3::kZero),                        //
      mAttachments(new Sim::Attachments(this)) {}

// PC: 0x5EF2D0 (GCharacter part).
GCharacter::~GCharacter() {
    Unspawn();
    delete mAttachments;
}

// PC: 0x5EEB90. Lets go of the spawned vehicle.
// TODO(port): spawning characters (Spawn 0x601320, GManager::AttachCharacter /
// ServicePendingCharacters) isn't ported, so there's never a vehicle to kill.
void GCharacter::Unspawn() {
    if (mVehicle != nullptr) {
        Detach(mVehicle);
        mVehicle = nullptr;
    }
    mState = 1;
}

bool GCharacter::Attach(UTL::COM::IUnknown *pOther) {
    return mAttachments->Attach(pOther);
}

bool GCharacter::Detach(UTL::COM::IUnknown *pOther) {
    return mAttachments->Detach(pOther);
}

bool GCharacter::IsAttached(const UTL::COM::IUnknown *pOther) const {
    return mAttachments->IsAttached(pOther);
}

void GCharacter::OnAttached(IAttachable *pOther) {}

void GCharacter::OnDetached(IAttachable *pOther) {
    if (UTL::COM::ComparePtr(mVehicle, pOther)) {
        mVehicle = nullptr;
    }
}

const IAttachable::List *GCharacter::GetAttachments() const {
    return &mAttachments->GetList();
}
