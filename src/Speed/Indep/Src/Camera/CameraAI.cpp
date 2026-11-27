#include "CameraAI.hpp"

void CameraAI::Reset() {
    return;
}

#if defined(EA_PORT_NATIVE)
#include "Speed/Indep/Src/Interfaces/IBody.h"

DECLARE_CONTAINER_TYPE(CameraAIAvoidables);

// TODO(port): the camera AI director isn't decompiled; this is just its list of
// bodies the cameras steer around (speed.exe 1.3: 0x486130 / 0x480F90).
static UTL::Std::list<IBody *, _type_CameraAIAvoidables> &CameraAIAvoidables() {
    static UTL::Std::list<IBody *, _type_CameraAIAvoidables> avoidables;
    return avoidables;
}

void CameraAI::AddAvoidable(IBody *body) {
    UTL::Std::list<IBody *, _type_CameraAIAvoidables> &list = CameraAIAvoidables();
    if (std::find(list.begin(), list.end(), body) == list.end()) {
        list.push_back(body);
    }
}

void CameraAI::RemoveAvoidable(IBody *body) {
    CameraAIAvoidables().remove(body);
}
#endif
