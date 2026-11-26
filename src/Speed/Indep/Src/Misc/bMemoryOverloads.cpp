#include "Speed/Indep/bWare/Inc/bWare.hpp"
#include <cstddef>

#if defined(EA_PORT_NATIVE)
// In a native process the global operators are shared with AppKit, Metal and
// libc++, which allocate long before (and outside) the game's memory pools
// exist. Keep the system allocator for plain new/delete; game code that wants
// pool memory calls bMalloc explicitly. Only the game-specific overloads are
// defined here.
#include <cstdlib>

bool PortIsGameHeapPointer(const void *ptr);

static inline void PortFree(void *ptr) {
    if (PortIsGameHeapPointer(ptr)) {
        bFree(ptr);
    } else {
        std::free(ptr);
    }
}

// Plain new stays on the system heap (AppKit/libc++ share it), but delete must
// also accept game-pool memory, e.g. FNEW'd FEng strings released with delete[].
void operator delete(void *ptr) noexcept {
    PortFree(ptr);
}

void operator delete[](void *ptr) noexcept {
    PortFree(ptr);
}

void operator delete(void *ptr, size_t) noexcept {
    PortFree(ptr);
}

void operator delete[](void *ptr, size_t) noexcept {
    PortFree(ptr);
}

void *operator new(size_t size, int allocation_params) {
    return std::malloc(size);
}

void *operator new[](size_t size, int allocation_params) {
    return std::malloc(size);
}

void operator delete(void *ptr, int allocation_params) {
    PortFree(ptr);
}

void operator delete(void *ptr, const char *file, int line) {
    PortFree(ptr);
}

void operator delete[](void *ptr, const char *file, int line) {
    PortFree(ptr);
}

void operator delete(void *ptr, const char *file, int line, int allocation_params) {
    PortFree(ptr);
}

void operator delete[](void *ptr, const char *file, int line, int allocation_params) {
    PortFree(ptr);
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
