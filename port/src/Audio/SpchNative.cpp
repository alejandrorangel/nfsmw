// Native stand-ins for the EA speech library (spch) entry points the game
// needs to get through Speech::Manager::Init. Speech itself (cop chatter,
// NIS dialogue) isn't ported; everything else in spch stays a link stub.
#include "spch/spch.h"

#include <cstdarg>
#include <cstdio>

namespace {

void SpchAbort(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[spch] abort: ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

int SpchPrint(const char *, ...) {
    return 0;
}

int SpchTicks() {
    return 0;
}

SPCHType_ExtVecs gSpchVecs = {SpchAbort, SpchPrint, SpchTicks};

} // namespace

// The game installs its own abort/tick vectors into this table after init.
SPCHType_ExtVecs *SPCH_GetExtVecs() {
    return &gSpchVecs;
}
