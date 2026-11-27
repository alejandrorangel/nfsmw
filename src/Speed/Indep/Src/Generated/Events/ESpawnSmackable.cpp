#include "ESpawnSmackable.hpp"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/Interfaces/SimModels/IModel.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"
#include "Speed/Indep/Src/Physics/Smackable.h"

// Fired by a SmackableTrigger (SmackableTrigger.cpp) when a rigid body runs
// into a prop: the destructor turns the prop's model into a "Smackable"
// simable placed where the prop stands.

// speed.exe 1.3: 0x63B660. A model can't smack itself: the event is dropped
// when the simable that set it off is the model's own.
ESpawnSmackable::ESpawnSmackable(UMath::Vector3 pPosition, HMODEL pScenery, UMath::Vector4 pOrientation, bool pVirginSpawn, WTrigger *pTrigger,
                                 uintptr_t phSimable)
    : Event(0x40),                    //
      fPosition(pPosition),           //
      fScenery(pScenery),             //
      fOrientation(pOrientation),     //
      fVirginSpawn(pVirginSpawn),     //
      fTrigger(pTrigger),             //
      fhSimable(phSimable) {
    if (fhSimable != 0) {
        ISimable *isimable = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(fhSimable));
        if (isimable != nullptr) {
            if (fScenery != nullptr) {
                IModel *imodel = IModel::FindInstance(fScenery);
                if (imodel != nullptr && imodel->GetWorldID() != isimable->GetWorldID()) {
                    return;
                }
            }
            fScenery = nullptr;
        }
    }
}

// speed.exe 1.3: 0x63B790. If the prop can't become a simable (or already is
// one) its trigger is armed again.
ESpawnSmackable::~ESpawnSmackable() {
    UMath::Matrix4 matrix;
    UMath::QuaternionToMatrix4(fOrientation, matrix);
    matrix.v3.x = fPosition.x;
    matrix.v3.y = fPosition.y;
    matrix.v3.z = fPosition.z;
    matrix.v3.w = 1.0f;

    if (fScenery == nullptr) {
        return;
    }
    IModel *imodel = IModel::FindInstance(fScenery);
    if (imodel == nullptr) {
        return;
    }
    if (imodel->GetSimable() == nullptr) {
        SmackableParams params(matrix, fVirginSpawn, imodel, false);
        ISimable *isimable = ISimable::CreateInstance(UCrc32("Smackable"), params);
        if (isimable != nullptr || fTrigger == nullptr) {
            return;
        }
    }
    if (fTrigger != nullptr) {
        fTrigger->Enable();
    }
}

const char *ESpawnSmackable::GetEventName() {
    return "ESpawnSmackable";
}

// speed.exe 1.3: 0x63B940. The trigger and the simable come from the event
// dynamic data WTrigger::FireEvents filled in.
void ESpawnSmackable_MakeEvent_Callback(const void *staticData) {
    const ESpawnSmackable::StaticData *data = static_cast<const ESpawnSmackable::StaticData *>(staticData);
    new ESpawnSmackable(data->fPosition, data->fScenery, data->fOrientation, data->fVirginSpawn, gEventDynamicData.fTrigger,
                        gEventDynamicData.fhSimable);
}
