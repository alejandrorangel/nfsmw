// Native Csis runtime: the interface broker between the game's sound code
// and the AEMS module banks (EA "Csis" 2.x). Reconstructed from speed.exe 1.3
// (0x830E30-0x831ACF); the decompiled library source (csis.cpp) is empty.
//
// A Csis "system" is an interface table produced by the sound tools: the
// SOUND/EVT_SYS/*.csi files ("MOIR"), which the game loads and passes to
// System::Subscribe. Each lists functions, classes and global variables by
// name and CRC. Handles resolve against the subscribed systems; clients
// (the AEMS module banks, see SndNative.cpp) subscribe to class
// constructors, member data, destructors, function calls and variables.
//
// On PC the system data is fixed up in place (32-bit pointers). Natively the
// interface records live beside it, so the data itself is only read.
#include "csis/csis.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace Csis {

namespace {

// Every client struct starts with `void *reserved[2]`: the list links
// (next, prev), as in the PC library's intrusive lists (0x81962E/0x819648).
struct Link {
    Link *next;
    Link *prev;
};

void ListPush(void **head, void *client) {
    Link *node = static_cast<Link *>(client);
    node->next = static_cast<Link *>(*head);
    node->prev = nullptr;
    if (node->next != nullptr) {
        node->next->prev = node;
    }
    *head = node;
}

void ListRemove(void **head, void *client) {
    Link *node = static_cast<Link *>(client);
    if (node == *head) {
        *head = node->next;
    }
    if (node->prev != nullptr) {
        node->prev->next = node->next;
    }
    if (node->next != nullptr) {
        node->next->prev = node->prev;
    }
    node->next = node->prev = nullptr;
}

// One function, class or global variable of a subscribed system. `key` is
// the word the PC library keeps at entry+8: interface CRC in the low half,
// a unique serial in the high half (-1 once the system is unsubscribed).
// Handles copy it and are valid while it still matches.
struct Interface {
    const char *name = nullptr;
    int16_t crc = 0;
    int key = -1;
    void *clients = nullptr; // constructor / call / variable clients
    int value = 0;           // global variables only
};

enum Kind { KIND_FUNCTION, KIND_CLASS, KIND_GLOBAL, KIND_COUNT };

struct SystemRec {
    const uint8_t *data = nullptr;
    int16_t systemCrc = 0;
    std::vector<Interface *> interfaces[KIND_COUNT];
};

// A class instance (PC: 16 bytes -- class entry, refcount, member data
// clients, destructor clients).
struct Instance {
    Interface *cls;
    int refCount;
    void *memberDataClients;
    void *destructorClients;
};

struct HandleRep {
    Interface *entry;
    int key;
};

std::recursive_mutex &Mutex() {
    static std::recursive_mutex m;
    return m;
}

EA::Allocator::ICoreAllocator *gCoreAllocator = nullptr;
EA::Allocator::IAllocator *gAllocator = nullptr;
bool gInited = false;
std::vector<SystemRec *> gSystems; // front = most recently subscribed
int16_t gSerial = 0;

int16_t NextSerial() { // 0x830F27
    gSerial++;
    if (gSerial < 0) {
        gSerial = 1;
    }
    return gSerial;
}

uint16_t ReadU16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t ReadU32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Handle lookup (0x83123C/0x83132B/0x83141A): first the systems with the
// id's system CRC, then any system.
Result Resolve(HandleRep *handle, const InterfaceId *id, Kind kind) {
    for (int pass = 0; pass < 2; pass++) {
        for (SystemRec *sys : gSystems) {
            if (pass == 0 && sys->systemCrc != id->systemCrc) {
                continue;
            }
            for (Interface *it : sys->interfaces[kind]) {
                if (it->crc == id->interfaceCrc && std::strcmp(it->name, id->pString) == 0) {
                    handle->entry = it;
                    handle->key = it->key;
                    return RESULT_OK;
                }
            }
        }
    }
    return RESULT_ERR_NOTFOUND;
}

// 0x831304 / Valid__ClassHandle / 0x8314E4
Result Check(HandleRep *handle) {
    int key = handle->key;
    if (key < 0) {
        return static_cast<Result>(key);
    }
    if (handle->entry == nullptr) {
        return RESULT_ERR_UNINITIALIZED;
    }
    if (key != handle->entry->key) {
        handle->entry = nullptr;
        handle->key = RESULT_ERR_HANDLEEXPIRED;
        return RESULT_ERR_HANDLEEXPIRED;
    }
    return RESULT_OK;
}

HandleRep *Rep(void *handle) {
    return static_cast<HandleRep *>(handle);
}

void *CoreAlloc(int size) { // AllocFast (0x830FBA)
    if (gCoreAllocator != nullptr) {
        return gCoreAllocator->Alloc(size, "CsisAlloc", 1);
    }
    return std::calloc(1, size);
}

void CoreFree(void *p) { // FreeFast (0x830FD1)
    if (gCoreAllocator != nullptr) {
        gCoreAllocator->Free(p, 0);
    } else {
        std::free(p);
    }
}

void InstanceDecRef(Instance *inst) { // 0x830EAB
    if (--inst->refCount == 0) {
        CoreFree(inst);
    }
}

} // namespace

// --- System ---------------------------------------------------------------

Result System::SetAllocator(EA::Allocator::ICoreAllocator *pAllocator) {
    if (gInited) {
        return RESULT_ERR_ILLEGALOP;
    }
    gCoreAllocator = pAllocator;
    return RESULT_OK;
}

Result System::SetAllocator(EA::Allocator::IAllocator *pAllocator) {
    if (gInited) {
        return RESULT_ERR_ILLEGALOP;
    }
    gAllocator = pAllocator;
    return RESULT_OK;
}

Result System::GetAllocator(EA::Allocator::IAllocator **ppAllocator) {
    *ppAllocator = gAllocator;
    return RESULT_OK;
}

Result System::SetAbortMessage(void (*)(const char *)) {
    return RESULT_OK;
}

Result System::Init() {
    gSystems.clear();
    gInited = true;
    return RESULT_OK;
}

Result System::Restore() {
    gSystems.clear();
    gInited = false;
    return RESULT_OK;
}

bool System::IsInited() {
    return gInited;
}

// Snd shares this mutex (Snd::System::VectorToCsisMutex), see SndNative.cpp.
Result System::Lock() {
    Mutex().lock();
    return RESULT_OK;
}

Result System::Unlock() {
    Mutex().unlock();
    return RESULT_OK;
}

bool System::IsLocked() {
    if (Mutex().try_lock()) {
        Mutex().unlock();
        return false;
    }
    return true;
}

// 0x83104C. Layout of a system ("MOIR" .csi): u16 counts at +0xA functions,
// +0xC classes, +0xE globals; s16 system CRC at +0x10; the entries start at
// +0x28 -- functions and classes 12 bytes {clients, name offset, s16 crc,
// s16 serial}, globals 16 bytes {clients, value, name offset, crc, serial}.
Result System::Subscribe(void *pinterface) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    const uint8_t *data = static_cast<const uint8_t *>(pinterface);
    for (SystemRec *sys : gSystems) {
        if (sys->data == data) {
            return RESULT_OK;
        }
    }
    SystemRec *sys = new SystemRec;
    sys->data = data;
    sys->systemCrc = static_cast<int16_t>(ReadU16(data + 0x10));
    int counts[KIND_COUNT] = {ReadU16(data + 0xA), ReadU16(data + 0xC), ReadU16(data + 0xE)};
    const uint8_t *entry = data + 0x28;
    for (int kind = 0; kind < KIND_COUNT; kind++) {
        for (int i = 0; i < counts[kind]; i++) {
            Interface *it = new Interface;
            int nameAt = kind == KIND_GLOBAL ? 8 : 4;
            it->name = reinterpret_cast<const char *>(data + ReadU32(entry + nameAt));
            it->crc = static_cast<int16_t>(ReadU16(entry + nameAt + 4));
            it->key = static_cast<uint16_t>(it->crc) | (NextSerial() << 16);
            if (kind == KIND_GLOBAL) {
                it->value = static_cast<int>(ReadU32(entry + 4));
            }
            sys->interfaces[kind].push_back(it);
            entry += kind == KIND_GLOBAL ? 16 : 12;
        }
    }
    gSystems.insert(gSystems.begin(), sys);
    if (std::getenv("NFSMW_AUDIO_LOG") != nullptr) {
        std::fprintf(stderr, "[csis] subscribe system 0x%04x: %d functions, %d classes, %d globals\n",
                     static_cast<uint16_t>(sys->systemCrc), counts[0], counts[1], counts[2]);
    }
    return RESULT_OK;
}

// 0x831109: handles to the system's interfaces expire. The records stay
// allocated, since handles may still point at them.
Result System::Unsubscribe(void *pinterface) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    for (size_t i = 0; i < gSystems.size(); i++) {
        if (gSystems[i]->data == pinterface) {
            for (auto &list : gSystems[i]->interfaces) {
                for (Interface *it : list) {
                    it->key = -1;
                }
            }
            gSystems.erase(gSystems.begin() + i);
            break;
        }
    }
    return RESULT_OK;
}

unsigned char System::GetLibraryType() {
    return 0;
}

void *System::AllocFast(int size) {
    return CoreAlloc(size);
}

void System::FreeFast(void *pMem) {
    CoreFree(pMem);
}

void *System::Alloc(int size) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return AllocFast(size);
}

void System::Free(void *pMem) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    FreeFast(pMem);
}

// --- Handles ----------------------------------------------------------------

FunctionHandle::FunctionHandle() : mpPrivate(nullptr), mKey(RESULT_ERR_HANDLEEXPIRED) {}

Result FunctionHandle::SetFast(const InterfaceId *pInterfaceId) {
    return Resolve(Rep(this), pInterfaceId, KIND_FUNCTION);
}

Result FunctionHandle::Set(const InterfaceId *pInterfaceId) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return SetFast(pInterfaceId);
}

Result FunctionHandle::Valid() {
    return Check(Rep(this));
}

void *FunctionHandle::operator new(size_t size) {
    return System::Alloc(static_cast<int>(size));
}

void FunctionHandle::operator delete(void *ptr) {
    System::Free(ptr);
}

ClassHandle::ClassHandle() : mpPrivate(nullptr), mKey(RESULT_ERR_HANDLEEXPIRED) {}

Result ClassHandle::SetFast(const InterfaceId *pInterfaceId) {
    return Resolve(Rep(this), pInterfaceId, KIND_CLASS);
}

Result ClassHandle::Set(const InterfaceId *pInterfaceId) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return SetFast(pInterfaceId);
}

Result ClassHandle::Valid() {
    return Check(Rep(this));
}

void *ClassHandle::operator new(size_t size) {
    return System::Alloc(static_cast<int>(size));
}

void ClassHandle::operator delete(void *ptr) {
    System::Free(ptr);
}

GlobalVariableHandle::GlobalVariableHandle() : mpPrivate(nullptr), mKey(RESULT_ERR_HANDLEEXPIRED) {}

Result GlobalVariableHandle::SetFast(const InterfaceId *pInterfaceId) {
    return Resolve(Rep(this), pInterfaceId, KIND_GLOBAL);
}

Result GlobalVariableHandle::Set(const InterfaceId *pInterfaceId) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return SetFast(pInterfaceId);
}

Result GlobalVariableHandle::Valid() {
    return Check(Rep(this));
}

void *GlobalVariableHandle::operator new(size_t size) {
    return System::Alloc(static_cast<int>(size));
}

void GlobalVariableHandle::operator delete(void *ptr) {
    System::Free(ptr);
}

// --- Functions ----------------------------------------------------------------

// 0x831696: every subscriber gets the parameter block.
Result Function::CallFast(FunctionHandle *pFunctionHandle, void *pParameters) {
    HandleRep *h = Rep(pFunctionHandle);
    Result r = Check(h);
    if (r < RESULT_OK) {
        return r;
    }
    Link *node = static_cast<Link *>(h->entry->clients);
    if (node == nullptr) {
        return RESULT_ERR_UNUSED;
    }
    while (node != nullptr) {
        Link *next = node->next;
        FunctionClient *client = reinterpret_cast<FunctionClient *>(node);
        client->pClientFunc(static_cast<Parameter *>(pParameters), client->pClientData);
        node = next;
    }
    return RESULT_OK;
}

Result Function::Call(FunctionHandle *pFunctionHandle, void *pParameters) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return CallFast(pFunctionHandle, pParameters);
}

Result Function::SubscribeFast(FunctionHandle *pFunctionHandle, FunctionClient *pFunctionClient) { // 0x8316D0
    HandleRep *h = Rep(pFunctionHandle);
    Result r = Check(h);
    if (r < RESULT_OK) {
        return r;
    }
    ListPush(&h->entry->clients, pFunctionClient);
    return RESULT_OK;
}

Result Function::Subscribe(FunctionHandle *pFunctionHandle, FunctionClient *pFunctionClient) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return SubscribeFast(pFunctionHandle, pFunctionClient);
}

Result Function::UnsubscribeFast(FunctionHandle *pFunctionHandle, FunctionClient *pFunctionClient) { // 0x8316F2
    HandleRep *h = Rep(pFunctionHandle);
    Result r = Check(h);
    if (r < RESULT_OK) {
        return r;
    }
    ListRemove(&h->entry->clients, pFunctionClient);
    return RESULT_OK;
}

Result Function::Unsubscribe(FunctionHandle *pFunctionHandle, FunctionClient *pFunctionClient) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return UnsubscribeFast(pFunctionHandle, pFunctionClient);
}

// --- Global variables -------------------------------------------------------

// 0x831880: subscribers hear about changes only.
Result GlobalVariable::SetFast(GlobalVariableHandle *pHandle, void *pGlobalVariable) {
    HandleRep *h = Rep(pHandle);
    Result r = Check(h);
    if (r < RESULT_OK) {
        return r;
    }
    int value = *static_cast<int *>(pGlobalVariable);
    if (value == h->entry->value) {
        return RESULT_OK;
    }
    h->entry->value = value;
    for (Link *node = static_cast<Link *>(h->entry->clients); node != nullptr;) {
        Link *next = node->next;
        GlobalVariableClient *client = reinterpret_cast<GlobalVariableClient *>(node);
        client->pClientFunc(reinterpret_cast<Parameter *>(&h->entry->value), client->pClientData);
        node = next;
    }
    return RESULT_OK;
}

Result GlobalVariable::Set(GlobalVariableHandle *pHandle, void *pGlobalVariable) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return SetFast(pHandle, pGlobalVariable);
}

// 0x8318C7: a new subscriber gets the current value right away.
Result GlobalVariable::SubscribeFast(GlobalVariableHandle *pHandle, GlobalVariableClient *pClient) {
    HandleRep *h = Rep(pHandle);
    Result r = Check(h);
    if (r < RESULT_OK) {
        return r;
    }
    ListPush(&h->entry->clients, pClient);
    pClient->pClientFunc(reinterpret_cast<Parameter *>(&h->entry->value), pClient->pClientData);
    return RESULT_OK;
}

Result GlobalVariable::Subscribe(GlobalVariableHandle *pHandle, GlobalVariableClient *pClient) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return SubscribeFast(pHandle, pClient);
}

Result GlobalVariable::UnsubscribeFast(GlobalVariableHandle *pHandle, GlobalVariableClient *pClient) {
    HandleRep *h = Rep(pHandle);
    Result r = Check(h);
    if (r < RESULT_OK) {
        return r;
    }
    ListRemove(&h->entry->clients, pClient);
    return RESULT_OK;
}

Result GlobalVariable::Unsubscribe(GlobalVariableHandle *pHandle, GlobalVariableClient *pClient) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return UnsubscribeFast(pHandle, pClient);
}

// --- Classes ------------------------------------------------------------------

// 0x831714: the constructor subscribers attach to the new instance (they
// typically subscribe to its member data and destructor), then everything
// subscribed to the member data receives the initial values.
Result Class::CreateInstanceFast(ClassHandle *pClassHandle, void *pMemberData, Class **ppClass) {
    *ppClass = nullptr;
    HandleRep *h = Rep(pClassHandle);
    Result r = Check(h);
    if (r < RESULT_OK) {
        return r;
    }
    Instance *inst = static_cast<Instance *>(CoreAlloc(sizeof(Instance)));
    if (inst == nullptr) {
        return RESULT_ERR_MEM;
    }
    inst->cls = h->entry;
    inst->refCount = 1;
    inst->memberDataClients = nullptr;
    inst->destructorClients = nullptr;
    Class *cls = reinterpret_cast<Class *>(inst);
    for (Link *node = static_cast<Link *>(h->entry->clients); node != nullptr;) {
        Link *next = node->next;
        ClassConstructorClient *client = reinterpret_cast<ClassConstructorClient *>(node);
        client->pClientFunc(cls, static_cast<Parameter *>(pMemberData), client->pClientData);
        node = next;
    }
    cls->SetMemberDataFast(pMemberData);
    *ppClass = cls;
    return RESULT_OK;
}

Result Class::CreateInstance(ClassHandle *pClassHandle, void *pMemberData, Class **ppClass) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return CreateInstanceFast(pClassHandle, pMemberData, ppClass);
}

// 0x831182: destructor subscribers are told, then the creator's reference
// goes. Subscribers keep the instance alive until they unsubscribe.
Result Class::ReleaseFast() {
    Instance *inst = reinterpret_cast<Instance *>(this);
    for (Link *node = static_cast<Link *>(inst->destructorClients); node != nullptr;) {
        Link *next = node->next;
        ClassDestructorClient *client = reinterpret_cast<ClassDestructorClient *>(node);
        client->pClientFunc(this, client->pClientData);
        node = next;
    }
    InstanceDecRef(inst);
    return RESULT_OK;
}

Result Class::Release() {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return ReleaseFast();
}

// 0x830F0B
Result Class::SetMemberDataFast(void *pMemberData) {
    Instance *inst = reinterpret_cast<Instance *>(this);
    for (Link *node = static_cast<Link *>(inst->memberDataClients); node != nullptr;) {
        Link *next = node->next;
        MemberDataClient *client = reinterpret_cast<MemberDataClient *>(node);
        client->pClientFunc(static_cast<Parameter *>(pMemberData), client->pClientData);
        node = next;
    }
    return RESULT_OK;
}

Result Class::SetMemberData(void *pMemberData) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return SetMemberDataFast(pMemberData);
}

Result Class::GetRefCount(int *pRefCount) {
    *pRefCount = reinterpret_cast<Instance *>(this)->refCount;
    return RESULT_OK;
}

Result Class::AddRef() {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    reinterpret_cast<Instance *>(this)->refCount++;
    return RESULT_OK;
}

Result Class::DecRef() {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    InstanceDecRef(reinterpret_cast<Instance *>(this));
    return RESULT_OK;
}

Result Class::SubscribeConstructorFast(ClassHandle *pClassHandle, ClassConstructorClient *pClient) { // 0x8317BC
    HandleRep *h = Rep(pClassHandle);
    Result r = Check(h);
    if (r < RESULT_OK) {
        return r;
    }
    ListPush(&h->entry->clients, pClient);
    return RESULT_OK;
}

Result Class::SubscribeConstructor(ClassHandle *pClassHandle, ClassConstructorClient *pClient) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return SubscribeConstructorFast(pClassHandle, pClient);
}

Result Class::UnsubscribeConstructorFast(ClassHandle *pClassHandle, ClassConstructorClient *pClient) { // 0x8317DE
    HandleRep *h = Rep(pClassHandle);
    Result r = Check(h);
    if (r < RESULT_OK) {
        return r;
    }
    ListRemove(&h->entry->clients, pClient);
    return RESULT_OK;
}

Result Class::UnsubscribeConstructor(ClassHandle *pClassHandle, ClassConstructorClient *pClient) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return UnsubscribeConstructorFast(pClassHandle, pClient);
}

// 0x8311D4/0x8311EC: each subscription holds a reference on the instance.
Result Class::SubscribeDestructorFast(ClassDestructorClient *pClient) {
    Instance *inst = reinterpret_cast<Instance *>(this);
    ListPush(&inst->destructorClients, pClient);
    inst->refCount++;
    return RESULT_OK;
}

Result Class::SubscribeDestructor(ClassDestructorClient *pClient) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return SubscribeDestructorFast(pClient);
}

Result Class::UnsubscribeDestructorFast(ClassDestructorClient *pClient) {
    Instance *inst = reinterpret_cast<Instance *>(this);
    ListRemove(&inst->destructorClients, pClient);
    InstanceDecRef(inst);
    return RESULT_OK;
}

Result Class::UnsubscribeDestructor(ClassDestructorClient *pClient) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return UnsubscribeDestructorFast(pClient);
}

Result Class::SubscribeMemberDataFast(MemberDataClient *pClient) { // 0x831208
    Instance *inst = reinterpret_cast<Instance *>(this);
    ListPush(&inst->memberDataClients, pClient);
    inst->refCount++;
    return RESULT_OK;
}

Result Class::SubscribeMemberData(MemberDataClient *pClient) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return SubscribeMemberDataFast(pClient);
}

Result Class::UnsubscribeMemberDataFast(MemberDataClient *pClient) { // 0x831220
    Instance *inst = reinterpret_cast<Instance *>(this);
    ListRemove(&inst->memberDataClients, pClient);
    InstanceDecRef(inst);
    return RESULT_OK;
}

Result Class::UnsubscribeMemberData(MemberDataClient *pClient) {
    std::lock_guard<std::recursive_mutex> lock(Mutex());
    return UnsubscribeMemberDataFast(pClient);
}

} // namespace Csis

// The shared Csis/Snd lock, for SndNative.cpp.
void CsisNativeLock() {
    Csis::System::Lock();
}

void CsisNativeUnlock() {
    Csis::System::Unlock();
}
