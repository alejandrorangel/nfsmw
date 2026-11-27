// Attrib runtime pieces missing from the decomp (they were link stubs):
//  - DefaultDataArea: zeroed, read-only storage returned for attribute fields a
//    collection doesn't define, and the default layout of attribute instances.
//  - TypeDesc::NameToType (0x626C90: a jump to StringToKey). As a stub every
//    type hashed to 0, so the database's type table held a single entry.
//  - TypeDesc::Lookup and the RefSpec type handler (speed.exe 1.3 0x626C50,
//    handler vtable 0x8A7994). Without the handler, Collection::Clean never
//    dropped the collection references RefSpecs cache, so unloading a vault
//    (UnloadFrontEndVault at boot) left its collections alive, pointing at
//    freed layout memory, and the reloaded vault's collections were skipped.

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

namespace {

// speed.exe: Retain returns the object; Clone copies it; Clean and Release both
// release the cached collection pointer (RefSpec::Clean).
class RefSpecTypeHandler : public ITypeHandler {
  public:
    void *Retain(void *obj) override {
        return obj;
    }
    void *Clone(void *obj) override {
        return obj ? new RefSpec(*static_cast<RefSpec *>(obj)) : nullptr;
    }
    void Clean(void *obj) override {
        static_cast<RefSpec *>(obj)->Clean();
    }
    void Release(void *obj) override {
        static_cast<RefSpec *>(obj)->Clean();
    }
};

} // namespace

Type TypeDesc::NameToType(const char *name) {
    return StringToKey(name);
}

// The PC table has seven handlers; only Attrib::RefSpec (0x2B936EB7) is a type
// the game's vaults use, so it is the only one reconstructed.
ITypeHandler *TypeDesc::Lookup(Type t) {
    static RefSpecTypeHandler sRefSpecHandler;
    if (t == 0x2B936EB7) {
        return &sRefSpecHandler;
    }
    return nullptr;
}

} // namespace Attrib
