#include "Speed/Indep/Src/Physics/VehicleBehaviors.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"

// speed.exe 1.3: 0x677480
VehicleBehavior::VehicleBehavior(const BehaviorParams &bp, unsigned int num_interfaces) : Behavior(bp, num_interfaces), mVehicle(nullptr) {
    static_cast<ISimable *>(bp.fowner)->QueryInterface(&mVehicle);
}
