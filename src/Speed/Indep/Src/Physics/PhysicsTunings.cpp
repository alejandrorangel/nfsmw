#include "Speed/Indep/Src/Physics/PhysicsTunings.h"

namespace Physics {

// speed.exe 1.3: 0x8A9350, {lower, upper} per tuning path
static const float TuningLimits[Tunings::MAX_TUNINGS][2] = {
    {-1.0f, 1.0f}, {-1.0f, 1.0f}, {-1.0f, 1.0f}, {-1.0f, 1.0f}, {-1.0f, 1.0f}, {-1.0f, 1.0f}, {-1.0f, 1.0f},
};

// speed.exe 1.3: 0x66CA10
float Tunings::LowerLimit(Path path) {
    return TuningLimits[path][0];
}

// speed.exe 1.3: 0x66CA20
float Tunings::UpperLimit(Path path) {
    return TuningLimits[path][1];
}

} // namespace Physics
