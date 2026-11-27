#ifndef DYNAMICS_ARTICULATION_H
#define DYNAMICS_ARTICULATION_H

#include "Speed/Indep/Src/Physics/Dynamics.h"

namespace Dynamics {
namespace Articulation {

// total size: 0x4
struct HJOINT__ {
    int unused; // offset 0x0, size 0x4
};
typedef HJOINT__ *HJOINT;

enum eJointFlags {
    JF_NONE = 0,
    JF_IMMOBILE_MALE = 1,
    JF_IMMOBILE_FEMALE = 2,
};

enum eConstraint {
    PRISMATIC = 0,
    HYPERBOLIC = 1,
    CONICAL = 2,
};

HJOINT Create(IEntity *female, const UMath::Vector3 &female_arm, IEntity *male, const UMath::Vector3 &male_arm, eJointFlags flags);
HJOINT Create(IEntity *female, IEntity *male, const UMath::Vector3 &worldpos, eJointFlags flags);
void Constrain(HJOINT hJoint, IEntity *female, const UMath::Matrix4 &orient, float minTheta, float maxTheta, const UMath::Vector3 &post,
               eConstraint type);
void Release(HJOINT hJoint);
bool IsJoined(const IEntity *rb0, const IEntity *rb1);
void Resolve();

} // namespace Articulation
} // namespace Dynamics

#endif
