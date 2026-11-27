#include "Speed/Indep/Src/Gameplay/GVault.h"

#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GObjectBlock.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Misc/AttribAsset.h"
#include "Speed/Indep/Src/Misc/AttribVaultPack.h"
#include "Speed/Indep/Tools/AttribSys/Runtime/AttribLoadAndGo.h"
#include "Speed/Indep/bWare/Inc/bMemory.hpp"
#include "Speed/Indep/bWare/Inc/bPrintf.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

// PC: 0x5E50B0 / 0x5E50E0.
void *PreloadingAttribAllocator::Allocate(std::size_t bytes, const char *name) {
    LogAlloc(bytes);
    return bMalloc(static_cast<int>((bytes + 0xf) & ~0xf), mPoolNum & 0xf);
}

void PreloadingAttribAllocator::Free(void *ptr, std::size_t bytes, const char *name) {
    LogFree(bytes);
    bFree(ptr);
}

// Bump allocator over a streamed vault's preallocated attribute block
// (PC vtable 0x8A3A30: 0x5E5190 / 0x5E51C0).
// total size: 0x24
class TransientAttribAllocator : public LoggingAttribAllocator {
  public:
    TransientAttribAllocator(uint8 *data, unsigned int size, unsigned int checksum) : mData(data), mRemaining(size), mExpectedChecksum(checksum) {}

    void *Allocate(std::size_t bytes, const char *name) override {
        std::size_t aligned = (bytes + 0xf) & ~0xf;
        LogAlloc(bytes);
        void *result = mData;
        mData += aligned;
        mRemaining -= aligned;
        return result;
    }

    void Free(void *ptr, std::size_t bytes, const char *name) override {
        std::size_t aligned = (bytes + 0xf) & ~0xf;
        LogFree(bytes);
        mRemaining += aligned;
    }

  private:
    uint8 *mData;                   // offset 0x18, size 0x4
    unsigned int mRemaining;        // offset 0x1C, size 0x4
    unsigned int mExpectedChecksum; // offset 0x20, size 0x4
};

// PC: inlined in GManager::BuildVaultTable (0x5E3D80).
GVault::GVault(const AttribVaultPackEntry &packEntry, const char *vaultName)
    : mVault(nullptr),                                //
      mVaultName(vaultName),                          //
      mFlags(0),                                      //
      mBinResidentData(nullptr),                      //
      mBinOffset(packEntry.mBinOffset),               //
      mBinSize((packEntry.mBinSize + 0xf) & ~0xfu),   //
      mVltOffset(packEntry.mVltOffset),               //
      mVltSize((packEntry.mVltSize + 0xf) & ~0xfu),   //
      mAttribAllocator(nullptr),                      //
      mAttribObjSize(0),                              //
      mAttribAllocChecksum(0),                        //
      mAttribTransientData(nullptr),                  //
      mGameObjSize(0),                                //
      mGameObjCount(0),                               //
      mGameObjBlock(nullptr),                         //
      mGameObjData(nullptr) {}

static unsigned int CalcGameObjectSpace(GVault *vault, unsigned int *outObjCount) {
    return GObjectBlock::CalcSpaceRequired(vault, outObjCount);
}

// PC: 0x5F65F0.
void GVault::LoadResident(AttribVaultPackImage *packImage) {
    uint8 *binData = packImage->GetData(mBinOffset);
    uint8 *vltData = packImage->GetData(mVltOffset);
    char binName[128];
    char vltName[128];
    char allocName[128];

    bSPrintf(binName, "%s.bin", mVaultName);
    bSPrintf(vltName, "%s.vlt", mVaultName);
    bSPrintf(allocName, "Gameplay resident data: %s", binName);

    mBinResidentData = static_cast<uint8 *>(bMalloc(mBinSize, 0));
    bMemCpy(mBinResidentData, binData, mBinSize);
    AddDepFile(binName, mBinResidentData, mBinSize);
    mVault = AddVault(vltName, vltData, mVltSize);

    unsigned int objSpace = CalcGameObjectSpace(this, &mGameObjCount);
    mGameObjSize = (objSpace + 0x5f) & ~0xfu;
    mGameObjData = static_cast<uint8 *>(bMalloc(mGameObjSize, 0x400));
    mFlags |= kFlag_Resident;
}

// PC: 0x5F66D0. Loads a transient vault straight out of the pack image so the
// race database can index it; the attribute allocations are measured so the
// vault can later be streamed into a fixed-size slot.
void GVault::PreloadTransient(AttribVaultPackImage *packImage, int poolNum) {
    uint8 *binData = packImage->GetData(mBinOffset);
    uint8 *vltData = packImage->GetData(mVltOffset);
    char binName[128];
    char vltName[128];

    bSPrintf(binName, "%s.bin", mVaultName);
    bSPrintf(vltName, "%s.vlt", mVaultName);

    PreloadingAttribAllocator *allocator = new PreloadingAttribAllocator(poolNum);
    IAttribAllocator *oldAllocator = AttribAlloc::OverrideAllocator(allocator);
    AddDepFile(binName, binData, mBinSize);
    mVault = AddVault(vltName, vltData, mVltSize);
    mAttribAllocator = allocator;
    mAttribAllocChecksum = allocator->GetChecksum();
    mAttribObjSize = allocator->GetByteCount();
    AttribAlloc::OverrideAllocator(oldAllocator);

    unsigned int objSpace = CalcGameObjectSpace(this, &mGameObjCount);
    mGameObjSize = (objSpace + 0x5f) & ~0xfu;
}

// PC: 0x5FA5E0. binBlock/vltBlock hold the vault's data read into a stream slot.
unsigned int GVault::InitTransient(uint8 *binBlock, uint8 *vltBlock) {
    char binName[128];
    char vltName[128];

    bSPrintf(binName, "%s.bin", mVaultName);
    bSPrintf(vltName, "%s.vlt", mVaultName);

    mAttribTransientData = binBlock + mBinSize;
    mGameObjData = mAttribTransientData + mAttribObjSize;

    TransientAttribAllocator *allocator = new TransientAttribAllocator(mAttribTransientData, mAttribObjSize, mAttribAllocChecksum);
    IAttribAllocator *oldAllocator = AttribAlloc::OverrideAllocator(allocator);
    AddDepFile(binName, binBlock, mBinSize);
    mVault = AddVault(vltName, vltBlock, mVltSize);
    mAttribAllocator = allocator;
    AttribAlloc::OverrideAllocator(oldAllocator);

    if (GManager::Get().GetInGameplay()) {
        CreateGameplayObjects();
    }
    GRaceDatabase::Get().NotifyVaultLoaded(this);
    return mGameObjSize + mBinSize + mAttribObjSize;
}

// PC: 0x5FA860.
void GVault::LoadSyncTransient() {
    GManager::Get().LoadVaultSync(this);
}

// PC: 0x5FA520. The block is built in place at the start of the vault's
// object data on PC; natively it (and its objects) are heap allocated.
void GVault::CreateGameplayObjects() {
#if defined(EA_PORT_NATIVE)
    mGameObjBlock = new GObjectBlock(this, mGameObjData);
#else
    mGameObjBlock = new (mGameObjData) GObjectBlock(this, mGameObjData + sizeof(GObjectBlock));
#endif
    mGameObjBlock->Initialize(mGameObjSize);
}

// PC: 0x5EE090.
void GVault::DestroyGameplayObjects() {
#if defined(EA_PORT_NATIVE)
    delete mGameObjBlock;
#else
    mGameObjBlock->~GObjectBlock();
#endif
    mGameObjBlock = nullptr;
}

// PC: 0x5EE0F0.
void GVault::Unload() {
    if (GRaceDatabase::Exists()) {
        GRaceDatabase::Get().NotifyVaultUnloading(this);
    }

    IAttribAllocator *oldAllocator = nullptr;
    if (IsTransient()) {
        oldAllocator = AttribAlloc::OverrideAllocator(mAttribAllocator);
    }
    if (mGameObjBlock != nullptr) {
        DestroyGameplayObjects();
    }
    if (IsResident()) {
        bFree(mGameObjData);
    }
    mGameObjData = nullptr;

    mVault->Deinitialize();
    mVault->Release();
    mVault = nullptr;

    char name[128];
    bSPrintf(name, "%s.bin", mVaultName);
    RemoveDepFile(name);
    bSPrintf(name, "%s.vlt", mVaultName);
    RemoveVault(name);

    if (oldAllocator != nullptr) {
        AttribAlloc::OverrideAllocator(oldAllocator);
    }
    if (IsTransient()) {
        if (mAttribAllocator != nullptr) {
            delete mAttribAllocator;
        }
        mAttribAllocator = nullptr;
    }
    if (IsResident()) {
        bFree(mBinResidentData);
        mBinResidentData = nullptr;
        mFlags &= ~kFlag_Resident;
    }
}
