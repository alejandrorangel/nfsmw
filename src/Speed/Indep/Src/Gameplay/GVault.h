#ifndef GAMEPLAY_GVAULT_H
#define GAMEPLAY_GVAULT_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include <types.h>

#include "Speed/Indep/Src/Misc/AttribAlloc.h"

namespace Attrib {
class Vault;
};
struct AttribVaultPackEntry;
struct AttribVaultPackImage;
class GObjectBlock;
class GManager;

// Attribute allocator that counts what a vault allocates (PC vtable 0x8A397C).
// total size: 0x18
class LoggingAttribAllocator : public IAttribAllocator {
  public:
    unsigned int GetChecksum() const {
        return mChecksum;
    }

    unsigned int GetByteCount() const {
        return mAllocBytes;
    }

    virtual ~LoggingAttribAllocator() {}

    void *Allocate(std::size_t bytes, const char *name) override = 0;
    void Free(void *ptr, std::size_t bytes, const char *name) override = 0;

  protected:
    LoggingAttribAllocator() : mChecksum(0xea0ff1ce), mAllocCount(0), mAllocBytes(0), mFreeCount(0), mFreeBytes(0) {}

    void LogAlloc(std::size_t bytes) {
        mAllocCount++;
        mAllocBytes += (bytes + 0xf) & ~0xf;
    }

    void LogFree(std::size_t bytes) {
        mFreeCount++;
        mFreeBytes += (bytes + 0xf) & ~0xf;
    }

    unsigned int mChecksum;   // offset 0x4, size 0x4
    unsigned int mAllocCount; // offset 0x8, size 0x4
    unsigned int mAllocBytes; // offset 0xC, size 0x4
    unsigned int mFreeCount;  // offset 0x10, size 0x4
    unsigned int mFreeBytes;  // offset 0x14, size 0x4
};

// Allocates a transient vault's attribute data from a memory pool while it is
// preloaded to measure it (PC vtable 0x8A3A24).
// total size: 0x1C
class PreloadingAttribAllocator : public LoggingAttribAllocator {
  public:
    PreloadingAttribAllocator(int poolNum) : mPoolNum(poolNum) {}

    void *Allocate(std::size_t bytes, const char *name) override;
    void Free(void *ptr, std::size_t bytes, const char *name) override;

  private:
    int mPoolNum; // offset 0x18, size 0x4
};

// One vault of the gameplay vault pack (GLOBAL\GAMEPLAY.BIN).
// total size: 0x40
class GVault {
  public:
    enum Flags {
        kFlag_Resident = 1 << 0,
        kFlag_RaceBin = 1 << 1,
    };

    GVault(const AttribVaultPackEntry &packEntry, const char *vaultName);
    ~GVault() {}

    void LoadResident(AttribVaultPackImage *packImage);
    void PreloadTransient(AttribVaultPackImage *packImage, int poolNum);
    void LoadSyncTransient();
    void Unload();
    void CreateGameplayObjects();
    void DestroyGameplayObjects();
    unsigned int InitTransient(uint8 *binBlock, uint8 *vltBlock);

    const char *GetName() const {
        return mVaultName;
    }

    Attrib::Vault *GetAttribVault() const {
        return mVault;
    }

    GObjectBlock *GetObjectBlock() const {
        return mGameObjBlock;
    }

    unsigned int GetObjectCount() const {
        return mGameObjCount;
    }

    unsigned int GetFootprint() const {
        return mGameObjSize + mAttribObjSize + mBinSize;
    }

    unsigned int GetDataOffset() const {
        return mBinOffset;
    }

    unsigned int GetDataSize() const {
        return mBinSize;
    }

    unsigned int GetLoadDataOffset() const {
        return mVltOffset;
    }

    unsigned int GetLoadDataSize() const {
        return mVltSize;
    }

    bool IsLoaded() const {
        return mVault != nullptr;
    }

    bool IsResident() const {
        return (mFlags & kFlag_Resident) != 0;
    }

    bool IsTransient() const {
        return (mFlags & kFlag_Resident) == 0;
    }

    bool IsRaceBin() const {
        return (mFlags & kFlag_RaceBin) != 0;
    }

    void SetRaceBin() {
        mFlags |= kFlag_RaceBin;
    }

  private:
    friend class GManager;

    Attrib::Vault *mVault;                     // offset 0x0, size 0x4
    const char *mVaultName;                    // offset 0x4, size 0x4
    unsigned int mFlags;                       // offset 0x8, size 0x4
    uint8 *mBinResidentData;                   // offset 0xC, size 0x4
    unsigned int mBinOffset;                   // offset 0x10, size 0x4
    unsigned int mBinSize;                     // offset 0x14, size 0x4
    unsigned int mVltOffset;                   // offset 0x18, size 0x4
    unsigned int mVltSize;                     // offset 0x1C, size 0x4
    LoggingAttribAllocator *mAttribAllocator;  // offset 0x20, size 0x4
    unsigned int mAttribObjSize;               // offset 0x24, size 0x4
    unsigned int mAttribAllocChecksum;         // offset 0x28, size 0x4
    uint8 *mAttribTransientData;               // offset 0x2C, size 0x4
    unsigned int mGameObjSize;                 // offset 0x30, size 0x4
    unsigned int mGameObjCount;                // offset 0x34, size 0x4
    GObjectBlock *mGameObjBlock;               // offset 0x38, size 0x4
    uint8 *mGameObjData;                       // offset 0x3C, size 0x4
};

#endif
