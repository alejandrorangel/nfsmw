#include "Speed/Indep/Src/Lua/LuaAttributes.h"

#include "Speed/Indep/Src/Gameplay/GReflected.h"
#include "Speed/Indep/Src/Lua/LuaBindery.h"
#include "Speed/Indep/Src/Misc/attribuserinclude.h"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

LuaAttributes *LuaAttributes::fObj = nullptr;

namespace {

template <typename T> const T &ElementOf(const LuaAttribAccessInfo &info) {
    return info.mAttribute->Get<T>(static_cast<unsigned int>(info.mAttribIndex));
}

// PC: 0x60A1F0 .. 0x60A550 (numbers and bools).
template <typename T> void PushNumber(const LuaAttribAccessInfo &info) {
    lua_pushnumber(info.mLuaState, static_cast<lua_Number>(ElementOf<T>(info)));
}

void PushBool(const LuaAttribAccessInfo &info) {
    lua_pushboolean(info.mLuaState, ElementOf<EA::Reflection::Bool>(info));
}

// PC: 0x618CE0. A one-character string.
void PushChar(const LuaAttribAccessInfo &info) {
    char str[2];
    str[0] = ElementOf<EA::Reflection::Char>(info);
    str[1] = '\0';
    lua_pushstring(info.mLuaState, str);
}

// PC: 0x618CA0.
void PushText(const LuaAttribAccessInfo &info) {
    const char *text = ElementOf<EA::Reflection::Text>(info);
    lua_pushstring(info.mLuaState, text);
}

// PC: 0x618C50.
void PushStringKey(const LuaAttribAccessInfo &info) {
    lua_pushstring(info.mLuaState, ElementOf<Attrib::StringKey>(info).GetString());
}

// PC: 0x618D60. A reference reads as the instance it connects to.
void PushConnectedInstance(const LuaAttribAccessInfo &info) {
    Attrib::Key key = info.mAttribute->GetKey();
    LuaBindery::PushRuntimeInstance(info.mLuaState, info.mRuntimeInstance->GetConnectedInstance(key, info.mAttribIndex));
}

// PC: 0x8F5DC0. No attribute can be written from a script.
struct TypeAccessorEntry {
    const char *mTypeName;
    void (*mPush)(const LuaAttribAccessInfo &);
    void (*mSet)(const LuaAttribAccessInfo &);
};

const TypeAccessorEntry kTypeAccessors[] = {
    {"EA::Reflection::Float", PushNumber<float>, nullptr},
    {"EA::Reflection::Double", PushNumber<double>, nullptr},
    {"EA::Reflection::Int64", PushNumber<int64_t>, nullptr},
    {"EA::Reflection::Int32", PushNumber<int32_t>, nullptr},
    {"EA::Reflection::Int16", PushNumber<int16_t>, nullptr},
    {"EA::Reflection::Int8", PushNumber<int8_t>, nullptr},
    {"EA::Reflection::UInt64", PushNumber<uint64_t>, nullptr},
    {"EA::Reflection::UInt32", PushNumber<uint32_t>, nullptr},
    {"EA::Reflection::UInt16", PushNumber<uint16_t>, nullptr},
    {"EA::Reflection::UInt8", PushNumber<uint8_t>, nullptr},
    {"EA::Reflection::Char", PushChar, nullptr},
    {"EA::Reflection::Bool", PushBool, nullptr},
    {"EA::Reflection::Text", PushText, nullptr},
    {"GCollectionKey", PushConnectedInstance, nullptr},
    {"Attrib::RefSpec", PushConnectedInstance, nullptr},
    {"Attrib::StringKey", PushStringKey, nullptr},
};

GRuntimeInstance *CheckRuntimeInstance(lua_State *L, int index) {
    GRuntimeInstance **userdata = static_cast<GRuntimeInstance **>(lua_touserdata(L, index));
    if (userdata == nullptr) {
        return nullptr;
    }
    return *userdata;
}

// The userdata behind activity.Checkpoint.
// total size: 0x8
struct LuaAttribArrayInfo {
    GRuntimeInstance *mInstance;
    Attrib::Key mAttribKey;
};

// PC: 0x61BB60. __index of a runtime instance.
int IndexRuntimeInstance(lua_State *L) {
    GRuntimeInstance *instance = CheckRuntimeInstance(L, 1);
    const char *name = lua_tostring(L, 2);
    if (instance == nullptr || name == nullptr) {
        lua_pushnil(L);
        return 1;
    }
    Attrib::Key key = Attrib::StringToKey(name);
    Attrib::Attribute attrib = instance->Get(key);
    if (!attrib.IsValid()) {
        GRuntimeInstance *connected = instance->GetConnectedInstance(key, 0);
        if (connected != nullptr) {
            LuaBindery::PushRuntimeInstance(L, connected);
            return 1;
        }
    }
    LuaAttribAccessInfo info;
    info.mLuaState = L;
    info.mRuntimeInstance = instance;
    info.mAttribute = &attrib;
    info.mAttribIndex = 0;
    info.mSrcStackIndex = 0;
    LuaAttributes::Get().PushAttributeValue(info, false);
    return 1;
}

// PC: 0x61BD60. Writes go through the (empty) setters.
int NewIndexRuntimeInstance(lua_State *L) {
    return 0;
}

// PC: 0x6040F0. Two instances are the same if they're the same collection or
// one is the other's customized (dynamic) copy.
bool SameInstance(GRuntimeInstance *a, GRuntimeInstance *b) {
    if (a == b) {
        return true;
    }
    if (a == nullptr || b == nullptr) {
        return false;
    }
    if (a->IsDynamic() && a->GetParent() == b->GetCollection()) {
        return true;
    }
    if (b->IsDynamic() && a->GetCollection() == b->GetParent()) {
        return true;
    }
    return false;
}

// PC: 0x61BE80.
int EqRuntimeInstance(lua_State *L) {
    lua_pushboolean(L, SameInstance(CheckRuntimeInstance(L, 1), CheckRuntimeInstance(L, 2)));
    return 1;
}

// PC: 0x61BF00. array[i] (1-based) or array.Count.
int IndexAttribArray(lua_State *L) {
    LuaAttribArrayInfo *array = static_cast<LuaAttribArrayInfo *>(lua_touserdata(L, 1));
    if (array == nullptr) {
        return 0;
    }
    Attrib::Attribute attrib = array->mInstance->Get(array->mAttribKey);
    if (lua_type(L, 2) == LUA_TNUMBER) {
        int index = static_cast<int>(lua_tonumber(L, 2)) - 1;
        if (index >= 0) {
            LuaAttribAccessInfo info;
            info.mLuaState = L;
            info.mRuntimeInstance = array->mInstance;
            info.mAttribute = &attrib;
            info.mAttribIndex = index;
            info.mSrcStackIndex = 0;
            LuaAttributes::Get().PushAttributeValue(info, true);
        } else {
            lua_pushnil(L);
        }
        return 1;
    }
    if (lua_isstring(L, 2) && bStrCmp(lua_tostring(L, 2), "Count") == 0) {
        lua_pushnumber(L, static_cast<lua_Number>(attrib.GetLength()));
        return 1;
    }
    return 0;
}

// PC: 0x604150.
int NewIndexAttribArray(lua_State *L) {
    return 0;
}

// PC: 0x61C0F0.
int EqAttribArray(lua_State *L) {
    LuaAttribArrayInfo *a = static_cast<LuaAttribArrayInfo *>(lua_touserdata(L, 1));
    LuaAttribArrayInfo *b = static_cast<LuaAttribArrayInfo *>(lua_touserdata(L, 2));
    bool equal = a != nullptr && b != nullptr && SameInstance(a->mInstance, b->mInstance) && a->mAttribKey == b->mAttribKey;
    lua_pushboolean(L, equal);
    return 1;
}

void SetMetaFunction(lua_State *L, const char *name, lua_CFunction fn) {
    lua_pushstring(L, name);
    lua_pushcclosure(L, fn, 0);
    lua_settable(L, -3);
}

} // namespace

// PC: 0x61F6D0.
LuaAttributes::LuaAttributes() : fGameplayClass(Attrib::Database::Get().GetClass(Attrib::Gen::gameplay::ClassKey())) {
    BuildAttributeTypeTable();
}

LuaAttributes::~LuaAttributes() {}

// PC: 0x61F750.
void LuaAttributes::Init() {
    fObj = new LuaAttributes;
}

// PS2: 0x307C70.
void LuaAttributes::Shutdown() {
    delete fObj;
    fObj = nullptr;
}

// PC: 0x61D820. The PC walks the attribute database's types and looks each up
// by name; the type key is the hash of the name.
void LuaAttributes::BuildAttributeTypeTable() {
    for (unsigned int i = 0; i < sizeof(kTypeAccessors) / sizeof(kTypeAccessors[0]); i++) {
        LuaAttribAccessors &accessors = fTypeAccessors[Attrib::StringToKey(kTypeAccessors[i].mTypeName)];
        accessors.mPushAttrib = kTypeAccessors[i].mPush;
        accessors.mSetAttrib = kTypeAccessors[i].mSet;
    }
}

// PC: 0x618DD0. An array attribute (read whole) becomes a LuaAttribArrayInfo.
void LuaAttributes::PushAttributeValue(const LuaAttribAccessInfo &info, bool arrayAccess) {
    lua_State *L = info.mLuaState;
    if (info.mAttribute->IsValid()) {
        const Attrib::Definition *def = fGameplayClass->GetDefinition(info.mAttribute->GetKey());
        if (def != nullptr && def->IsArray() && !arrayAccess) {
            LuaAttribArrayInfo *array = static_cast<LuaAttribArrayInfo *>(lua_newuserdata(L, sizeof(LuaAttribArrayInfo)));
            array->mInstance = info.mRuntimeInstance;
            array->mAttribKey = info.mAttribute->GetKey();
            LuaBindery::AttachMetatable(L, "LuaAttribArrayInfo");
            return;
        }
        UTL::Std::map<unsigned int, LuaAttribAccessors, _type_map>::iterator iter = fTypeAccessors.find(info.mAttribute->GetType());
        if (iter != fTypeAccessors.end()) {
            if (iter->second.mPushAttrib != nullptr) {
                iter->second.mPushAttrib(info);
            }
            return;
        }
    }
    lua_pushnil(L);
}

// PC: 0x61C180.
void LuaAttributes::BindAccessors(lua_State *luaState) {
    LuaBindery::LoadMetatable(luaState, "GRuntimeInstance");
    SetMetaFunction(luaState, "__index", IndexRuntimeInstance);
    SetMetaFunction(luaState, "__newindex", NewIndexRuntimeInstance);
    SetMetaFunction(luaState, "__eq", EqRuntimeInstance);
    lua_settop(luaState, -2);
    LuaBindery::LoadMetatable(luaState, "LuaAttribArrayInfo");
    SetMetaFunction(luaState, "__index", IndexAttribArray);
    SetMetaFunction(luaState, "__newindex", NewIndexAttribArray);
    SetMetaFunction(luaState, "__eq", EqAttribArray);
    lua_settop(luaState, -2);
}
