#include "Speed/Indep/Src/Gameplay/GObjectBlock.h"

#include "Speed/Indep/Src/Gameplay/GActivity.h"
#include "Speed/Indep/Src/Gameplay/GCharacter.h"
#include "Speed/Indep/Src/Gameplay/GHandler.h"
#include "Speed/Indep/Src/Gameplay/GMarker.h"
#include "Speed/Indep/Src/Gameplay/GState.h"
#include "Speed/Indep/Src/Gameplay/GTrigger.h"
#include "Speed/Indep/Src/Gameplay/GVault.h"
#include "Speed/Indep/Tools/AttribSys/Runtime/AttribLoadAndGo.h"

#include <list>

namespace {

// The template collection every instance of a type derives from (PC: the
// keys at 0x8F5C50.., one per FindInstances<T>).
template <typename T> struct GObjectTemplate;
template <> struct GObjectTemplate<GHandler> {
    static const Attrib::Key kKey = 0xbb61c9e5;
};
template <> struct GObjectTemplate<GState> {
    static const Attrib::Key kKey = 0x5f91f83d; // "state"
};
template <> struct GObjectTemplate<GActivity> {
    static const Attrib::Key kKey = 0x7fcb7aba; // "activity"
};
template <> struct GObjectTemplate<GCharacter> {
    static const Attrib::Key kKey = 0xff4fe4b0; // "character"
};
template <> struct GObjectTemplate<GMarker> {
    static const Attrib::Key kKey = 0xa7bccf63; // "marker"
};
template <> struct GObjectTemplate<GTrigger> {
    static const Attrib::Key kKey = 0x3c779b79; // "trigger"
};

typedef std::list<Attrib::Key> KeyList;

// PC: 0x5F3D90 (GHandler) .. 0x5F4510 (GTrigger). Collects the vault's
// collections that are instances of T's template.
template <typename T> unsigned int FindInstances(GVault *vault, KeyList *keys, unsigned int *objCount, unsigned int *connCount) {
    Attrib::Gen::gameplay templateObj(GObjectTemplate<T>::kKey, 0, nullptr);
    Attrib::Key loadDataType = Attrib::StringHash32("Attrib::CollectionLoadData");
    Attrib::Vault *attribVault = vault->GetAttribVault();
    unsigned int found = 0;
    unsigned int connections = 0;
    if (attribVault != nullptr) {
        unsigned int numExports = attribVault->CountExports();
        for (unsigned int onExport = 0; onExport < numExports; onExport++) {
            if (attribVault->GetExportType(onExport) != loadDataType) {
                continue;
            }
            const Attrib::Collection *collection = static_cast<const Attrib::Collection *>(attribVault->GetExportData(onExport));
            Attrib::Key key = Attrib::GetCollectionKey(collection);
            Attrib::Gen::gameplay instanceObj(key, 0, nullptr);
            if (GObjectBlock::CollectionIsInstanceOfTemplate(instanceObj, templateObj)) {
                if (keys != nullptr) {
                    keys->push_back(key);
                }
                connections += GObjectBlock::CalcNumConnections(key);
                found++;
            }
        }
    }
    if (objCount != nullptr) {
        *objCount += found;
    }
    if (connCount != nullptr) {
        *connCount += connections;
    }
    return (found * 0x30 + connections * 8 + 0xf) & ~0xfu;
}

} // namespace

// PC: 0x5FA520 (inlined).
GObjectBlock::GObjectBlock(GVault *vault, uint8 *buffer) : mVault(vault), mObjectBuffer(buffer) {
    for (int i = 0; i < kGameplayObjType_Count; i++) {
        mObjectCount[i] = 0;
        mObjectSize[i] = 0;
        mObjectList[i] = nullptr;
#if defined(EA_PORT_NATIVE)
        mObjects[i] = nullptr;
#endif
    }
#if defined(EA_PORT_NATIVE)
    mConnectionBuffer = nullptr;
#endif
}

// PC: 0x5EE090.
GObjectBlock::~GObjectBlock() {
    DeleteObjects<GTrigger>();
    DeleteObjects<GMarker>();
    DeleteObjects<GCharacter>();
    DeleteObjects<GActivity>();
    DeleteObjects<GState>();
    DeleteObjects<GHandler>();
#if defined(EA_PORT_NATIVE)
    delete[] mConnectionBuffer;
    mConnectionBuffer = nullptr;
#endif
}

// PC: 0x5FA270.
void GObjectBlock::Initialize(unsigned int bufferSize) {
#if defined(EA_PORT_NATIVE)
    unsigned int totalConnections = 0;
    FindInstances<GHandler>(mVault, nullptr, nullptr, &totalConnections);
    FindInstances<GState>(mVault, nullptr, nullptr, &totalConnections);
    FindInstances<GActivity>(mVault, nullptr, nullptr, &totalConnections);
    FindInstances<GCharacter>(mVault, nullptr, nullptr, &totalConnections);
    FindInstances<GMarker>(mVault, nullptr, nullptr, &totalConnections);
    FindInstances<GTrigger>(mVault, nullptr, nullptr, &totalConnections);
    mConnectionBuffer = new ConnectedInstance[totalConnections > 0 ? totalConnections : 1];
    uint8 *buffer = reinterpret_cast<uint8 *>(mConnectionBuffer);
#else
    uint8 *buffer = mObjectBuffer;
#endif
    buffer += CreateObjects<GHandler>(mVault, buffer);
    buffer += CreateObjects<GState>(mVault, buffer);
    buffer += CreateObjects<GActivity>(mVault, buffer);
    GOBJECTBLOCK_CREATE_CUSTOM_TYPES();
}

// PC: 0x5F55D0 (GHandler) .. 0x602A60 (GTrigger). Creates the instances and
// hands each its slice of the connection table. Natively only the connection
// tables are in the buffer; returns the bytes of it used.
template <typename T> unsigned int GObjectBlock::CreateObjects(GVault *vault, uint8 *buffer) {
    KeyList keys;
    unsigned int count = 0;
    FindInstances<T>(vault, &keys, &count, nullptr);
    ConnectedInstance *connections = reinterpret_cast<ConnectedInstance *>(buffer);
    const int type = T::kType;
#if defined(EA_PORT_NATIVE)
    mObjects[type] = new GRuntimeInstance *[count > 0 ? count : 1];
#endif
    unsigned int created = 0;
    for (KeyList::iterator iter = keys.begin(); iter != keys.end(); ++iter) {
        Attrib::Key key = *iter;
        // ::new: GCharacter also has UTL::COM::Object's allocator.
        T *object = ::new T(key);
        unsigned int numConnections = CalcNumConnections(key);
        object->SetConnectionBuffer(connections, numConnections);
        connections += numConnections;
#if defined(EA_PORT_NATIVE)
        mObjects[type][created] = object;
#endif
        if (created == 0) {
            mObjectList[type] = object;
        }
        created++;
    }
    mObjectCount[type] = created;
    mObjectSize[type] = sizeof(T);
    return static_cast<unsigned int>(reinterpret_cast<uint8 *>(connections) - buffer);
}

// PC: 0x5E76F0 (GTrigger) .. 0x5E7830 (GHandler).
template <typename T> void GObjectBlock::DeleteObjects() {
    const int type = T::kType;
#if defined(EA_PORT_NATIVE)
    if (mObjects[type] != nullptr) {
        for (unsigned int i = 0; i < mObjectCount[type]; i++) {
            T *object = static_cast<T *>(mObjects[type][i]);
            object->~T();
            ::operator delete(object);
        }
        delete[] mObjects[type];
        mObjects[type] = nullptr;
    }
#endif
    mObjectCount[type] = 0;
    mObjectSize[type] = 0;
    mObjectList[type] = nullptr;
}

// PC: 0x5F51A0.
unsigned int GObjectBlock::CalcSpaceRequired(GVault *vault, unsigned int *outObjCount) {
    unsigned int objCount = 0;
    unsigned int bytes = 0;
    bytes += FindInstances<GHandler>(vault, nullptr, &objCount, nullptr);
    bytes += FindInstances<GState>(vault, nullptr, &objCount, nullptr);
    bytes += FindInstances<GActivity>(vault, nullptr, &objCount, nullptr);
    bytes += FindInstances<GCharacter>(vault, nullptr, &objCount, nullptr);
    bytes += FindInstances<GMarker>(vault, nullptr, &objCount, nullptr);
    bytes += FindInstances<GTrigger>(vault, nullptr, &objCount, nullptr);
    if (outObjCount != nullptr) {
        *outObjCount = objCount;
    }
    return bytes;
}

// PC: 0x5E5220. Templates themselves aren't instanced.
bool GObjectBlock::CollectionIsInstanceOfTemplate(const Attrib::Gen::gameplay &instanceObj, const Attrib::Gen::gameplay &templateObj) {
    if (instanceObj.Template()) {
        return false;
    }
    Attrib::Key parent = instanceObj.GetParent();
    while (parent != 0) {
        Attrib::Gen::gameplay parentObj(parent, 0, nullptr);
        if (parentObj.GetCollection() == templateObj.GetCollection()) {
            return true;
        }
        parent = parentObj.GetParent();
    }
    return false;
}

// PC: 0x5E5330. One connection per child and per element of every
// GCollectionKey attribute of the collection and its parents.
unsigned int GObjectBlock::CalcNumConnections(Attrib::Key collectionKey) {
    Attrib::Class *gameplayClass = Attrib::Database::Get().GetClass(Attrib::Gen::gameplay::ClassKey());
    Attrib::Gen::gameplay collection(collectionKey, 0, nullptr);
    Attrib::Type collectionKeyType = Attrib::StringToKey("GCollectionKey");
    unsigned int count = collection.Num_Children();
    Attrib::Key key = collectionKey;
    while (key != 0) {
        Attrib::Gen::gameplay obj(key, 0, nullptr);
        if (!obj.IsValid()) {
            break;
        }
        for (Attrib::AttributeIterator iter = obj.Iterator(); iter.Valid(); iter.Advance()) {
            Attrib::Key attribKey = iter.GetKey();
            if (attribKey == 0x916e0e78) { // Children
                continue;
            }
            Attrib::Attribute attrib = obj.Get(attribKey);
            if (!attrib.IsValid() || attrib.GetType() != collectionKeyType) {
                continue;
            }
            const Attrib::Definition *def = gameplayClass->GetDefinition(attribKey);
            if (def != nullptr && def->IsArray()) {
                count += attrib.GetLength();
            } else {
                count += 1;
            }
        }
        key = obj.GetParent();
    }
    return count;
}
