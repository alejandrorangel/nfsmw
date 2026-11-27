#ifndef GAMEPLAY_GOBJECTBLOCK_H
#define GAMEPLAY_GOBJECTBLOCK_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include "Speed/Indep/Src/Gameplay/GRuntimeInstance.h"

class GVault;

// The runtime instances created for one loaded gameplay vault, one list per
// object type. On PC the objects and their connection tables are constructed in
// place in the vault's object buffer (sized by CalcSpaceRequired).
// total size: 0x50
class GObjectBlock {
  public:
    GObjectBlock(GVault *vault, uint8 *buffer);
    ~GObjectBlock();

    void Initialize(unsigned int bufferSize);

    static unsigned int CalcSpaceRequired(GVault *vault, unsigned int *outObjCount);
    static bool CollectionIsInstanceOfTemplate(const Attrib::Gen::gameplay &instanceObj, const Attrib::Gen::gameplay &templateObj);
    static unsigned int CalcNumConnections(Attrib::Key collectionKey);

  private:
    template <typename T> unsigned int CreateObjects(GVault *vault, uint8 *buffer);
    template <typename T> void DeleteObjects();

    GVault *mVault;                                       // offset 0x0, size 0x4
    uint8 *mObjectBuffer;                                 // offset 0x4, size 0x4
    unsigned int mObjectCount[kGameplayObjType_Count];    // offset 0x8, size 0x18
    unsigned int mObjectSize[kGameplayObjType_Count];     // offset 0x20, size 0x18
    GRuntimeInstance *mObjectList[kGameplayObjType_Count]; // offset 0x38, size 0x18
#if defined(EA_PORT_NATIVE)
    // The objects are heap allocated natively (their 64-bit sizes don't match
    // the PC's buffer layout); these are the pointer arrays.
    GRuntimeInstance **mObjects[kGameplayObjType_Count];
    ConnectedInstance *mConnectionBuffer;
#endif
};

#endif
