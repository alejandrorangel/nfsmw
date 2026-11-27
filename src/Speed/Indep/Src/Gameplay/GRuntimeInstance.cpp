#include "Speed/Indep/Src/Gameplay/GRuntimeInstance.h"

#include "Speed/Indep/Src/Gameplay/GActivity.h"
#include "Speed/Indep/Src/Gameplay/GCharacter.h"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GMarker.h"
#include "Speed/Indep/Src/Gameplay/GTrigger.h"

#include <algorithm>

GRuntimeInstance *GRuntimeInstance::sRingListHead[kGameplayObjType_Count];

// PC: 0x5E8130.
GRuntimeInstance::GRuntimeInstance(const Attrib::Key &key, GameplayObjType type)
    : Attrib::Gen::gameplay(key, 0, nullptr), //
      mFlags(0),                               //
      mNumConnected(0),                        //
      mConnected(nullptr),                     //
      mPrev(nullptr),                          //
      mNext(nullptr) {
#if defined(EA_PORT_NATIVE)
    mType = type;
#endif
    AddToTypeList(type);
    GManager::Get().RegisterInstance(this);
    mFlags |= kFlag_Registered;
}

// PS2: 0x2DFA58 (inlined into the PC's DeleteObjects<T>).
GRuntimeInstance::~GRuntimeInstance() {
    if (GManager::Exists()) {
        GManager::Get().UnregisterInstance(this);
    }
    RemoveFromTypeList();
    if ((mFlags & kFlag_OwnsConnections) != 0) {
        delete[] mConnected;
    }
    mConnected = nullptr;
    mNumConnected = 0;
}

GameplayObjType GRuntimeInstance::GetType() const {
    return kGameplayObjType_Invalid;
}

// PC: inlined in the ctor.
void GRuntimeInstance::AddToTypeList(GameplayObjType type) {
    GRuntimeInstance *&head = sRingListHead[type];
    if (head == nullptr) {
        mNext = this;
        mPrev = this;
        head = this;
    } else {
        mNext = head;
        mPrev = head->mPrev;
        mPrev->mNext = this;
        mNext->mPrev = this;
    }
}

// PS2: 0x2DFDB8.
void GRuntimeInstance::RemoveFromTypeList() {
    if (mNext == nullptr) {
        return;
    }
    for (int type = 0; type < kGameplayObjType_Count; type++) {
        if (sRingListHead[type] == this) {
            sRingListHead[type] = mNext != this ? mNext : nullptr;
        }
    }
    mPrev->mNext = mNext;
    mNext->mPrev = mPrev;
    mNext = nullptr;
    mPrev = nullptr;
}

// PS2: 0x2DFD50. The attribute (or child name) key reduced to 24 bits, with
// the array index in the low byte.
uint32 GRuntimeInstance::MakePackedKey(Attrib::Key key, int index) {
    return (GManager::Get().Get24BitAttributeKey(key) << 8) | (static_cast<unsigned int>(index) & 0xff);
}

// PS2: 0x2DFAE8.
void GRuntimeInstance::SetConnectionBuffer(ConnectedInstance *destBuffer, unsigned int numEntries) {
    mConnected = destBuffer;
    mNumConnected = 0;
}

// PS2: 0x2DFB40.
void GRuntimeInstance::ConnectToInstance(Attrib::Key key, int index, GRuntimeInstance *instance) {
    ConnectedInstance &entry = mConnected[mNumConnected++];
    entry.mIndexedKey = MakePackedKey(key, index);
    entry.mInstance = instance;
}

// PS2: 0x2DFB98. Sorts the table so GetConnectedInstance can binary search it.
void GRuntimeInstance::LockConnections() {
    std::sort(mConnected, mConnected + mNumConnected);
    mFlags |= kFlag_ConnectionsLocked;
}

// PS2: 0x2DFCD0.
void GRuntimeInstance::ResetConnections() {
    mFlags &= ~kFlag_ConnectionsLocked;
    mNumConnected = 0;
}

// PC: 0x5E2850.
GRuntimeInstance *GRuntimeInstance::GetConnectedInstance(const Attrib::Key &key, int index) const {
    uint32 packed = MakePackedKey(key, index);
    int lo = 0;
    int hi = static_cast<int>(mNumConnected) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32 midKey = mConnected[mid].mIndexedKey;
        if (midKey < packed) {
            lo = mid + 1;
        } else if (midKey > packed) {
            hi = mid - 1;
        } else {
            return mConnected[mid].mInstance;
        }
    }
    return nullptr;
}

// PC: 0x5E28B0.
bool GRuntimeInstance::IsDerivedFromTemplate(Attrib::Key templateKey) const {
    Attrib::Key parent = GetParent();
    while (parent != 0) {
        if (parent == templateKey) {
            return true;
        }
        Attrib::Gen::gameplay parentObj(parent, 0, nullptr);
        parent = parentObj.GetParent();
    }
    return false;
}

// PC: 0x5E2970. Only markers and triggers have a place in the world.
bool GRuntimeInstance::GetPosition(UMath::Vector3 &pos) {
    switch (GetType()) {
        case kGameplayObjType_Marker:
            pos = static_cast<GMarker *>(this)->GetPosition();
            return true;
        case kGameplayObjType_Trigger:
            static_cast<GTrigger *>(this)->GetPosition(pos);
            return true;
        default:
            return false;
    }
}

// PC: 0x5DACF0.
bool GRuntimeInstance::GetDirection(UMath::Vector3 &dir) {
    switch (GetType()) {
        case kGameplayObjType_Marker:
            dir = static_cast<GMarker *>(this)->GetDirection();
            return true;
        case kGameplayObjType_Trigger:
            dir = static_cast<GTrigger *>(this)->GetDirection();
            return true;
        default:
            return false;
    }
}

// PC: 0x5E1820 (activity) etc. A walk of the type's ring list.
template <typename T> T *GRuntimeInstance::FindObject(Attrib::Key key) {
    GRuntimeInstance *head = sRingListHead[T::kType];
    GRuntimeInstance *instance = head;
    while (instance != nullptr) {
        if (instance->GetCollection() == key) {
            return static_cast<T *>(instance);
        }
        instance = instance->mNext;
        if (instance == head) {
            break;
        }
    }
    return nullptr;
}

// PS2: 0x2DFAF8. For an instance made outside a vault's object block (the
// customized race's activity); freed with the instance.
void GRuntimeInstance::AllocateConnectionBuffer(unsigned int numEntries) {
    mConnected = new ConnectedInstance[numEntries > 0 ? numEntries : 1];
    mNumConnected = 0;
    mFlags |= kFlag_OwnsConnections;
}

template GActivity *GRuntimeInstance::FindObject<GActivity>(Attrib::Key key);
template GCharacter *GRuntimeInstance::FindObject<GCharacter>(Attrib::Key key);
template GMarker *GRuntimeInstance::FindObject<GMarker>(Attrib::Key key);
template GTrigger *GRuntimeInstance::FindObject<GTrigger>(Attrib::Key key);
template GState *GRuntimeInstance::FindObject<GState>(Attrib::Key key);
template GHandler *GRuntimeInstance::FindObject<GHandler>(Attrib::Key key);

// ---------------------------------------------------------------------------
// GState
// ---------------------------------------------------------------------------

// PS2: 0x2EEBB0.
GState::GState(const Attrib::Key &stateKey) : GRuntimeInstance(stateKey, kGameplayObjType_State) {}

GState::~GState() {}

GameplayObjType GState::GetType() const {
    return kGameplayObjType_State;
}

// PS2: inlined (size 0x48). Guess: a state that has nowhere to go ends its
// activity.
bool GState::IsTerminalState() const {
    return Num_transitionlist() == 0;
}

// ---------------------------------------------------------------------------
// GMarker
// ---------------------------------------------------------------------------

// PC: 0x5E8600. Position is in the vault's frame (x, y, z) -> sim (-y, z, x).
GMarker::GMarker(const unsigned int &markerKey) : GRuntimeInstance(markerKey, kGameplayObjType_Marker) {
    const UMath::Vector3 &pos = Position();
    mPosition.x = -pos.y;
    mPosition.y = pos.z;
    mPosition.z = pos.x;

    UMath::Matrix4 rotMat = UMath::Matrix4::kIdentity;
    UMath::Vector3 dir = {0.0f, 0.0f, 1.0f};
    UMath::MultYRot(rotMat, Rotation() * (-1.0f / 360.0f), rotMat);
    UMath::Rotate(dir, rotMat, dir);
    mDirection = dir;
}

GMarker::~GMarker() {}

// PS2: inlined.
void GMarker::CalcTransform(UMath::Matrix4 &mat) const {
    mat = UMath::Matrix4::kIdentity;
    UMath::MultYRot(mat, Rotation() * (-1.0f / 360.0f), mat);
    mat.v3.x = mPosition.x;
    mat.v3.y = mPosition.y;
    mat.v3.z = mPosition.z;
    mat.v3.w = 1.0f;
}

// ---------------------------------------------------------------------------
// UTL::Collections::_KeyedNode (PC: 0x5D51F0, built with the gameplay code).
// Instanceable lists stay sorted by handle (handles only grow), so a lookup
// is a lower_bound.
// ---------------------------------------------------------------------------

namespace UTL {
namespace Collections {

_KeyedNode *_KeyedNode::Search(_KeyedNode *begin_iter, _KeyedNode *end_iter, uintptr_t handle) {
    _KeyedNode *node = std::lower_bound(begin_iter, end_iter, _KeyedNode(handle, nullptr),
                                        [](const _KeyedNode &lhs, const _KeyedNode &rhs) { return lhs.Handle < rhs.Handle; });
    if (node == end_iter || node->Handle != handle) {
        return nullptr;
    }
    return node;
}

} // namespace Collections
} // namespace UTL
