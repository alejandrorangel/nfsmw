// TODO remove
#ifdef _MSC_VER
#pragma warning(disable : 4716)
#endif

#include "Speed/Indep/Src/Physics/Common/Wheel.cpp"

#include "Speed/Indep/Src/Physics/Common/VehicleSystem.cpp"

#include "Speed/Indep/Src/Physics/PhysicsInfo.cpp"
#include "Speed/Indep/Src/Physics/PhysicsUpgrades.cpp"

#if defined(EA_PORT_NATIVE)
// Vehicle core, reconstructed from speed.exe 1.3 for the native port (no
// console split yet, so the matching builds leave these out).
#include "Speed/Indep/Src/Physics/Common/Behavior.cpp"

#include "Speed/Indep/Src/Physics/Common/VehicleBehaviors.cpp"

#include "Speed/Indep/Src/Physics/Common/Bounds.cpp"

#include "Speed/Indep/Src/Physics/Common/PhysicsObject.cpp"

#include "Speed/Indep/Src/Physics/Common/PVehicle.cpp"

#include "Speed/Indep/Src/Physics/PhysicsTunings.cpp"
#endif
