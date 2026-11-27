// Attrib::DefaultDataArea: zeroed, read-only storage returned for attribute
// fields a collection doesn't define, and used as the default layout of
// attribute instances. (A link stub returning null before.)

#include "Speed/Indep/Tools/AttribSys/Runtime/AttribSys.h"

#include <cstdint>
#include <cstdio>

namespace Attrib {

const void *DefaultDataArea(std::size_t bytes) {
    alignas(16) static const std::uint8_t zeros[16384] = {};
    if (bytes > sizeof(zeros)) {
        std::fprintf(stderr, "[attrib] default data area too small for %zu bytes\n", bytes);
        return nullptr;
    }
    return zeros;
}

} // namespace Attrib
