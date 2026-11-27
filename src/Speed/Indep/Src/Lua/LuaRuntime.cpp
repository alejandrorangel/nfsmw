#include "Speed/Indep/Src/Lua/LuaRuntime.h"

#include "Speed/Indep/Libs/Support/Miscellaneous/StringHash.h"
#include "Speed/Indep/Src/Main/Event.h"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// lmem.h (kept out of here: the VM's internal headers define macros that
// clash with game code). The allocation type is an enum (int) there.
typedef int LuaAllocType;
extern "C" void luaM_setallocator(void *(*reallocFunc)(lua_State *, void *, unsigned int, unsigned int, LuaAllocType),
                                  void (*freeFunc)(lua_State *, void *, unsigned int, LuaAllocType));

LuaRuntime *LuaRuntime::mObj = nullptr;

namespace {

#if defined(EA_PORT_NATIVE)
// PC: 0x61E520 / 0x61E550 (LuaRuntime::Realloc / Free) carve the VM's memory
// out of LuaRuntime's pool (small block chains, a large free block map and
// emergency allocations when it runs out). Natively the system heap does it.
// TODO(port): the pool (and with it the heap snapshot/reset) isn't ported.
void *LuaRealloc(lua_State *L, void *block, unsigned int oldsize, unsigned int size, LuaAllocType type) {
    return std::realloc(block, size);
}

void LuaFree(lua_State *L, void *block, unsigned int oldsize, LuaAllocType type) {
    std::free(block);
}
#endif

#if defined(EA_PORT_NATIVE)
int UnportedEventBinding(lua_State *luaState) {
    return 0;
}
#endif

// PC: 0x61B960.
int LuaPanic(lua_State *luaState) {
    LuaRuntime::DumpStack(luaState);
#if defined(EA_PORT_NATIVE)
    std::fprintf(stderr, "[lua] panic: %s\n", lua_tostring(luaState, -1) != nullptr ? lua_tostring(luaState, -1) : "?");
#endif
    return 0;
}

} // namespace

// PC: 0x61F3B0.
LuaRuntime::LuaRuntime(unsigned int memPoolSize)
    : mState(nullptr),          //
      mMemPoolSize(memPoolSize), //
      mMemPoolUsed(0),          //
      mMemPoolBlock(nullptr),   //
      mHeapSnapshot(nullptr),   //
      mHeapSnapshotSize(0),     //
      mHeapSnapshotCompressed(false),
      mNeedToResetHeap(false), //
      mMessagesBeingDelivered(0) {
    for (int i = 0; i < 16; i++) {
        mSmallFreeBlockChain[i] = nullptr;
    }
#if defined(EA_PORT_NATIVE)
    luaM_setallocator(LuaRealloc, LuaFree);
#endif
}

// PS2: 0x3039D8.
LuaRuntime::~LuaRuntime() {
    if (mState != nullptr) {
        lua_close(mState);
        mState = nullptr;
    }
    if (mHeapSnapshot != nullptr) {
        bFree(mHeapSnapshot);
        mHeapSnapshot = nullptr;
    }
}

// PC: 0x61F490.
void LuaRuntime::Init(std::size_t memPoolSize) {
    mObj = new LuaRuntime(static_cast<unsigned int>(memPoolSize));
    mObj->CreateState();
}

// PS2: 0x303B10.
void LuaRuntime::Shutdown() {
    delete mObj;
    mObj = nullptr;
}

// PC: 0x61D250. Undefined globals go through HandleGlobalIndex, which turns
// E* names into event constructors.
void LuaRuntime::CreateState() {
    mState = lua_open();
    lua_atpanic(mState, LuaPanic);
    lua_newtable(mState);
    lua_pushstring(mState, "__index");
    lua_pushcclosure(mState, HandleGlobalIndex, 0);
    lua_settable(mState, -3);
    lua_setmetatable(mState, LUA_GLOBALSINDEX);
    // The PC also raises the GC threshold to 0x40000000 here; the collector
    // is compiled out (lgc.c) anyway.
}

// PC: 0x609F20. Copies (compressing if there's room) the VM's pool so the
// heap can be put back to its post-binding state later.
// TODO(port): no pool natively (see LuaRealloc), so there's nothing to copy.
void LuaRuntime::TakeResetSnapshot() {}

// PS2: 0x304690.
void LuaRuntime::BeginDelivery() {
    mMessagesBeingDelivered++;
}

// PS2: 0x3046A0. A heap reset requested while scripts ran waits until the
// last delivery ends.
void LuaRuntime::EndDelivery() {
    mMessagesBeingDelivered--;
    if (mMessagesBeingDelivered == 0 && mNeedToResetHeap) {
        // TODO(port): ResetHeap (PS2 0x304768) restores the snapshot.
        mNeedToResetHeap = false;
    }
}

// PC: 0x6182F0. Formats every stack slot (the PC's output is compiled out).
void LuaRuntime::DumpStack(lua_State *luaState) {
#if defined(EA_PORT_NATIVE)
    int top = lua_gettop(luaState);
    for (int i = 1; i <= top; i++) {
        int type = lua_type(luaState, i);
        switch (type) {
            case LUA_TSTRING:
                std::fprintf(stderr, "[lua]   %d/-%d: '%s'\n", i, top - i + 1, lua_tostring(luaState, i));
                break;
            case LUA_TNUMBER:
                std::fprintf(stderr, "[lua]   %d/-%d: %.2f\n", i, top - i + 1, static_cast<double>(lua_tonumber(luaState, i)));
                break;
            default:
                std::fprintf(stderr, "[lua]   %d/-%d: %s\n", i, top - i + 1, lua_typename(luaState, type));
                break;
        }
    }
#endif
}

// PC: 0x61B970. __index of the globals table: an E<name> global that isn't
// defined yet is the event's Lua constructor, cached in the globals.
int LuaRuntime::HandleGlobalIndex(lua_State *luaState) {
    const char *name = lua_isstring(luaState, -1) ? lua_tostring(luaState, -1) : nullptr;
    if (name != nullptr && name[0] == 'E') {
        RegisterEvent::LuaBinding binding = RegisterEvent::GetLuaBinding(stringhash32(name));
        if (binding != nullptr) {
            lua_pushcclosure(luaState, binding, 0);
            lua_pushvalue(luaState, -2);
            lua_pushvalue(luaState, -2);
            lua_rawset(luaState, LUA_GLOBALSINDEX);
            return 1;
        }
#if defined(EA_PORT_NATIVE)
        // The event isn't reconstructed: hand the script a constructor that
        // does nothing (on PC every event has one), so the handler carries on.
        std::fprintf(stderr, "[lua] TODO(port): no Lua binding for event %s\n", name);
        lua_pushcclosure(luaState, UnportedEventBinding, 0);
        lua_pushvalue(luaState, -2);
        lua_pushvalue(luaState, -2);
        lua_rawset(luaState, LUA_GLOBALSINDEX);
        return 1;
#endif
    }
    lua_pushnil(luaState);
    return 1;
}

#if defined(EA_PORT_NATIVE)
// ---------------------------------------------------------------------------
// Table (de)serialization: an activity's script variables kept in GManager's
// object state blocks (GActivity::SerializeVars / DeserializeVars), the
// persistent ones in the career save.
//
// PC: 0x617A80 / 0x617F80. Each key and each value is a header byte (0x80
// present, 0x40 a key, 0x20 packed, 0x10 a boolean's value, the Lua type in
// the low nibble) and its data: a number packed into one signed byte or a
// float, a string (a persistent table's keys packed 5 bits a character, see
// PackName) or a nested table; a session table (not saved) may also hold
// light and full userdata (with their metatable's registry name's hash). A
// zero byte ends a table. Natively a light userdata takes 8 bytes (session
// tables only, never saved).
// ---------------------------------------------------------------------------

extern "C" size_t PortLua_UserdataSize(lua_State *L, int index);

namespace {

// PC: 0x603D90. a-z 1..26, '_' 27, A-Z 28 then 1..26, 0 to end; 0 when the
// name has anything else.
int PackName(const char *name, unsigned char *out) {
    int bytes = 0;
    int bits = 0;
    int numBits = 0;
    char c;
    do {
        c = *name++;
        int code = 0;
        if (c != '\0') {
            if (c >= 'a' && c <= 'z') {
                code = c - 0x60;
            } else if (c >= 'A' && c <= 'Z') {
                code = c - 0x40;
                bits |= 0x1C << numBits;
                numBits += 5;
            } else if (c == '_') {
                code = 0x1B;
            } else {
                return 0;
            }
        }
        bits |= code << numBits;
        numBits += 5;
        const int keep = c != '\0' ? 7 : 0;
        if (keep < numBits) {
            int count = ((numBits - keep - 1) >> 3) + 1;
            bytes += count;
            numBits -= count * 8;
            do {
                if (out != nullptr) {
                    *out++ = static_cast<unsigned char>(bits);
                }
                bits >>= 8;
            } while (--count != 0);
        }
    } while (c != '\0');
    return bytes;
}

// PC: 0x603E40.
int UnpackName(const unsigned char *in, char *out) {
    const unsigned char *p = in;
    int bits = 0;
    int numBits = 0;
    bool upper = false;
    for (;;) {
        if (numBits < 5) {
            bits |= *p++ << numBits;
            numBits += 8;
        }
        int code = bits & 0x1F;
        bits >>= 5;
        numBits -= 5;
        if (code == 0) {
            *out = '\0';
            return static_cast<int>(p - in);
        }
        if (code < 0x1B) {
            *out++ = static_cast<char>(code + (upper ? '@' : '`'));
            upper = false;
        } else if (code == 0x1B) {
            *out++ = '_';
        } else if (code == 0x1C) {
            upper = true;
        }
    }
}

// The registry name of the metatable of the userdata at index (0 for none).
unsigned int MetatableNameHash(lua_State *L, int index) {
    if (!lua_getmetatable(L, index)) {
        return 0;
    }
    unsigned int hash = 0;
    lua_pushnil(L);
    while (lua_next(L, LUA_REGISTRYINDEX) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_rawequal(L, -1, -3)) {
            hash = stringhash32(lua_tostring(L, -2));
            lua_settop(L, -3);
            break;
        }
        lua_settop(L, -2);
    }
    lua_settop(L, -2);
    return hash;
}

// One key (index -2) or value (-1) of the pair on the stack.
unsigned int SerializeEntry(lua_State *L, int index, unsigned char *dest, bool session) {
    const int type = lua_type(L, index);
    unsigned char header = static_cast<unsigned char>((index == -2 ? 0xC0 : 0x80) | (type & 0xF));
    unsigned int size = 1;
    switch (type) {
        case LUA_TBOOLEAN:
            if (lua_toboolean(L, index)) {
                header |= 0x10;
            }
            break;
        case LUA_TLIGHTUSERDATA: {
            void *ptr = lua_touserdata(L, index);
            if (dest != nullptr) {
                std::memcpy(dest + 1, &ptr, sizeof(ptr));
            }
            size += sizeof(ptr);
            break;
        }
        case LUA_TNUMBER: {
            float value = static_cast<float>(lua_tonumber(L, index));
            signed char small = static_cast<signed char>(static_cast<long long>(value));
            if (value == static_cast<float>(small)) {
                header |= 0x20;
                if (dest != nullptr) {
                    dest[1] = static_cast<unsigned char>(small);
                }
                size += 1;
            } else {
                if (dest != nullptr) {
                    std::memcpy(dest + 1, &value, 4);
                }
                size += 4;
            }
            break;
        }
        case LUA_TSTRING: {
            const char *text = lua_tostring(L, index);
            int length = static_cast<int>(std::strlen(text));
            int packed = (header & 0x40) != 0 && !session ? PackName(text, nullptr) : 0;
            if (packed >= 1 && packed < length + 1 && packed <= 0x80) {
                header |= 0x20;
                if (dest != nullptr) {
                    PackName(text, dest + 1);
                }
                size += packed;
            } else {
                if (dest != nullptr) {
                    std::memcpy(dest + 1, text, length + 1);
                }
                size += length + 1;
            }
            break;
        }
        case LUA_TTABLE:
            lua_pushvalue(L, index);
            size += LuaRuntime::SerializeTable(L, dest != nullptr ? dest + 1 : nullptr, session);
            lua_settop(L, -2);
            break;
        case LUA_TUSERDATA: {
            unsigned int length = static_cast<unsigned int>(PortLua_UserdataSize(L, index)) & 0xFF;
            unsigned int hash = MetatableNameHash(L, index);
            if (dest != nullptr) {
                dest[1] = static_cast<unsigned char>(length);
                std::memcpy(dest + 2, lua_touserdata(L, index), length);
                std::memcpy(dest + 2 + length, &hash, 4);
            }
            size += 1 + length + 4;
            break;
        }
        default:
            break;
    }
    if (dest != nullptr) {
        dest[0] = header;
    }
    return size;
}

} // namespace

// PC: 0x617A80. The table on the top of the stack; returns its size (and
// with no destination only measures it).
unsigned int LuaRuntime::SerializeTable(lua_State *L, unsigned char *dest, bool session) {
    unsigned int size = 0;
    if (lua_type(L, -1) == LUA_TTABLE) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            const int keyType = lua_type(L, -2);
            const int valueType = lua_type(L, -1);
            const bool keyOk = keyType == LUA_TBOOLEAN || keyType == LUA_TNUMBER || keyType == LUA_TSTRING;
            bool valueOk = valueType != LUA_TNIL && valueType != LUA_TFUNCTION && valueType != LUA_TTHREAD;
            if ((valueType == LUA_TLIGHTUSERDATA || valueType == LUA_TUSERDATA) && !session) {
                valueOk = false;
            }
            if (keyOk && valueOk) {
                size += SerializeEntry(L, -2, dest != nullptr ? dest + size : nullptr, session);
                size += SerializeEntry(L, -1, dest != nullptr ? dest + size : nullptr, session);
            }
            lua_settop(L, -2);
        }
    }
    if (dest != nullptr) {
        dest[size] = 0;
    }
    return size + 1;
}

// PC: 0x617F80. Pushes the table read from src; returns the bytes read, or 0
// (and pushes nothing) when they don't make a table.
unsigned int LuaRuntime::DeserializeTable(lua_State *L, const unsigned char *src, bool session) {
    if (src == nullptr) {
        return 0;
    }
    const int top = lua_gettop(L);
    lua_newtable(L);
    const unsigned char *p = src;
    unsigned char header = *p++;
    bool key = true;
    while ((header & 0x80) != 0) {
        if (key != ((header & 0x40) != 0)) {
            lua_settop(L, top);
            return 0;
        }
        switch (header & 0xF) {
            case LUA_TBOOLEAN:
                lua_pushboolean(L, (header & 0x10) != 0);
                break;
            case LUA_TLIGHTUSERDATA: {
                if (key || !session) {
                    lua_settop(L, top);
                    return 0;
                }
                void *ptr;
                std::memcpy(&ptr, p, sizeof(ptr));
                p += sizeof(ptr);
                lua_pushlightuserdata(L, ptr);
                break;
            }
            case LUA_TNUMBER: {
                float value;
                if ((header & 0x20) != 0) {
                    value = static_cast<float>(static_cast<signed char>(*p++));
                } else {
                    std::memcpy(&value, p, 4);
                    p += 4;
                }
                if (value != value) {
                    lua_settop(L, top);
                    return 0;
                }
                lua_pushnumber(L, value);
                break;
            }
            case LUA_TSTRING:
                if ((header & 0x20) != 0) {
                    char name[128];
                    p += UnpackName(p, name);
                    lua_pushstring(L, name);
                } else {
                    const char *text = reinterpret_cast<const char *>(p);
                    lua_pushstring(L, text);
                    p += std::strlen(text) + 1;
                }
                break;
            case LUA_TTABLE: {
                unsigned int size = key ? 0 : DeserializeTable(L, p, session);
                if (size == 0) {
                    lua_settop(L, top);
                    return 0;
                }
                p += size;
                break;
            }
            case LUA_TUSERDATA: {
                if (key || !session) {
                    lua_settop(L, top);
                    return 0;
                }
                unsigned int length = *p++;
                void *data = lua_newuserdata(L, length);
                std::memcpy(data, p, length);
                p += length;
                unsigned int hash;
                std::memcpy(&hash, p, 4);
                p += 4;
                if (hash != 0) {
                    bool found = false;
                    lua_pushnil(L);
                    while (lua_next(L, LUA_REGISTRYINDEX) != 0) {
                        if (lua_type(L, -2) == LUA_TSTRING && stringhash32(lua_tostring(L, -2)) == hash) {
                            lua_setmetatable(L, -3);
                            lua_settop(L, -2);
                            found = true;
                            break;
                        }
                        lua_settop(L, -2);
                    }
                    if (!found) {
                        lua_settop(L, top);
                        return 0;
                    }
                }
                break;
            }
            default:
                lua_settop(L, top);
                return 0;
        }
        if (!key) {
            lua_settable(L, -3);
        }
        header = *p++;
        key = !key;
    }
    if (!key) {
        lua_settop(L, top);
        return 0;
    }
    return static_cast<unsigned int>(p - src);
}
#endif
