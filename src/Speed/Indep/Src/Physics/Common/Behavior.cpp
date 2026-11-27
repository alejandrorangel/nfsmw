#include "Speed/Indep/Src/Physics/Behavior.h"
#include "Speed/Indep/Src/Generated/AttribSys/GenericAccessor.h"
#include "Speed/Indep/Src/Sim/SimTypes.h"

// Mechanic names; the hash of each is also the key of the attribute that names
// the behaviour implementing that mechanic for a vehicle (pvehicle::BEHAVIOR_MECHANIC_*).
Attrib::StringKey BEHAVIOR_MECHANIC_AI("BEHAVIOR_MECHANIC_AI");
Attrib::StringKey BEHAVIOR_MECHANIC_RIGIDBODY("BEHAVIOR_MECHANIC_RIGIDBODY");
Attrib::StringKey BEHAVIOR_MECHANIC_INPUT("BEHAVIOR_MECHANIC_INPUT");
Attrib::StringKey BEHAVIOR_MECHANIC_SUSPENSION("BEHAVIOR_MECHANIC_SUSPENSION");
Attrib::StringKey BEHAVIOR_MECHANIC_ENGINE("BEHAVIOR_MECHANIC_ENGINE");
Attrib::StringKey BEHAVIOR_MECHANIC_DAMAGE("BEHAVIOR_MECHANIC_DAMAGE");
Attrib::StringKey BEHAVIOR_MECHANIC_DRAW("BEHAVIOR_MECHANIC_DRAW");
Attrib::StringKey BEHAVIOR_MECHANIC_AUDIO("BEHAVIOR_MECHANIC_AUDIO");
Attrib::StringKey BEHAVIOR_MECHANIC_EFFECTS("BEHAVIOR_MECHANIC_EFFECTS");
Attrib::StringKey BEHAVIOR_MECHANIC_RESET("BEHAVIOR_MECHANIC_RESET");

// speed.exe 1.3: 0x6717F0
Behavior::Behavior(const BehaviorParams &params, unsigned int num_interfaces)
    : Sim::Object(num_interfaces + 1),                         //
      mPaused(false),                                          //
      mOwner(params.fowner),                                   //
      mIOwner(params.fowner ? static_cast<ISimable *>(params.fowner) : nullptr), //
      mMechanic(params.fMechanic),                             //
      mSignature(params.fSig),                                 //
      mPriority(0),                                            //
      mProfile(nullptr) {
    // The priority is the mechanic's position in the owner's BEHAVIOR_ORDER list
    const Attrib::Gen::GenericAccessor &order =
        reinterpret_cast<const Attrib::Gen::GenericAccessor &>(params.fowner->GetAttributes());
    unsigned int num = order.Num_BEHAVIOR_ORDER();
    for (; static_cast<unsigned int>(mPriority) < num; mPriority++) {
        Attrib::StringKey key;
        order.BEHAVIOR_ORDER(key, mPriority);
        if (key.GetHash32() == mMechanic.GetValue()) {
            break;
        }
    }
}

// GC 0x8021CC28
void Behavior::Pause(bool pause) {
    if (mPaused != pause) {
        mPaused = pause;
        if (pause) {
            OnPause();
        } else {
            OnUnPause();
        }
    }
}
