#include "CameraAI.hpp"

#if !defined(EA_PORT_NATIVE)
void CameraAI::Reset() {
    return;
}
#endif
// The native port defines CameraAI (director, actions, avoidables) in
// port/src/Game/CameraDriveNative.cpp.
