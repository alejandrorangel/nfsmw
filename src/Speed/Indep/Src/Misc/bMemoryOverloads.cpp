#include "Speed/Indep/bWare/Inc/bWare.hpp"
#include <cstddef>

#if defined(EA_PORT_NATIVE)
// In a native process the global operators are shared with AppKit, Metal and
// libc++, which allocate long before (and outside) the game's memory pools
// exist. Keep the system allocator for plain new/delete; game code that wants
// pool memory calls bMalloc explicitly. Only the game-specific overloads are
// defined here.
#include <cstdlib>

void *operator new(size_t size, int allocation_params) {
    return std::malloc(size);
}

void *operator new[](size_t size, int allocation_params) {
    return std::malloc(size);
}

void operator delete(void *ptr, int allocation_params) {
    std::free(ptr);
}

void operator delete(void *ptr, const char *file, int line) {
    std::free(ptr);
}

void operator delete[](void *ptr, const char *file, int line) {
    std::free(ptr);
}

void operator delete(void *ptr, const char *file, int line, int allocation_params) {
    std::free(ptr);
}

void operator delete[](void *ptr, const char *file, int line, int allocation_params) {
    std::free(ptr);
}

#else

void *operator new(size_t size) {
#ifdef MILESTONE_BUILD
    // TODO
    return bWareMalloc(size, nullptr, 0, 0);
#else
    return bMalloc(size, 0);
#endif
}

void *operator new[](size_t size) {
#ifdef MILESTONE_BUILD
    // TODO
    return bWareMalloc(size, nullptr, 0, 0);
#else
    return bMalloc(size, 0);
#endif
}

// STRIPPED
void *operator new(size_t size, int allocation_params) {}

// STRIPPED
void *operator new[](size_t size, int allocation_params) {}

void operator delete(void *ptr) {
    bFree(ptr);
}

void operator delete[](void *ptr) {
    bFree(ptr);
}

// STRIPPED
void operator delete(void *ptr, int allocation_params) {}

// STRIPPED
void operator delete(void *ptr, const char *file, int line) {}

// STRIPPED
void operator delete[](void *ptr, const char *file, int line) {}

// STRIPPED
void operator delete(void *ptr, const char *file, int line, int allocation_params) {}

// STRIPPED
void operator delete[](void *ptr, const char *file, int line, int allocation_params) {}

#endif // EA_PORT_NATIVE
