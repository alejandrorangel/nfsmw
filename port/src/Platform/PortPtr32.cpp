// Encoding for PortPtr32 (see port/include/PortPtr32.h).

#include "PortPtr32.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

// From bMemory.cpp: the main pool mapping that every bWare pool lives in.
void PortGetGameHeapRange(uintptr_t *start, uintptr_t *end);

namespace {

const uint32_t kInternTag = 0x80000000u;

// Function-local so they exist even when game static initialisers encode
// pointers before this file's globals would have been constructed.
struct InternTable {
    std::mutex lock;
    std::vector<const void *> pointers; // index -> pointer
    std::unordered_map<const void *, uint32_t> index;
};

InternTable &Interns() {
    static InternTable *table = new InternTable; // never destroyed: used until exit
    return *table;
}

// Heap range, fixed once the main pool exists. Pointers encoded before that
// (static initialisers) go through the intern table and stay valid.
bool HeapRange(uintptr_t &base, uintptr_t &end) {
    static uintptr_t s_base, s_end;
    if (s_base == 0) {
        uintptr_t start, stop;
        PortGetGameHeapRange(&start, &stop);
        if (start == 0) {
            return false;
        }
        s_base = start - 16; // so no heap address encodes to 0
        s_end = stop;
    }
    base = s_base;
    end = s_end;
    return true;
}

} // namespace

uint32_t PortPtr32Encode(const void *p) {
    if (p == nullptr) {
        return 0;
    }
    uintptr_t base, end;
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    if (HeapRange(base, end) && addr > base && addr < end && addr - base < kInternTag) {
        return static_cast<uint32_t>(addr - base);
    }
    InternTable &t = Interns();
    std::lock_guard<std::mutex> guard(t.lock);
    auto it = t.index.find(p);
    if (it != t.index.end()) {
        return it->second;
    }
    uint32_t bits = kInternTag | static_cast<uint32_t>(t.pointers.size());
    t.pointers.push_back(p);
    t.index.emplace(p, bits);
    return bits;
}

void *PortPtr32Decode(uint32_t bits) {
    if (bits == 0) {
        return nullptr;
    }
    if (bits & kInternTag) {
        InternTable &t = Interns();
        std::lock_guard<std::mutex> guard(t.lock);
        uint32_t index = bits & ~kInternTag;
        if (index >= t.pointers.size()) {
            std::fprintf(stderr, "[ptr32] bad intern index %#x\n", bits);
            std::abort();
        }
        return const_cast<void *>(t.pointers[index]);
    }
    uintptr_t base, end;
    if (!HeapRange(base, end)) {
        std::fprintf(stderr, "[ptr32] heap offset %#x decoded before the heap exists\n", bits);
        std::abort();
    }
    return reinterpret_cast<void *>(base + bits);
}
