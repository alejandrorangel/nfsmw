#include "Speed/Indep/Src/Gameplay/GManager.h"

#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GVault.h"
#include "Speed/Indep/Src/Misc/LZCompress.hpp"
#include "Speed/Indep/Src/Misc/bFile.hpp"
#include "Speed/Indep/bWare/Inc/bMemory.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include <algorithm>

// Only the vault side of GManager is implemented here so far: loading the
// gameplay vault pack, the vault table and vault lookup, which the race
// database is built from. The runtime-instance side (GObjectBlock, the
// instance map, milestones, speed traps, icons, timers, SMS) isn't ported yet.

GManager *GManager::mObj = nullptr;

// PC: 0x5FA430. The PC build also runs an opaque check here (0x5F8xxx helpers,
// result compared against 0x9F0F7E84) that can skip creating the manager; it
// isn't gameplay logic and is left out.
void GManager::Init(const char *vaultPackName) {
    mObj = new GManager(vaultPackName);
    mObj->InitializeVaults();
}

// PC: 0x5F7D80.
GManager::GManager(const char *vaultPackName)
    : UTL::COM::Object(1),                      //
      IVehicleCache(this),                      //
      mVaultPackFileName(vaultPackName),        //
      mVaultPackFile(nullptr),                  //
      mVaultCount(0),                           //
      mVaults(nullptr),                         //
      mVaultNameStrings(nullptr),               //
      mLoadingPackImage(nullptr),               //
      mBinSlotSize(0),                          //
      mStreamedBinSlots(nullptr),               //
      mRaceSlotSize(0),                         //
      mStreamedRaceSlots(nullptr),              //
      mTempLoadData(nullptr),                   //
      mTransientPoolNumber(0),                  //
      mTransientPoolMemory(nullptr),            //
      mGameplayClass(nullptr),                  //
      mMilestoneClass(nullptr),                 //
      mAttributeKeyShiftTo24(0),                //
      mCollectionKeyShiftTo32(0),               //
      mMaxObjects(0),                           //
      mClassTempBuffer(nullptr),                //
      mInstanceHashTableSize(0),                //
      mInstanceHashTableMask(0),                //
      mWorstHashCollision(0),                   //
      mKeyToInstanceMap(nullptr),               //
      mNumMilestones(0),                        //
      mMilestones(nullptr),                     //
      mNumSpeedTraps(0),                        //
      mSpeedTraps(nullptr),                     //
      mNumBountySpawnPoints(0),                 //
      mFreeRoamStartMarker(0),                  //
      mFreeRoamFromSafeHouseStartMarker(0),     //
      mStartFreeRoamFromSafeHouse(false),       //
      mStartFreeRoamPursuit(false),             //
      mQueuedPursuitMinHeat(0.0f),              //
      mInGameplay(false),                       //
      mOverrideFreeRoamStartMarker(0),          //
      mObjectStateBuffer(nullptr),              //
      mObjectStateBufferFree(nullptr),          //
      mObjectStateBufferSize(0),                //
      mLastCouldSendTime(0.0f),                 //
      mWarping(false),                          //
      mWarpStartPursuit(false),                 //
      mWarpTargetMarker(0),                     //
      mNumIcons(0),                             //
      mNumVisibleIcons(0),                      //
      mIcons(nullptr),                          //
      mPursuitBreakerIconsShown(false),         //
      mHidingSpotIconsShown(false),             //
      mEventIconsShown(false),                  //
      mMenuGateIconsShown(false),               //
      mSpeedTrapIconsShown(false),              //
      mSpeedTrapRaceIconsShown(false),          //
      mAllowEngageEvents(false),                //
      mAllowEngageSafehouse(false),             //
      mAllowMenuGates(false),                   //
      mRestartEventHash(0) {
    mBinVaultInSlot[0] = nullptr;
    mRaceVaultInSlot[0] = nullptr;
    mGameplayClass = Attrib::Database::Get().GetClass(Attrib::Gen::gameplay::ClassKey());
    mMilestoneClass = Attrib::Database::Get().GetClass(0xe4c3d904);
    bMemSet(mHidingSpotFound, 0, sizeof(mHidingSpotFound));
    bMemSet(mBountySpawnPoint, 0, sizeof(mBountySpawnPoint));
    mActiveCharacters.reserve(16);
    mObjectStateBuffer = static_cast<uint8 *>(bMalloc(0x4000, 0));
    mObjectStateBufferFree = mObjectStateBuffer;
    mObjectStateBufferSize = 0x4000;
    ResetAllGameplayData();
}

bool GManager::GetInGameplay() const {
    return mInGameplay;
}

// PC: 0x5F8080.
void GManager::InitializeVaults() {
    char packName[128];
    bStrCpy(packName, mVaultPackFileName);
    bStrCpy(bStrIStr(packName, ".BIN"), ".LZC");

    uint8 *compressed = static_cast<uint8 *>(bGetFile(packName, nullptr, 0));
    uint32 uncompressedSize = reinterpret_cast<uint32 *>(compressed)[2];
    uint8 *packData = static_cast<uint8 *>(bMalloc(uncompressedSize, 0x1040));
    LZDecompress(compressed, packData);
    bFree(compressed);
    mLoadingPackImage = reinterpret_cast<AttribVaultPackImage *>(packData);

    std::size_t tableBytes = mGameplayClass->GetTableNodeSize() << 14;
    mClassTempBuffer = bMalloc(static_cast<int>(tableBytes), 0x40);
    mGameplayClass->SetTableBuffer(mClassTempBuffer, tableBytes);

    BuildVaultTable(mLoadingPackImage);
    LoadCoreVault(mLoadingPackImage);
    PreloadTransientVaults(mLoadingPackImage);
    FindKeyReductionShifts();

    // The PC build then sets up the icons (0x320 bytes from FastMem), the
    // milestones, speed traps and bounty spawn points and the section icon
    // callback; those parts of GManager aren't ported yet.
}

// PC: 0x5E3D80.
void GManager::BuildVaultTable(AttribVaultPackImage *packImage) {
    unsigned int stringBytes = packImage->mHeader.mStringBlockSize;
    char *strings = static_cast<char *>(gFastMem.Alloc(stringBytes, nullptr));
    bMemCpy(strings, packImage->GetData(packImage->mHeader.mStringBlockOffset), stringBytes);
    mVaultNameStrings = strings;

    mVaultCount = packImage->mHeader.mNumEntries;
    mVaults = static_cast<GVault *>(bMalloc(mVaultCount * sizeof(GVault), 0x1000));
    for (unsigned int onVault = 0; onVault < mVaultCount; onVault++) {
        const AttribVaultPackEntry &entry = packImage->GetEntry(onVault);
        new (&mVaults[onVault]) GVault(entry, strings + entry.mVaultNameOffset);
    }
}

// PC: inlined in InitializeVaults (0x5F8122).
void GManager::LoadCoreVault(AttribVaultPackImage *packImage) {
    FindVault(GMANAGER_COREVAULT)->LoadResident(packImage);
}

// PC: 0x5F7390.
void GManager::PreloadTransientVaults(AttribVaultPackImage *packImage) {
    unsigned int poolBytes = 0x164000;
#if defined(EA_PORT_NATIVE)
    // The attribute runtime's own structures are larger with 64-bit pointers.
    poolBytes *= 2;
#endif
    mTransientPoolMemory = bMalloc(poolBytes, 0x47);
    mTransientPoolNumber = bGetFreeMemoryPoolNum();
    bInitMemoryPool(mTransientPoolNumber, mTransientPoolMemory, poolBytes, "GManager Temp");
    bSetMemoryPoolDebugTracing(mTransientPoolNumber, false);
    for (unsigned int onVault = 0; onVault < mVaultCount; onVault++) {
        if (!mVaults[onVault].IsLoaded()) {
            mVaults[onVault].PreloadTransient(packImage, mTransientPoolNumber);
        }
    }
}

// PC: 0x5E3E60. The pack's vaults are sorted by name.
GVault *GManager::FindVault(const char *vaultName) {
    int lo = 0;
    int hi = static_cast<int>(mVaultCount) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int cmp = bStrCmp(vaultName, mVaults[mid].GetName());
        if (cmp > 0) {
            lo = mid + 1;
        } else if (cmp < 0) {
            hi = mid - 1;
        } else {
            return &mVaults[mid];
        }
    }
    return nullptr;
}

// PC: 0x5E3EC0.
GVault *GManager::FindVaultContaining(Attrib::Key collectionKey) {
    Attrib::Key loadDataType = Attrib::StringHash32("Attrib::CollectionLoadData");
    for (unsigned int onVault = 0; onVault < mVaultCount; onVault++) {
        GVault &vault = mVaults[onVault];
        Attrib::Vault *attribVault = vault.GetAttribVault();
        if (attribVault == nullptr) {
            continue;
        }
        unsigned int numExports = attribVault->CountExports();
        for (unsigned int onExport = 0; onExport < numExports; onExport++) {
            if (attribVault->GetExportType(onExport) != loadDataType) {
                continue;
            }
            const Attrib::Collection *collection = static_cast<const Attrib::Collection *>(attribVault->GetExportData(onExport));
            if (collection != nullptr && Attrib::GetCollectionKey(collection) == collectionKey) {
                return &vault;
            }
        }
    }
    return nullptr;
}

// PC: 0x5FA780. Streams a transient vault into its slot (race bins and races
// have one slot each).
void GManager::LoadVaultSync(GVault *vault) {
#if defined(EA_PORT_NATIVE)
    // InitializeRaceStreaming (which allocates the stream slots and unloads the
    // preloaded transient vaults) isn't ported, so every vault stays loaded from
    // the pack image; reload from it if one ever isn't.
    if (mStreamedBinSlots == nullptr || mStreamedRaceSlots == nullptr) {
        if (!vault->IsLoaded() && mLoadingPackImage != nullptr) {
            vault->PreloadTransient(mLoadingPackImage, mTransientPoolNumber);
            GRaceDatabase::Get().NotifyVaultLoaded(vault);
        }
        return;
    }
#endif
    uint8 *binBlock;
    if (vault->IsRaceBin()) {
        if (mRaceVaultInSlot[0] != nullptr) {
            mRaceVaultInSlot[0]->Unload();
            mRaceVaultInSlot[0] = nullptr;
        }
        if (mBinVaultInSlot[0] != nullptr) {
            mBinVaultInSlot[0]->Unload();
            mBinVaultInSlot[0] = nullptr;
        }
        binBlock = mStreamedBinSlots;
        mBinVaultInSlot[0] = vault;
    } else {
        if (mRaceVaultInSlot[0] != nullptr) {
            mRaceVaultInSlot[0]->Unload();
            mRaceVaultInSlot[0] = nullptr;
        }
        binBlock = mStreamedRaceSlots;
        mRaceVaultInSlot[0] = vault;
    }

    mVaultPackFile = bOpen(mVaultPackFileName, 1, 1);
    bSeek(mVaultPackFile, vault->GetDataOffset(), 0);
    bRead(mVaultPackFile, binBlock, vault->GetDataSize());
    bSeek(mVaultPackFile, vault->GetLoadDataOffset(), 0);
    bRead(mVaultPackFile, mTempLoadData, vault->GetLoadDataSize());
    bClose(mVaultPackFile);
    mVaultPackFile = nullptr;

    vault->InitTransient(binBlock, mTempLoadData);
    // PC: if (mInGameplay) ConnectRuntimeInstances(); -- runtime instances
    // aren't ported yet.
}

// PC: 0x5F1780. The smallest right shift that keeps the top uniqueBits of every
// key distinct (the keys are masked down as the shift drops).
unsigned int GManager::FindUniqueKeyShift(Attrib::Key *keys, unsigned int numKeys, unsigned int uniqueBits) {
    if (numKeys < 2) {
        return 0;
    }
    unsigned int mask = ((1u << uniqueBits) - 1) << (32 - uniqueBits);
    unsigned int keep = 0xffffffff;
    for (int shift = 32 - static_cast<int>(uniqueBits); shift >= 0; shift--) {
        for (unsigned int i = 0; i < numKeys; i++) {
            keys[i] &= keep;
        }
        std::sort(keys, keys + numKeys);
        bool unique = true;
        for (unsigned int i = 1; i < numKeys; i++) {
            if ((keys[i - 1] & mask) == (keys[i] & mask)) {
                unique = false;
                break;
            }
        }
        if (unique) {
            return static_cast<unsigned int>(shift);
        }
        keep >>= 1;
        mask >>= 1;
    }
    return 0;
}

// PC: 0x5F2F60.
void GManager::FindKeyReductionShifts() {
    unsigned int numCollections = mGameplayClass->GetNumCollections();
    unsigned int numKeys = numCollections + static_cast<unsigned int>(mGameplayClass->GetNumDefinitions());
    if (numKeys < numCollections) {
        numKeys = numCollections;
    }
    Attrib::Key *keys = new Attrib::Key[numKeys > 0 ? numKeys : 1];
    Attrib::Key *out = keys;

    mCollectionKeyShiftTo32 = 0;
    for (Attrib::Key def = mGameplayClass->GetFirstDefinition(); def != 0; def = mGameplayClass->GetNextDefinition(def)) {
        *out++ = def;
    }
    for (Attrib::Key key = mGameplayClass->GetFirstCollection(); key != 0; key = mGameplayClass->GetNextCollection(key)) {
        Attrib::Gen::gameplay collection(key, 0, nullptr);
        const char *name = collection.CollectionName();
        for (const char *p = name + bStrLen(name) - 1; p >= name; p--) {
            if (*p == '/' || *p == '\\') {
                name = p + 1;
                break;
            }
        }
        *out++ = Attrib::StringToKey(name);
    }

    std::sort(keys, out);
    Attrib::Key *end = std::unique(keys, out);
    mAttributeKeyShiftTo24 = FindUniqueKeyShift(keys, static_cast<unsigned int>(end - keys), 24);
    delete[] keys;
}

// PC: 0x5F3160. The PC build also keeps the vehicles of active characters
// (mActiveCharacters); characters aren't ported yet, so only the stock cars are
// checked here.
eVehicleCacheResult GManager::OnQueryVehicleCache(const IVehicle *removethis, const IVehicleCache *whosasking) const {
    for (StockCarMap::const_iterator iter = mStockCars.begin(); iter != mStockCars.end(); ++iter) {
        if (UTL::COM::ComparePtr(iter->second, removethis)) {
            return VCR_WANT;
        }
    }
    return VCR_DONTCARE;
}

// PC: 0x5F32B0.
void GManager::OnRemovedVehicleCache(IVehicle *ivehicle) {
    for (StockCarMap::iterator iter = mStockCars.begin(); iter != mStockCars.end(); ++iter) {
        if (UTL::COM::ComparePtr(iter->second, ivehicle)) {
            mStockCars.erase(iter);
            return;
        }
    }
}
