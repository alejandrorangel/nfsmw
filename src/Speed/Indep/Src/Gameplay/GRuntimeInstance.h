#ifndef GAMEPLAY_GRUNTIMEINSTANCE_H
#define GAMEPLAY_GRUNTIMEINSTANCE_H

#include "GUserIncludes.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/gameplay.h"

class GRuntimeInstance;

// One reference from a runtime instance to another: the 24-bit reduced key of
// the attribute (or child name) it came from, shifted up by 8, with the array
// index in the low byte. Kept sorted so lookups can binary search.
// total size: 0x8
struct ConnectedInstance {
    uint32 mIndexedKey;           // offset 0x0, size 0x4
    GRuntimeInstance *mInstance;  // offset 0x4, size 0x4

    bool operator<(const ConnectedInstance &rhs) const {
        return mIndexedKey < rhs.mIndexedKey;
    }
};

// A gameplay vault collection brought to life (activity, state, handler,
// character, marker or trigger). Every instance is in its type's ring list and
// in GManager's key -> instance map.
// total size: 0x28
class GRuntimeInstance : public Attrib::Gen::gameplay {
  public:
    enum Flags {
        kFlag_Registered = 1 << 0,
        kFlag_ConnectionsLocked = 1 << 1,
        kFlag_OwnsConnections = 1 << 2,
        DEFINE_GRUNTIMINSTANCE_CUSTOM_FLAGS
    };

    GRuntimeInstance(const Attrib::Key &key, GameplayObjType type);
    virtual ~GRuntimeInstance();

    virtual GameplayObjType GetType() const; // Decl: 47

    static GRuntimeInstance *GetRingListHead(GameplayObjType type) {
        return sRingListHead[type];
    }

    GRuntimeInstance *GetNextInRing() const {
        return mNext;
    }

    void SetConnectionBuffer(ConnectedInstance *destBuffer, unsigned int numEntries);
    void AllocateConnectionBuffer(unsigned int numEntries);
    void ConnectToInstance(Attrib::Key key, int index, GRuntimeInstance *instance);
    void LockConnections();
    void ResetConnections();
    GRuntimeInstance *GetConnectedInstance(const Attrib::Key &key, int index) const;
    bool GetPosition(UMath::Vector3 &pos);
    bool GetDirection(UMath::Vector3 &dir);
    bool IsDerivedFromTemplate(Attrib::Key templateKey) const;

    unsigned int GetConnectionCount() const {
        return mNumConnected;
    }

    GRuntimeInstance *GetConnectionAt(unsigned int index) const {
        return mConnected[index].mInstance;
    }

    bool GetFlag(unsigned int flag) const {
        return (mFlags & flag) != 0;
    }

    void SetFlag(unsigned int flag) {
        mFlags |= flag;
    }

    void ClearFlag(unsigned int flag) {
        mFlags &= ~flag;
    }

    unsigned int GetFlags() const {
        return mFlags;
    }

    template <typename T> static T *FindObject(Attrib::Key key);

  protected:
    static uint32 MakePackedKey(Attrib::Key key, int index);

  private:
    void AddToTypeList(GameplayObjType type);
    void RemoveFromTypeList();

    static GRuntimeInstance *sRingListHead[kGameplayObjType_Count];

    uint16 mFlags;                  // offset 0x14, size 0x2
    uint16 mNumConnected;           // offset 0x16, size 0x2
    ConnectedInstance *mConnected;  // offset 0x18, size 0x4
    GRuntimeInstance *mPrev;        // offset 0x1C, size 0x4
    GRuntimeInstance *mNext;        // offset 0x20, size 0x4
#if defined(EA_PORT_NATIVE)
    GameplayObjType mType;
#endif
};

// Walks one type's ring list, skipping instances that have none of the flags
// in the mask (all of them with the default mask).
// total size: 0x8
template <typename T> class GObjectIterator {
  public:
    GObjectIterator(unsigned int flagMask) : mInstance(GRuntimeInstance::GetRingListHead(static_cast<GameplayObjType>(T::kType))), mFlagMask(flagMask) {
        if (mInstance != nullptr && (mInstance->GetFlags() & mFlagMask) == 0) {
            Advance();
        }
    }

    void Advance() {
        GRuntimeInstance *head = GRuntimeInstance::GetRingListHead(static_cast<GameplayObjType>(T::kType));
        mInstance = mInstance->GetNextInRing();
        while (mInstance != head) {
            if ((mInstance->GetFlags() & mFlagMask) != 0) {
                return;
            }
            mInstance = mInstance->GetNextInRing();
        }
        mInstance = nullptr;
    }

    bool IsValid() const {
        return mInstance != nullptr;
    }

    T *GetInstance() const {
        return static_cast<T *>(mInstance);
    }

  private:
    GRuntimeInstance *mInstance; // offset 0x0, size 0x4
    unsigned int mFlagMask;      // offset 0x4, size 0x4
};

#endif
