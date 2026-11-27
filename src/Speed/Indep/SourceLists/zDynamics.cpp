// TODO remove
#ifdef _MSC_VER
#pragma warning(disable : 4716)
#endif

#if defined(EA_PORT_NATIVE)
// Rigid-body dynamics helpers, reconstructed from speed.exe 1.3 for the
// native port (the matching builds leave these out).
#include "Speed/Indep/Src/Physics/Dynamics/Collision.cpp"

#include "Speed/Indep/Src/Physics/Dynamics/Articulation.cpp"

#include "Speed/Indep/Src/Physics/Dynamics/Geometry.cpp"
#endif
