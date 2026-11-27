#include "ECollision.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/ICause.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"

namespace Sim {
namespace Collision {
void Respond(const Info &cinfo);
} // namespace Collision
} // namespace Sim

// PC: ECollision 0x6211D0 (vtable 0x8A6974), ~ECollision 0x637D30.
// RigidBody queues one per resolved ground/world/object contact; running it
// hands the contact to the collision listeners of both participants (camera,
// AI, damage, sound, infractions) and, for a car-vs-car hit where exactly one
// side carries a cause, tells that cause it hit the other one.
ECollision::ECollision(COLLISION_INFO pInfo) : Event(0x90), fInfo(pInfo) {}

ECollision::~ECollision() {
    if (fInfo.objA == nullptr) {
        return;
    }
    ISimable *simA = ISimable::FindInstance(fInfo.objA);
    if (simA == nullptr) {
        return;
    }
    ISimable *simB = nullptr;
    if (fInfo.type == COLLISION_INFO::OBJECT && fInfo.objB != nullptr) {
        simB = ISimable::FindInstance(fInfo.objB);
    }
    COLLISION_INFO cinfo = fInfo;
    Sim::Collision::Respond(cinfo);
    if (cinfo.type != COLLISION_INFO::OBJECT || simB == nullptr) {
        return;
    }
    HCAUSE causeA = simA->GetCausality();
    HCAUSE causeB = simB->GetCausality();
    HCAUSE cause;
    ISimable *from;
    ISimable *to;
    if (causeA == nullptr) {
        if (causeB == nullptr) {
            return;
        }
        cause = causeB;
        from = simB;
        to = simA;
    } else {
        if (causeB != nullptr) {
            return;
        }
        cause = causeA;
        from = simA;
        to = simB;
    }
    ICause *icause = ICause::FindInstance(cause);
    if (icause != nullptr) {
        icause->OnCausedCollision(cinfo, from, to);
    }
}

const char *ECollision::GetEventName() {
    return "ECollision";
}
