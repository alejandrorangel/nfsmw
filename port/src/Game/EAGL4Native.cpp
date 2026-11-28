// EAGL4 runtime for the native port: the dynamic loader that relocates the
// EAGL4 tool chain's ELF objects (animation banks, skeletons) in place, and
// the parts of EAGL4Anim the decomp has no code for. Reconstructed from
// speed.exe 1.3:
//
//   DynamicLoader  ctor 0x4A91A0  dtor 0x4A9250  Initialize 0x491660
//                  Resolve 0x4A2200  RunConstructors 0x4A20B0
//                  GetSymbol 0x491CA0  GetNextSymbol 0x491E90
//                  GetNextAddr 0x491F50  Release 0x4A2030  RunDestructors 0x4915E0
//   EAGL4Anim      Initializer::InitInternal 0x4A8600  __Init2 0x48D540
//                  Skeleton::GetStillPose 0x49FF90  PoseSQTToGlobal 0x49FCF0
//                  (SQT -> matrix 0x493FF0, the D3DXMatrixMultiply hook 0x4A05A0)
//
// The objects are MIPS ELF relocatables (the tools' format on every platform),
// little endian, one .data section; every relocation is R_MIPS_32 against the
// section symbol. On PC the loader writes absolute 32-bit addresses into the
// data; natively the slots are PORT_PTR32 (PortPtr32 encoding) and the data
// structs declare those fields PORT_PTR32, so every struct keeps its 32-bit
// layout. Section and symbol addresses live in a native side table instead of
// being written into the ELF headers as PC does.

#include "Speed/Indep/Src/EAGL4Anim/MemoryPoolManager.h"
#include "Speed/Indep/Src/EAGL4Anim/Skeleton.h"
#include "Speed/Indep/Src/EAGL4Anim/System.h"
#include "Speed/Indep/Src/EAGL4Anim/eagl4AnimBank.h"
#include "Speed/Indep/Src/EAGL4Anim/eagl4supportconspool.h"
#include "Speed/Indep/Src/EAGL4Anim/eagl4supportdlopen.h"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>


namespace EAGL4Internal {
extern MallocFunctionType gEAGL4ANIM_Malloc; // SetMallocOverride
extern FreeFunctionType gEAGL4ANIM_Free;
} // namespace EAGL4Internal

namespace {

bool TraceEAGL4() {
    static int sTrace = -1;
    if (sTrace < 0) {
        const char *env = std::getenv("NFSMW_TRACE_EAGL4");
        sTrace = env != nullptr && env[0] != '\0' && env[0] != '0';
    }
    return sTrace != 0;
}

// ELFSectionHeader with its 32-bit layout (the decomp's has pointer unions).
struct Elf32Section {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
};
static_assert(sizeof(Elf32Section) == 0x28, "ELF32 section header");
static_assert(sizeof(EAGL4::ELFHeader) == 0x34, "ELF32 header");
static_assert(sizeof(EAGL4::ELF32_Sym) == 0x10, "ELF32 symbol");

// The loader's state for one object (PC: HashPointer, 0x430 bytes).
struct NativeModule {
    char *data;
    unsigned int length;
    EAGL4::ELFHeader *header;
    Elf32Section *sections;
    std::vector<char *> sectionAddr; // PC writes these into sh_offset
    EAGL4::ELF32_Sym *symtab;
    int numSymbols;
    char *strtab;
    bool resolved;
};

// PC: the constructor pool (gConsPool, types registered by EAGL4Anim::Init).
struct NativeConstructor {
    Constructor construct;
    Destructor destruct;
};

std::map<std::string, NativeConstructor> &Constructors() {
    static std::map<std::string, NativeConstructor> *sTable = new std::map<std::string, NativeConstructor>;
    return *sTable;
}

void *NativeEAGL4Malloc(size_t size, const char *name) {
    return EAGL4Internal::gEAGL4ANIM_Malloc(size, name);
}

void NativeEAGL4Free(void *block, size_t size) {
    EAGL4Internal::gEAGL4ANIM_Free(block, size);
}

NativeModule *Module(void *handle) {
    return static_cast<NativeModule *>(handle);
}

} // namespace

namespace EAGL4 {

// PC 0x491660: maps the sections, renames the symbols "__<type>:::<name>" to
// "<name>\0\x7f<type>" in place (GetSymbol finds the type after the 0x7f) and
// builds the symbol hash (natively a linear search; the objects are small).
void DynamicLoader::Initialize(DynamicUserCallback pSearchFunction) {
    NativeModule *m = new NativeModule();
    m->data = mpData;
    m->length = mDataLen;
    m->header = reinterpret_cast<ELFHeader *>(mpData);
    m->sections = reinterpret_cast<Elf32Section *>(mpData + m->header->e_shoff);
    m->symtab = nullptr;
    m->numSymbols = 0;
    m->strtab = nullptr;
    m->resolved = false;
    int numSections = m->header->e_shnum;
    m->sectionAddr.resize(numSections, nullptr);
    for (int i = 0; i < numSections; i++) {
        unsigned int offset = m->sections[i].sh_offset;
        m->sectionAddr[i] = offset < mDataLen ? mpData + offset : nullptr;
    }
    const char *shstrtab = m->sectionAddr[m->header->e_shstrndx];
    for (int i = 0; i < numSections; i++) {
        Elf32Section &sh = m->sections[i];
        const char *name = shstrtab != nullptr ? shstrtab + sh.sh_name : "";
        if (sh.sh_type == SHT_SYMTAB && std::strcmp(name, ".symtab") == 0) {
            m->symtab = reinterpret_cast<ELF32_Sym *>(m->sectionAddr[i]);
            m->numSymbols = static_cast<int>(sh.sh_size / sizeof(ELF32_Sym));
        } else if (sh.sh_type == SHT_STRTAB && std::strcmp(name, ".strtab") == 0) {
            char *p = m->sectionAddr[i];
            m->strtab = p;
            for (int left = static_cast<int>(sh.sh_size); left > 0;) {
                int len = static_cast<int>(std::strlen(p));
                if (p[0] == '_' && p[1] == '_') {
                    char *sep = std::strstr(p + 2, ":::");
                    if (sep != nullptr) {
                        char type[128];
                        *sep = '\0';
                        std::snprintf(type, sizeof(type), "%s", p + 2);
                        const char *symbol = sep + 3;
                        size_t symLen = std::strlen(symbol) + 1;
                        std::memmove(p, symbol, symLen);
                        p[symLen] = 0x7F;
                        std::strcpy(p + symLen + 1, type);
                    }
                }
                p += len + 1;
                left -= len + 1;
            }
        }
    }
    mSearchCallback = pSearchFunction;
    handle = m;
    if (TraceEAGL4()) {
        std::fprintf(stderr, "[eagl4] object %p (%u bytes): %d sections, %d symbols\n", mpData, mDataLen, numSections, m->numSymbols);
        for (int i = 0; i < m->numSymbols; i++) {
            Symbol s = GetSymbol(i);
            std::fprintf(stderr, "[eagl4]   %s : %s\n", s.name, s.type);
        }
    }
}

// PC 0x4A2200. Only what the game's data uses: R_MIPS_32 against symbols
// defined in the object (PC also resolves undefined symbols from the other
// loaded objects, the search callback and the registered variables).
void DynamicLoader::Resolve() {
    NativeModule *m = Module(handle);
    if (m == nullptr || m->resolved) {
        return;
    }
    m->resolved = true;
    int numSections = m->header->e_shnum;
    for (int i = 0; i < numSections; i++) {
        Elf32Section &sh = m->sections[i];
        if (sh.sh_type != SHT_REL) {
            continue;
        }
        char *target = m->sectionAddr[sh.sh_info];
        ELF32_Sym *symtab = reinterpret_cast<ELF32_Sym *>(m->sectionAddr[sh.sh_link]);
        const ELF32_Rel *rel = reinterpret_cast<const ELF32_Rel *>(m->sectionAddr[i]);
        int count = static_cast<int>(sh.sh_size / sizeof(ELF32_Rel));
        for (int r = 0; r < count; r++) {
            unsigned int symIndex = rel[r].r_info >> 8;
            unsigned int type = rel[r].r_info & 0xFF;
            const ELF32_Sym &sym = symtab[symIndex];
            char *value = nullptr;
            if (sym.st_shndx != 0 && sym.st_shndx < numSections) {
                value = m->sectionAddr[sym.st_shndx] + sym.st_value;
            } else if (TraceEAGL4()) {
                std::fprintf(stderr, "[eagl4] unresolved symbol %s\n", m->strtab + sym.st_name);
            }
            uint32_t *slot = reinterpret_cast<uint32_t *>(target + rel[r].r_offset);
            if (type == R_MIPS_32) {
                *slot = PortPtr32Encode(value + *slot);
            } else if (TraceEAGL4()) {
                std::fprintf(stderr, "[eagl4] relocation type %u not supported\n", type);
            }
        }
    }
    mIsResolved = true;
    RunConstructors();
}

// PC 0x491CA0.
DynamicLoader::Symbol DynamicLoader::GetSymbol(int i) const {
    Symbol r;
    r.type = nullptr;
    r.isInternalRef = false;
    NativeModule *m = Module(handle);
    if (m == nullptr || i < 0 || i >= m->numSymbols) {
        return r;
    }
    const ELF32_Sym &s = m->symtab[i];
    const char *name = m->strtab + s.st_name;
    const char *end = name + std::strlen(name);
    r.name = name;
    r.type = end[1] == 0x7F ? end + 2 : end;
    r.isInternalRef = s.st_other != 2 && s.st_other != 3 && s.st_other != 4 && s.st_other != 5;
    if (s.st_other == 1) {
        r.data = reinterpret_cast<void *>(static_cast<uintptr_t>(s.st_value));
    } else if (s.st_shndx != 0 && s.st_shndx < m->header->e_shnum) {
        r.data = m->sectionAddr[s.st_shndx] + s.st_value;
    } else {
        r.data = nullptr;
    }
    return r;
}

int DynamicLoader::GetCount() const {
    NativeModule *m = Module(handle);
    return m != nullptr ? m->numSymbols : 0;
}

void *DynamicLoader::GetElfData() const {
    return mpData;
}

bool DynamicLoader::GetNextSymbol(const char *type, int &iIndex, Symbol &result) const {
    for (; iIndex < GetCount(); iIndex++) {
        Symbol s = GetSymbol(iIndex);
        if (std::strcmp(type, s.type) == 0) {
            result = s;
            iIndex++;
            return true;
        }
    }
    return false;
}

bool DynamicLoader::GetNextAddr(const char *type, int &iIndex, void *&addr) const {
    Symbol s;
    if (GetNextSymbol(type, iIndex, s)) {
        addr = s.data;
        return true;
    }
    return false;
}

// PC 0x4A2030.
void DynamicLoader::Release() {
    delete Module(handle);
    handle = nullptr;
    mIsResolved = false;
}

// PC 0x4A20B0: the registered constructor of each internal symbol's type.
void DynamicLoader::RunConstructors() {
    std::vector<DestructorEntry> entries;
    int n = GetCount();
    for (int i = 0; i < n; i++) {
        Symbol s = GetSymbol(i);
        if (!s.isInternalRef || std::strcmp(s.type, "RenderMethod") == 0) {
            continue;
        }
        auto it = Constructors().find(s.type);
        if (it == Constructors().end() || it->second.construct == nullptr) {
            continue;
        }
        it->second.construct(s.data, this, s.name);
        DestructorEntry entry;
        entry.d = it->second.destruct;
        entry.data = s.data;
        entries.push_back(entry);
    }
    nDestructors = static_cast<int>(entries.size());
    destructors = nullptr;
    if (!entries.empty()) {
        destructors = static_cast<DestructorEntry *>(EAGL4Internal::EAGL4Malloc(entries.size() * sizeof(DestructorEntry), "EAGL4::dynamic destructor list"));
        std::memcpy(destructors, entries.data(), entries.size() * sizeof(DestructorEntry));
    }
}

// PC 0x4915E0.
void DynamicLoader::RunDestructors() {
    if (destructors != nullptr) {
        for (int i = nDestructors - 1; i >= 0; i--) {
            if (destructors[i].d != nullptr) {
                destructors[i].d(destructors[i].data);
            }
        }
        EAGL4Internal::EAGL4Free(destructors, nDestructors * sizeof(DestructorEntry));
        destructors = nullptr;
    }
    nDestructors = 0;
}

DynamicLoader::DynamicLoader(void *d, unsigned int len, DynamicUserCallback pSearchFunction)
    : handle(nullptr),                  //
      nDestructors(0),                  //
      destructors(nullptr),             //
      RuntimeAllocDestructors(nullptr), //
      mpData(static_cast<char *>(d)),   //
      mDataLen(len),                    //
      mpReloc(nullptr),                 //
      mSearchCallback(pSearchFunction), //
      mNumPatchAddresses(0),            //
      mMaxPatchAddresses(0),            //
      mpPatchAddresses32(nullptr),      //
      mIsResolved(false) {
    Initialize(pSearchFunction);
    Resolve();
}

DynamicLoader::~DynamicLoader() {
    Release();
    RunDestructors();
}

ConstructorPool::~ConstructorPool() {}

RuntimeAllocConstructorPool::~RuntimeAllocConstructorPool() {}

} // namespace EAGL4

namespace EAGL4Anim {

// PC 0x4A8600 (with __Init2 0x48D540): the memory pool manager and its pool,
// and the AnimationBank constructor (which initialises each anim's data).
void Initializer::InitInternal(size_t memorySize, bool enableStats) {
    if (EAGL4Internal::EAGL4Malloc == nullptr) {
        EAGL4Internal::EAGL4Malloc = NativeEAGL4Malloc;
        EAGL4Internal::EAGL4Free = NativeEAGL4Free;
    }
    static MemoryPoolManager sManager;
    MemoryPoolManager::SetMemoryPoolManager(sManager);
    sManager.InitAux(static_cast<unsigned int>(memorySize));
    NativeConstructor &bank = Constructors()[EAGL4::DynamicLoader::AnimBankType];
    bank.construct = AnimBank::Constructor;
    bank.destruct = AnimBank::Destructor;
}

// PC 0x49FF90. The pose is 12 floats per bone: S xyz, the inverse bone
// scale, Q xyzw, T xyz, 1.
void Skeleton::GetStillPose(float *pose, const BoneMask *mask) const {
    const float *invScales = GetInvBoneScales();
    int numBones = GetNumBones();
    for (int i = 0; i < numBones; i++, pose += 12) {
        if (mask != nullptr && !mask->GetBone(i)) {
            continue;
        }
        const BoneData &b = GetBoneData(i);
        pose[0] = b.mS.x;
        pose[1] = b.mS.y;
        pose[2] = b.mS.z;
        pose[3] = invScales != nullptr ? invScales[i] : 1.0f;
        pose[4] = b.mQ.x;
        pose[5] = b.mQ.y;
        pose[6] = b.mQ.z;
        pose[7] = b.mQ.w;
        pose[8] = b.mT.x;
        pose[9] = b.mT.y;
        pose[10] = b.mT.z;
        pose[11] = 1.0f;
    }
}

// PC 0x49FCF0: each bone's SQT as a matrix (0x493FF0), its first column
// scaled by the pose's inverse scale, times its parent's global matrix.
void Skeleton::PoseSQTToGlobal(float *pose, EAGL4::Transform *output, BoneMask *mask) {
    float *out = reinterpret_cast<float *>(output);
    int numBones = GetNumBones();
    for (int i = 0; i < numBones; i++, pose += 12, out += 16) {
        if (mask != nullptr && !mask->GetBone(i)) {
            continue;
        }
        float sx = pose[0], sy = pose[1], sz = pose[2];
        float qx = pose[4], qy = pose[5], qz = pose[6], qw = pose[7];
        float y2 = qy + qy;
        float z2 = qz + qz;
        float xw2 = (qx + qx) * qw;
        float xx2 = (qx + qx) * qx;
        out[3] = 0.0f;
        out[7] = 0.0f;
        out[0] = (1.0f - (z2 * qz + y2 * qy)) * sx;
        out[4] = (qx * y2 - z2 * qw) * sy;
        out[8] = (z2 * qx + qw * y2) * sz;
        out[1] = (qx * y2 + z2 * qw) * sx;
        out[5] = (1.0f - (z2 * qz + xx2)) * sy;
        out[9] = (z2 * qy - xw2) * sz;
        out[2] = (z2 * qx - qw * y2) * sx;
        out[6] = (z2 * qy + xw2) * sy;
        out[10] = (1.0f - (y2 * qy + xx2)) * sz;
        out[11] = 0.0f;
        out[12] = pose[8];
        out[13] = pose[9];
        out[14] = pose[10];
        out[15] = 1.0f;
        out[0] = pose[3] * out[0];
        out[4] = out[4] * pose[3];
        out[8] = pose[3] * out[8];
        int parent = GetBoneData(i).mParentIdx;
        if (parent >= 0) {
            // D3DXMatrixMultiply(out, local, parent): local * parent.
            eMulMatrix(reinterpret_cast<bMatrix4 *>(out), reinterpret_cast<bMatrix4 *>(out), reinterpret_cast<bMatrix4 *>(output) + parent);
        }
    }
}

} // namespace EAGL4Anim
