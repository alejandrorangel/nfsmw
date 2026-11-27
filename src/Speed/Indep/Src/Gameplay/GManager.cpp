#include "Speed/Indep/Src/Gameplay/GManager.h"

#include "Speed/Indep/Src/Gameplay/GActivity.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Gameplay/GTrigger.h"
#if defined(EA_PORT_NATIVE)
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/uiWorldMap.hpp"
#include "Speed/Indep/Src/Interfaces/SimActivities/INIS.h"
#include "Speed/Indep/Src/World/TrackPath.hpp"
#include "Speed/Indep/Src/World/TrackStreamer.hpp"
#include "Speed/Indep/Src/World/WCollisionAssets.h"
#endif
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Gameplay/GVault.h"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Generated/Messages/MEnteringGameplay.h"
#include "Speed/Indep/Src/Misc/Config.h"
#include "Speed/Indep/Src/Misc/LZCompress.hpp"
#include "Speed/Indep/Src/Misc/bFile.hpp"
#include "Speed/Indep/bWare/Inc/bMemory.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include <algorithm>
#if defined(EA_PORT_NATIVE)
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Generated/Events/EAutoSave.hpp"
#include "Speed/Indep/Src/Generated/Events/ECellCall.hpp"
#include "Speed/Indep/Src/Generated/Events/EFadeScreenOff.hpp"
#include "Speed/Indep/Src/Generated/Events/EFadeScreenOn.hpp"
#include "Speed/Indep/Src/Gameplay/GMarker.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/ICopMgr.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/ITrafficMgr.h"
#include "Speed/Indep/Src/World/TrackInfo.hpp"
#include "Speed/Indep/bWare/Inc/bPrintf.hpp"

void World_RestoreProps();
void GPS_Disengage();
#include "Speed/Indep/Src/Generated/Events/EShowSMS.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/PhotoFinish.hpp"
#include "Speed/Indep/Src/Interfaces/IFengHud.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#include "Speed/Indep/Src/EAXSound/Stream/SpeechManager.hpp"
#include "Speed/Indep/Src/Generated/Messages/MNotifyMilestoneProgress.h"
#include "Speed/Indep/Src/Sim/Simulation.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/milestonetypes.h"
#include "Speed/Indep/Src/Misc/MD5.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

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
    // callback.
#if defined(EA_PORT_NATIVE)
    AllocateIcons();
    AllocateMilestones();
    AllocateSpeedTraps();
    FindBountySpawnPoints();
    // 0x5F8189: the zone icons, then the track marker ones (sub_5EF6D0).
    RefreshZoneIcons();
    RefreshTrackMarkerIcons();
#endif
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
        // The slots' bookkeeping as on PC: a vault streamed in during
        // gameplay (a race engaged from free roam) gets its objects and
        // the one it replaces in the slot loses its own.
        if (vault->IsTransient() && vault->IsLoaded()) {
            GVault *&slot = vault->IsRaceBin() ? mBinVaultInSlot[0] : mRaceVaultInSlot[0];
            if (slot != vault) {
                if (mRaceVaultInSlot[0] != nullptr && mRaceVaultInSlot[0]->GetObjectBlock() != nullptr) {
                    mRaceVaultInSlot[0]->DestroyGameplayObjects();
                }
                mRaceVaultInSlot[0] = nullptr;
                if (vault->IsRaceBin() && slot != nullptr && slot->GetObjectBlock() != nullptr) {
                    slot->DestroyGameplayObjects();
                }
                slot = vault;
            }
            if (mInGameplay && vault->GetObjectBlock() == nullptr) {
                vault->CreateGameplayObjects();
                ConnectRuntimeInstances();
            }
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
    if (mInGameplay) {
        ConnectRuntimeInstances();
    }
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

// PC: 0x5EBF30.
bool GManager::StockCarsLoaded() {
    for (StockCarMap::iterator iter = mStockCars.begin(); iter != mStockCars.end(); ++iter) {
        IVehicle *ivehicle;
        if (iter->second->QueryInterface(&ivehicle) && ivehicle->IsLoading()) {
            return false;
        }
    }
    return true;
}

// PC: 0x6035F0. A queued restart event becomes the startup race.
void GManager::PreBeginGameplay() {
    if (mRestartEventHash == 0) {
        return;
    }
    GRaceDatabase &raceDatabase = GRaceDatabase::Get();
    GRaceCustom *custom = raceDatabase.AllocCustomRace(raceDatabase.GetRaceFromHash(mRestartEventHash));
    raceDatabase.SetStartupRace(custom, GRace::kRaceContext_Career);
    if (custom != nullptr) {
        raceDatabase.FreeCustomRace(custom);
    }
    mRestartEventHash = 0;
}

// PC: 0x603500.
void GManager::BeginGameplay() {
#if defined(EA_PORT_NATIVE)
    // InitializeRaceStreaming normally sized the instance map first.
    if (mKeyToInstanceMap == nullptr) {
        AllocateInstanceMap();
    }
#endif
#if defined(EA_PORT_NATIVE)
    // Natively every vault stays loaded (no race streaming, see
    // LoadVaultSync); on PC only the resident vaults and the two stream
    // slots' are: the current bin's and the startup race's. Without this
    // every race's triggers (and their icons, 200 at most) exist in free roam.
    GRaceCustom *slotRace = GRaceDatabase::Get().GetStartupRace();
    GRaceBin *slotBin = GRaceStatus::Get().GetCurrentBin();
    for (unsigned int onVault = 0; onVault < mVaultCount; onVault++) {
        GVault *vault = &mVaults[onVault];
        const bool inSlot = (slotBin != nullptr && vault == slotBin->GetChildVault()) ||
                            (slotRace != nullptr && (vault == slotRace->GetChildVault() || vault == slotRace->GetParentVault()));
        if (vault->IsLoaded() && (vault->IsResident() || inSlot)) {
            vault->CreateGameplayObjects();
        }
    }
    mBinVaultInSlot[0] = slotBin != nullptr ? slotBin->GetChildVault() : nullptr;
    mRaceVaultInSlot[0] = slotRace != nullptr ? slotRace->GetChildVault() : nullptr;
#else
    for (unsigned int onVault = 0; onVault < mVaultCount; onVault++) {
        if (mVaults[onVault].IsLoaded()) {
            mVaults[onVault].CreateGameplayObjects();
        }
    }
#endif
    GRaceCustom *startupRace = GRaceDatabase::Get().GetStartupRace();
    if (startupRace != nullptr) {
        startupRace->CreateRaceActivity();
    }
    ConnectRuntimeInstances();
    StartActivities();
#if defined(EA_PORT_NATIVE)
    WCollisionAssets::Get().AddPackLoadCallback(NotifyCollisionPackLoaded);
#endif
    mPursuitBreakerIconsShown = false;
    mHidingSpotIconsShown = false;
    mEventIconsShown = false;
    mMenuGateIconsShown = false;
    mSpeedTrapIconsShown = false;
    mSpeedTrapRaceIconsShown = false;
#if defined(EA_PORT_NATIVE)
    RefreshEngageTriggerIcons();
    RefreshSpeedTrapIcons();
    SpawnAllLoadedSectionIcons();
#endif
    if (startupRace == nullptr && GRaceStatus::Get().GetCurrentBin() != nullptr) {
        GRaceStatus::Get().GetCurrentBin()->EnableBarriers();
    }
    MEnteringGameplay().Post(UCrc32(UCRC32_Gameplay));
    mStartFreeRoamFromSafeHouse = false;
    mInGameplay = true;
}

// PC: 0x5F9E70. Runs the startup race's activity (or the free roam and bin
// activities) and hands the race context to the race status.
void GManager::StartActivities() {
    bool startWorld = SkipFE == 0;
    bool binChanged = false;
    GRaceCustom *startupRace = GRaceDatabase::Get().GetStartupRace();
    GRaceBin *raceBin = GRaceStatus::Get().GetCurrentBin();
    if (startupRace != nullptr) {
        GActivity *raceActivity = startupRace->GetRaceActivity();
        if (raceActivity != nullptr) {
            raceActivity->Run();
        }
        GRace::Context context = GRaceDatabase::Get().GetStartupRaceContext();
        GRaceStatus::Get().SetRaceContext(context);
        startWorld = context == GRace::kRaceContext_Career && startWorld;
        if (raceBin != nullptr && startupRace->GetChildVault() != raceBin->GetChildVault()) {
            binChanged = true;
            if (context == GRace::kRaceContext_Career) {
                GRaceStatus::Get().RefreshBinAfterRace();
            }
        }
    }
    if (startWorld) {
        StartWorldActivities(startupRace == nullptr);
        if (!binChanged && raceBin != nullptr) {
            GActivity *binActivity = GRuntimeInstance::FindObject<GActivity>(raceBin->GetCollectionKey());
            if (binActivity != nullptr && !binActivity->GetIsRunning()) {
                binActivity->Run();
            }
        }
    }
}

// PC: 0x5F95F0. Runs the auto-start activities (the free-roam-only ones only
// when there's no race).
void GManager::StartWorldActivities(bool startFreeRoamOnly) {
    for (GObjectIterator<GActivity> iter(0xffffffff); iter.IsValid(); iter.Advance()) {
        GActivity *activity = iter.GetInstance();
        bool autoStart = activity->AutoStart();
        if ((!activity->FreeRoamOnly() || startFreeRoamOnly) && autoStart) {
            activity->Run();
        }
    }
}

// PS2: 0x2F0EF8. Stops the activities and tears the runtime instances down
// so the next session starts clean.
void GManager::EndGameplay() {
    SuspendAllActivities();
#if defined(EA_PORT_NATIVE)
    // PC sub_5F6050: the icons unspawned and the section callback removed.
    UnspawnAllIcons();
    WCollisionAssets::Get().RemovePackLoadCallback(NotifyCollisionPackLoaded);
#endif
    for (unsigned int onVault = 0; onVault < mVaultCount; onVault++) {
        if (mVaults[onVault].GetObjectBlock() != nullptr) {
            mVaults[onVault].DestroyGameplayObjects();
        }
    }
    mInGameplay = false;
}

// PS2: inlined. Stops every running activity.
void GManager::SuspendAllActivities() {
    for (GObjectIterator<GActivity> iter(0xffffffff); iter.IsValid(); iter.Advance()) {
        iter.GetInstance()->Suspend();
    }
}

// PC: 0x5E40F0. Room for every resident object plus two transient vaults'
// worth, with 50% slack, rounded up to a power of two (at least 0x100).
void GManager::AllocateInstanceMap() {
    unsigned int largestTransient = 0;
    mMaxObjects = 0;
    for (unsigned int onVault = 0; onVault < mVaultCount; onVault++) {
        unsigned int count = mVaults[onVault].GetObjectCount();
        if (mVaults[onVault].IsResident()) {
            mMaxObjects += count;
        } else if (count > largestTransient) {
            largestTransient = count;
        }
#if defined(EA_PORT_NATIVE)
        // Every transient vault stays loaded natively (no race streaming), so
        // all of their instances exist at once.
        if (!mVaults[onVault].IsResident()) {
            mMaxObjects += count;
        }
#endif
    }
    mMaxObjects += largestTransient * 2;
    unsigned int wanted = mMaxObjects + (mMaxObjects >> 1);
    unsigned int size = 0x100;
    while (size < wanted) {
        size <<= 1;
    }
    mInstanceHashTableSize = size;
    mInstanceHashTableMask = size - 1;
    mKeyToInstanceMap = static_cast<HashEntry *>(bMalloc(static_cast<int>(size * sizeof(HashEntry)), 0));
    bMemSet(mKeyToInstanceMap, 0, size * sizeof(HashEntry));
    mWorstHashCollision = 0;
}

// PC: 0x5EF5B0. The bin/race stream slots and unloading the preloaded
// transient vaults are skipped natively (every vault stays loaded from the
// pack image, see LoadVaultSync); only the instance map is set up.
void GManager::InitializeRaceStreaming() {
    if (mKeyToInstanceMap == nullptr) {
        AllocateInstanceMap();
    }
}

// PC: 0x5E41A0. Open addressing on the reduced collection key.
void GManager::RegisterInstance(GRuntimeInstance *instance) {
    if (mInstanceHashTableSize == 0) {
        return;
    }
    uint32 key32 = Get32BitCollectionKey(instance->GetCollection());
    uint32 slot = key32;
    for (unsigned int probes = 0; probes < mInstanceHashTableSize; probes++, slot++) {
        slot &= mInstanceHashTableMask;
        if (mKeyToInstanceMap[slot].mInstance == nullptr) {
            if (probes > mWorstHashCollision) {
                mWorstHashCollision = probes;
            }
            mKeyToInstanceMap[slot].mKey32 = key32;
            mKeyToInstanceMap[slot].mInstance = instance;
            return;
        }
    }
}

// PS2: 0x2F0C30 (guess at the details). The key stays in the slot so later
// probes still step over it.
void GManager::UnregisterInstance(GRuntimeInstance *instance) {
    for (unsigned int slot = 0; slot < mInstanceHashTableSize; slot++) {
        if (mKeyToInstanceMap[slot].mInstance == instance) {
            mKeyToInstanceMap[slot].mInstance = nullptr;
        }
    }
}

// PC: 0x5DD930.
GRuntimeInstance *GManager::FindInstance(Attrib::Key key) const {
    if (mInstanceHashTableSize == 0) {
        return nullptr;
    }
    uint32 key32 = Get32BitCollectionKey(key);
    uint32 slot = key32;
    for (unsigned int probes = 0; probes <= mWorstHashCollision; probes++, slot++) {
        slot &= mInstanceHashTableMask;
        if (mKeyToInstanceMap[slot].mKey32 == key32 && mKeyToInstanceMap[slot].mInstance != nullptr) {
            return mKeyToInstanceMap[slot].mInstance;
        }
    }
    return nullptr;
}

// PC: 0x5F18A0. Rebuilds every instance's connection table from its own and
// its parents' GCollectionKey attributes and its children.
void GManager::ConnectRuntimeInstances() {
    for (unsigned int slot = 0; slot < mInstanceHashTableSize; slot++) {
        GRuntimeInstance *instance = mKeyToInstanceMap[slot].mInstance;
        if (instance == nullptr) {
            continue;
        }
        instance->ResetConnections();
        ConnectInstanceReferences(instance, *instance);
        Attrib::Key parent = instance->GetParent();
        while (parent != 0) {
            Attrib::Gen::gameplay parentObj(parent, 0, nullptr);
            ConnectInstanceReferences(instance, parentObj);
            parent = parentObj.GetParent();
        }
        ConnectChildren(instance);
        instance->LockConnections();
    }
}

// PC: 0x5E4210.
void GManager::ConnectInstanceReferences(GRuntimeInstance *runtimeInstance, const Attrib::Gen::gameplay &collection) {
    Attrib::Type collectionKeyType = Attrib::StringToKey("GCollectionKey");
    for (Attrib::AttributeIterator iter = collection.Iterator(); iter.Valid(); iter.Advance()) {
        Attrib::Key attribKey = iter.GetKey();
        if (attribKey == 0x916e0e78) { // Children
            continue;
        }
        Attrib::Attribute attrib = collection.Get(attribKey);
        if (!attrib.IsValid() || attrib.GetType() != collectionKeyType) {
            continue;
        }
        const Attrib::Definition *def = mGameplayClass->GetDefinition(attribKey);
        unsigned int count = def != nullptr && def->IsArray() ? attrib.GetLength() : 1;
        for (unsigned int index = 0; index < count; index++) {
            const GCollectionKey *ref = static_cast<const GCollectionKey *>(collection.GetAttributePointer(attribKey, index));
            if (ref == nullptr) {
                continue;
            }
            GRuntimeInstance *target = FindInstance(ref->mCollectionKey);
            if (target != nullptr) {
                runtimeInstance->ConnectToInstance(attribKey, index, target);
            }
        }
    }
}

// PC: 0x5E43D0. Children are connected under their name (the last component
// of the collection name).
void GManager::ConnectChildren(GRuntimeInstance *runtimeInstance) {
    unsigned int numChildren = runtimeInstance->Num_Children();
    for (unsigned int index = 0; index < numChildren; index++) {
        GRuntimeInstance *child = FindInstance(runtimeInstance->Children(index).mCollectionKey);
        if (child == nullptr) {
            continue;
        }
        const char *name = child->CollectionName();
        for (const char *p = name + bStrLen(name) - 1; p >= name; p--) {
            if (*p == '/' || *p == '\\') {
                name = p + 1;
                break;
            }
        }
        runtimeInstance->ConnectToInstance(Attrib::StringToKey(name), 0, child);
    }
}

// PC: 0x5E4C10.
void GManager::UpdateTimers(float dT) {
    for (int i = 0; i < 8; i++) {
        mTimers[i].Update(dT);
    }
}

// PC: 0x6036C0.
void GManager::Update(float dT) {
    GRaceStatus::Get().Update(dT);
    // TODO(port): UnspawnUselessCharacters and ServicePendingCharacters
    // (characters aren't spawned by the scripts yet).
#if defined(EA_PORT_NATIVE)
    UpdatePursuit();
#endif
    UpdateTimers(dT);
#if defined(EA_PORT_NATIVE)
    // NFSMW_DEBUG_SKIP_RACE=<place>[:<seconds>]: once the race clock passes
    // <seconds> (default 5), finish the race with the player in <place>
    // (GRaceStatus::SkipToEndOfRaceInPlace, a PC debug path).
    if (const char *skip = std::getenv("NFSMW_DEBUG_SKIP_RACE")) {
        static bool sSkipped = false;
        GRaceStatus &raceStatus = GRaceStatus::Get();
        const char *colon = std::strchr(skip, ':');
        float delay = colon != nullptr ? static_cast<float>(std::atof(colon + 1)) : 5.0f;
        float elapsed = raceStatus.GetRaceTimeElapsed();
        if (raceStatus.GetPlayMode() != GRaceStatus::kPlayMode_Racing || raceStatus.GetRaceParameters() == nullptr || elapsed <= delay) {
            sSkipped = false;
        } else if (!sSkipped) {
            int place = std::atoi(skip);
            {
                sSkipped = true;
                std::fprintf(stderr, "[race] NFSMW_DEBUG_SKIP_RACE: finishing in place %d\n", place);
                raceStatus.SkipToEndOfRaceInPlace(place < 1 ? 1 : place, elapsed + 60.0f);
            }
        }
    }
    // NFSMW_TRACE_LUA: the race status every 5 s of sim time.
    static float sNextTrace = 0.0f;
    if (Sim::GetTime() + 5.0f < sNextTrace) {
        sNextTrace = 0.0f; // a new race
    }
    if (std::getenv("NFSMW_TRACE_LUA") != nullptr && Sim::GetTime() >= sNextTrace) {
        sNextTrace = Sim::GetTime() + 5.0f;
        GRaceStatus &raceStatus = GRaceStatus::Get();
        GRaceParameters *parms = raceStatus.GetRaceParameters();
        std::fprintf(stderr, "[race] t=%.1f mode=%d race=%s racers=%d clock=%.2f\n", Sim::GetTime(), static_cast<int>(raceStatus.GetPlayMode()),
                     parms != nullptr ? parms->GetEventID() : "-", raceStatus.GetRacerCount(), raceStatus.GetRaceTimeElapsed());
        for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
            GRacerInfo &info = raceStatus.GetRacerInfo(i);
            ISimable *isim = info.GetSimable();
            UMath::Vector3 pos = isim != nullptr ? isim->GetPosition() : UMath::Vector3::kZero;
            std::fprintf(stderr, "[race]   %d %-12s rank=%d pct=%.1f pos=(%.1f %.1f %.1f)%s\n", i, info.GetName() != nullptr ? info.GetName() : "?",
                         info.GetRanking(), info.GetPctRaceComplete(), pos.x, pos.y, pos.z, isim == nullptr ? " (no car)" : "");
        }
    }
    UpdatePendingSMS();
#endif
    UpdateTriggerAvailability();
#if defined(EA_PORT_NATIVE)
    UpdateIconVisibility();
#endif
}

// PC: 0x5EB9E0. The local player's pursuit (its vehicle AI's) and
// perpetrator interfaces.
void GManager::GetPlayerPursuitInterfaces(IPursuit *&pursuit, IPerpetrator *&perp) {
    IPlayer *player = IPlayer::First(PLAYER_LOCAL);
    ISimable *simable = player != nullptr ? player->GetSimable() : nullptr;
    pursuit = nullptr;
    if (simable != nullptr) {
        IVehicleAI *vehicleAI;
        if (simable->QueryInterface(&vehicleAI)) {
            pursuit = vehicleAI->GetPursuit();
        }
    }
    perp = nullptr;
    if (simable != nullptr) {
        simable->QueryInterface(&perp);
    }
}

// PC: 0x5EBA50. Out of races and pursuits the player may engage events and
// enter the menu gates; the safehouse also once a pursuit is cooling down.
void GManager::UpdateTriggerAvailability() {
    const bool racing = GRaceStatus::Get().GetPlayMode() != GRaceStatus::kPlayMode_Roaming;
    IPursuit *pursuit = nullptr;
    IPerpetrator *perp;
    GetPlayerPursuitInterfaces(pursuit, perp);
    const bool inPursuit = pursuit != nullptr;
    const bool coolingDown = inPursuit && pursuit->GetPursuitStatus() == PS_COOL_DOWN;
    mAllowMenuGates = !racing && !inPursuit;
    mAllowEngageEvents = !racing && !inPursuit;
    mAllowEngageSafehouse = !racing && (!inPursuit || coolingDown);
}

// PC: inlined (as GetNumMilestones 0x5DDA40 / GetMilestone 0x5DDA50).
unsigned int GManager::GetNumSpeedTraps() {
    return mNumSpeedTraps;
}

GSpeedTrap *GManager::GetSpeedTrap(unsigned int index) {
    return &mSpeedTraps[index];
}

// PC: 0x5EBD90. Every trigger's particle effects again (as enabled), then
// the icons' effects.
void GManager::RefreshWorldParticleEffects() {
    for (GObjectIterator<GTrigger> iter(0xffffffff); iter.IsValid(); iter.Advance()) {
        iter.GetInstance()->RefreshParticleEffects();
    }
#if defined(EA_PORT_NATIVE)
    for (unsigned int i = 0; i < mNumIcons; i++) {
        mIcons[i]->RefreshEffects();
    }
#endif
}

#if defined(EA_PORT_NATIVE)
// ---------------------------------------------------------------------------
// Icons (GIcon.cpp): the world and map markers of the gameplay objects. The
// first mNumVisibleIcons of mIcons are the visible ones (UpdateIconVisibility).
// ---------------------------------------------------------------------------

// PC: inlined in InitializeVaults (0x5F8080): room for 200.
void GManager::AllocateIcons() {
    mNumIcons = 0;
    mNumVisibleIcons = 0;
    mIcons = static_cast<GIcon **>(gFastMem.Alloc(200 * sizeof(GIcon *), nullptr));
}

// PC: 0x5E9EC0.
GIcon *GManager::AllocIcon(GIcon::Type iconType, const UMath::Vector3 &pos, float rotDeg, bool disposable) {
    if (mNumIcons >= 200) {
        return nullptr;
    }
    void *mem = gFastMem.Alloc(sizeof(GIcon), nullptr);
    if (mem == nullptr) {
        return nullptr;
    }
    GIcon *icon = new (mem) GIcon(iconType, pos, rotDeg);
    mIcons[mNumIcons] = icon;
    mNumIcons++;
    if (disposable) {
        icon->SetFlag(0x10);
    }
    return icon;
}

// PC: 0x5EDD60. The icon goes (and with it the GPS target); the last visible
// icon takes its place, and the last icon that one's.
void GManager::FreeIconAt(unsigned int index) {
    if (mNumIcons == 0) {
        return;
    }
    GIcon *icon = mIcons[index];
    if (icon != nullptr) {
        if (icon == WorldMap::GetGPSingIcon()) {
            WorldMap::ClearGPSing();
        }
        icon->~GIcon();
        gFastMem.Free(icon, sizeof(GIcon), nullptr);
        mIcons[index] = nullptr;
    }
    if (index + 1 < mNumIcons) {
        if (index + 1 < mNumVisibleIcons) {
            mIcons[index] = mIcons[mNumVisibleIcons - 1];
            if (mNumVisibleIcons < mNumIcons) {
                mIcons[mNumVisibleIcons - 1] = mIcons[mNumIcons - 1];
            }
        } else {
            mIcons[index] = mIcons[mNumIcons - 1];
        }
    }
    if (index < mNumVisibleIcons) {
        mNumVisibleIcons--;
    }
    mNumIcons--;
}

// PC: inlined.
void GManager::FreeIcon(GIcon *icon) {
    for (unsigned int i = 0; i < mNumIcons; i++) {
        if (mIcons[i] == icon) {
            FreeIconAt(i);
            return;
        }
    }
}

// PC: 0x5EEF00.
void GManager::FreeDisposableIcons(GIcon::Type iconType) {
    for (unsigned int i = 0; i < mNumIcons;) {
        GIcon *icon = mIcons[i];
        if (icon->IsFlagSet(0x10) && icon->GetType() == iconType) {
            FreeIconAt(i);
        } else {
            i++;
        }
    }
}

// PC: 0x5EF640. A disposable hiding spot icon on each zone of type 8.
void GManager::RefreshZoneIcons() {
    FreeDisposableIcons(GIcon::kType_HidingSpot);
    for (TrackPathZone *zone = TheTrackPathManager.FindZone(nullptr, static_cast<eTrackPathZoneType>(8), nullptr); zone != nullptr;
         zone = TheTrackPathManager.FindZone(nullptr, static_cast<eTrackPathZoneType>(8), zone)) {
        UMath::Vector3 pos = {zone->Position.x, zone->Position.y, zone->Elevation};
        GIcon *icon = AllocIcon(GIcon::kType_HidingSpot, pos, 0.0f, true);
        if (icon != nullptr) {
            icon->SetFlag(3);
        }
    }
    if (mInGameplay) {
        SpawnAllLoadedSectionIcons();
    }
}

// PC: 0x5EBE70. A disposable pursuit breaker icon on each "IconMarker".
bool GManager::AddIconForTrackMarker(TrackPositionMarker *marker, uint32 tag) {
    if (marker->NameHash == tag) {
        UMath::Vector3 pos = {marker->Position.x, marker->Position.y, marker->Position.z};
        GIcon *icon = GManager::Get().AllocIcon(GIcon::kType_PursuitBreaker, pos, 0.0f, true);
        if (icon != nullptr) {
            icon->SetFlag(3);
        }
    }
    return true;
}

// PC: 0x5EF6D0.
void GManager::RefreshTrackMarkerIcons() {
    FreeDisposableIcons(GIcon::kType_PursuitBreaker);
    ForEachTrackPositionMarker(AddIconForTrackMarker, bStringHash("IconMarker"));
    if (mInGameplay) {
        SpawnAllLoadedSectionIcons();
    }
}

// PC: 0x602840. An event trigger's icon shows while its race is unlocked
// and not yet won in the career.
void GManager::RefreshEngageTriggerIcons() {
    for (GObjectIterator<GTrigger> iter(GTrigger::kFlag_TriggerEngage); iter.IsValid(); iter.Advance()) {
        GTrigger *trigger = iter.GetInstance();
        GRuntimeInstance *activity = trigger->GetTargetActivity();
        if (activity == nullptr) {
            continue;
        }
        GRaceParameters *race = GRaceDatabase::Get().GetRaceFromKey(activity->GetCollection());
        const unsigned int eventHash = race->GetEventHash();
        if (!GRaceDatabase::Get().IsCareerRaceUnlocked(eventHash) || GRaceDatabase::Get().IsCareerRaceComplete(eventHash)) {
            trigger->HideIcon();
        } else {
            trigger->ShowIcon();
        }
    }
}

// PC: inlined (GIcon::FindSection): whether the icon's section (or its
// combined section) is loaded.
bool GManager::IconSectionLoaded(GIcon *icon) {
    icon->FindSection();
    if (icon->GetSectionID() < 0) {
        return false;
    }
    TrackStreamingSection *section = TheTrackStreamer.FindSection(icon->GetSectionID());
    if (section != nullptr && section->Status == TrackStreamingSection::ACTIVATED) {
        return true;
    }
    section = TheTrackStreamer.FindSection(icon->GetCombinedSectionID());
    return section != nullptr && section->Status == TrackStreamingSection::ACTIVATED;
}

// PC: 0x5EDE20.
void GManager::SpawnAllLoadedSectionIcons() {
    for (unsigned int i = 0; i < mNumIcons; i++) {
        if (IconSectionLoaded(mIcons[i])) {
            mIcons[i]->Spawn();
        }
    }
}

// PC: 0x5EDEF0.
void GManager::SpawnSectionIcons(int section) {
    for (unsigned int i = 0; i < mNumIcons; i++) {
        GIcon *icon = mIcons[i];
        icon->FindSection();
        if (icon->GetSectionID() == section || icon->GetCombinedSectionID() == section) {
            icon->Spawn();
        }
    }
}

// PC: 0x5E9F70.
void GManager::UnspawnSectionIcons(int section) {
    for (unsigned int i = 0; i < mNumIcons; i++) {
        GIcon *icon = mIcons[i];
        icon->FindSection();
        if (icon->GetSectionID() == section || icon->GetCombinedSectionID() == section) {
            icon->Unspawn();
        }
    }
}

// PC: inlined in sub_5F6050 (EndGameplay).
void GManager::UnspawnAllIcons() {
    for (unsigned int i = 0; i < mNumIcons; i++) {
        mIcons[i]->Unspawn();
    }
}

// PC: 0x5EED60 (WCollisionAssets pack callback).
void GManager::NotifyCollisionPackLoaded(int sectionID, bool loaded) {
    if (mObj == nullptr) {
        return;
    }
    if (loaded) {
        mObj->SpawnSectionIcons(sectionID);
    } else {
        mObj->UnspawnSectionIcons(sectionID);
    }
}

// PC: 0x5FDAE0. Which icon types show: events and speed traps while events
// may be engaged, gates while the menu gates may, the safehouse while it
// may, speed trap race cameras in a speed trap race; nothing during an NIS.
// The icons shown in the world (flag 1) of a shown type are enabled and
// moved to the front (the visible ones), the rest disabled.
void GManager::UpdateIconVisibility() {
    mEventIconsShown = mAllowEngageEvents;
    mMenuGateIconsShown = mAllowMenuGates;
    mSpeedTrapIconsShown = mAllowEngageEvents;
    GRaceParameters *race = GRaceStatus::Exists() ? GRaceStatus::Get().GetRaceParameters() : nullptr;
    mSpeedTrapRaceIconsShown = race != nullptr && race->GetRaceType() == GRace::kRaceType_SpeedTrap;
    // Types 0..15, and 16 (checkpoints) reads the byte after them on PC: the
    // show-all tweak.
    const bool showAll = false; // TWEAK_ShowAllGameplayIcons
    bool show[GIcon::kType_Count];
    for (int i = 0; i < GIcon::kType_Count; i++) {
        show[i] = showAll;
    }
    if (!showAll && !INIS::Exists()) {
        show[GIcon::kType_Invalid] = false;
        for (int i = GIcon::kType_RaceSprint; i <= GIcon::kType_RaceRival; i++) {
            show[i] = mEventIconsShown;
        }
        show[GIcon::kType_GateSafehouse] = mAllowEngageSafehouse;
        show[GIcon::kType_GateCarLot] = mMenuGateIconsShown;
        show[GIcon::kType_GateCustomShop] = mMenuGateIconsShown;
        show[GIcon::kType_HidingSpot] = mHidingSpotIconsShown;
        show[GIcon::kType_PursuitBreaker] = mPursuitBreakerIconsShown;
        show[GIcon::kType_SpeedTrap] = mSpeedTrapIconsShown;
        show[GIcon::kType_SpeedTrapInRace] = mSpeedTrapRaceIconsShown;
        show[GIcon::kType_AreaUnlock] = true;
    }
    mNumVisibleIcons = 0;
    for (unsigned int i = 0; i < mNumIcons; i++) {
        GIcon *icon = mIcons[i];
        const bool enabled = icon->IsFlagSet(8);
        if (!icon->IsFlagSet(1) || !show[icon->GetType()]) {
            if (enabled) {
                icon->Disable();
            }
        } else {
            if (!enabled && icon->IsFlagSet(4)) {
                icon->Enable();
            }
            mIcons[i] = mIcons[mNumVisibleIcons];
            mIcons[mNumVisibleIcons] = icon;
            mNumVisibleIcons++;
        }
    }
}

// PC: 0x5DDB30.
bool GManager::GetIsIconVisible(GIcon *icon) {
    for (unsigned int i = 0; i < mNumVisibleIcons; i++) {
        if (mIcons[i] == icon) {
            return true;
        }
    }
    return false;
}

namespace {
struct IconSort {
    GIcon *icon;
    int key;
};

// PC: 0x5DDB20.
int CompareIcons(const void *a, const void *b) {
    return static_cast<const IconSort *>(a)->key - static_cast<const IconSort *>(b)->key;
}
} // namespace

// PC: 0x5E4940. The visible icons shown on the map, nearest the player's
// car first. The key is PC's: icon x against the car's z and icon z
// (height) against its y, i.e. bWare x and z, not the ground plane.
int GManager::GatherVisibleIcons(GIcon **iconArray, IPlayer *player) {
    ISimable *simable = player != nullptr ? player->GetSimable() : nullptr;
    float carY = 0.0f;
    float carZ = 0.0f;
    if (simable != nullptr) {
        const UMath::Vector3 &pos = simable->GetPosition();
        carY = pos.y;
        carZ = pos.z;
    }
    IconSort sorted[200];
    int count = 0;
    for (unsigned int i = 0; i < mNumVisibleIcons; i++) {
        GIcon *icon = mIcons[i];
        if (icon->IsFlagSet(1) && icon->IsFlagSet(2)) {
            sorted[count].icon = icon;
            if (simable == nullptr) {
                sorted[count].key = 0;
            } else {
                const float dx = icon->GetPosition().x - carZ;
                const float dz = icon->GetPosition().z - carY;
                sorted[count].key = static_cast<int>(dz * dz + dx * dx);
            }
            count++;
        }
    }
    if (simable != nullptr) {
        std::qsort(sorted, count, sizeof(IconSort), CompareIcons);
    }
    for (int i = 0; i < count; i++) {
        iconArray[i] = sorted[i].icon;
    }
    return count;
}

// PC: 0x5F19D0 / 0x5F19C0.
void NotifyTrackMarkersChanged() {
    if (GManager::Exists()) {
        GManager::Get().RefreshTrackMarkerIcons();
    }
}

void NotifyGameZonesChanged() {
    if (GManager::Exists()) {
        GManager::Get().RefreshZoneIcons();
    }
}
#endif

#if defined(EA_PORT_NATIVE)
// Milestone value tracking and the pursuit notifications. PC looks values up
// with find() and writes through whatever it returns; natively an unknown
// value is ignored.

// PC: 0x5EDA20
void GManager::TrackValue(const char *valueName, float value) {
    MilestoneInfoMap::iterator it = mMilestoneTypeInfo.find(Attrib::StringToKey(valueName));
    if (it == mMilestoneTypeInfo.end()) {
        return;
    }
    MilestoneTypeInfo &info = it->second;
    info.mLastKnownValue = value;
    if (info.mBestValue != -1.0f) {
        if ((info.mFlags & 1) == 0) {
            if (info.mBestValue <= value) {
                return;
            }
        } else if (value <= info.mBestValue) {
            return;
        }
    }
    info.mBestValue = value;
    MNotifyMilestoneProgress(valueName, value).Post(UCrc32(UCRC32_Gameplay));
    if (GRaceStatus::Exists() && GRaceStatus::Get().GetPlayMode() == GRaceStatus::kPlayMode_Roaming) {
        for (unsigned int i = 0; i < mNumMilestones; i++) {
            GMilestone &milestone = mMilestones[i];
            if ((milestone.GetIsAvailable() || milestone.GetIsDonePendingEscape()) && milestone.GetTypeKey() == info.mTypeKey) {
                milestone.NotifyProgress(value);
            }
        }
    }
}

// PC: 0x5EDC00
float GManager::GetValue(Attrib::Key valueKey) {
    MilestoneInfoMap::iterator it = mMilestoneTypeInfo.find(valueKey);
    if (it == mMilestoneTypeInfo.end()) {
        return 0.0f;
    }
    return it->second.mLastKnownValue;
}

// PC: 0x5EDD10. Values that only count per pursuit start over.
void GManager::NotifyPursuitStarted() {
    for (MilestoneInfoMap::iterator it = mMilestoneTypeInfo.begin(); it != mMilestoneTypeInfo.end(); ++it) {
        if ((it->second.mFlags & 2) != 0) {
            it->second.mBestValue = -1.0f;
            it->second.mLastKnownValue = -1.0f;
        }
    }
}

// PC: 0x602770. Milestones done during the pursuit are awarded when it's
// evaded (or reset when it isn't); evading in free roam autosaves.
void GManager::NotifyPursuitEnded(bool evaded) {
    for (unsigned int i = 0; i < mNumMilestones; i++) {
        GMilestone &milestone = mMilestones[i];
        if (milestone.GetIsAvailable() || milestone.GetIsDonePendingEscape()) {
            milestone.NotifyPursuitOver(evaded);
        }
    }
    if (evaded && GRaceStatus::Get().GetPlayMode() == GRaceStatus::kPlayMode_Roaming) {
        new EAutoSave();
    }
}

// PC: 0x5EDB50. A count goes up by one (from nothing, 1).
void GManager::IncValue(const char *valueName) {
    MilestoneInfoMap::iterator it = mMilestoneTypeInfo.find(Attrib::StringToKey(valueName));
    if (it == mMilestoneTypeInfo.end()) {
        return;
    }
    TrackValue(valueName, it->second.mLastKnownValue == -1.0f ? 1.0f : it->second.mLastKnownValue + 1.0f);
}

// PC: 0x5EDBC0
float GManager::GetValue(const char *valueName) {
    return GetValue(Attrib::StringToKey(valueName));
}

// PC: 0x5EDC60
float GManager::GetBestValue(Attrib::Key valueKey) {
    MilestoneInfoMap::iterator it = mMilestoneTypeInfo.find(valueKey);
    return it != mMilestoneTypeInfo.end() ? it->second.mBestValue : 0.0f;
}

float GManager::GetBestValue(const char *valueName) {
    return GetBestValue(Attrib::StringToKey(valueName));
}

bool GManager::GetIsBiggerValueBetter(Attrib::Key valueKey) {
    MilestoneInfoMap::iterator it = mMilestoneTypeInfo.find(valueKey);
    return it != mMilestoneTypeInfo.end() && (it->second.mFlags & 1) != 0;
}

// PC: 0x5F4BF0. One milestone per challenge collection (under 0x1D975142,
// derived from the challenge template 0xA3D34781).
void GManager::AllocateMilestones() {
    Attrib::Gen::gameplay root(0x1D975142, 0, nullptr);
    AttribKeyList keys;
    GatherInstanceKeys(root, keys, 0xA3D34781);
    mNumMilestones = static_cast<unsigned int>(keys.size());
    mMilestones = mNumMilestones != 0 ? new GMilestone[mNumMilestones] : nullptr;
    GMilestone *milestone = mMilestones;
    for (AttribKeyList::iterator it = keys.begin(); it != keys.end(); ++it, ++milestone) {
        milestone->Init(*it);
    }
}

// PC: 0x5F4D40. Every milestone back to its challenge's (locked) state.
void GManager::ResetMilestones() {
    if (mNumMilestones == 0) {
        return;
    }
    Attrib::Gen::gameplay root(0x1D975142, 0, nullptr);
    AttribKeyList keys;
    GatherInstanceKeys(root, keys, 0xA3D34781);
    GMilestone *milestone = mMilestones;
    for (AttribKeyList::iterator it = keys.begin(); it != keys.end() && milestone < mMilestones + mNumMilestones; ++it, ++milestone) {
        milestone->Init(*it);
    }
}

// PC: 0x5F4AD0. Up to 20 bounty spawn markers (under 0x3D48E303, derived from
// 0xA7BCCF63).
void GManager::FindBountySpawnPoints() {
    Attrib::Gen::gameplay root(0x3D48E303, 0, nullptr);
    AttribKeyList keys;
    GatherInstanceKeys(root, keys, 0xA7BCCF63);
    mNumBountySpawnPoints = 0;
    for (AttribKeyList::iterator it = keys.begin(); it != keys.end(); ++it) {
        mBountySpawnPoint[mNumBountySpawnPoints++] = *it;
        if (mNumBountySpawnPoints > 19) {
            break;
        }
    }
}

// PC: 0x5DDAF0 / 0x5DDB00 / 0x5E47C0.
unsigned int GManager::GetNumBountySpawnMarkers() const {
    return mNumBountySpawnPoints;
}

Attrib::Key GManager::GetBountySpawnMarker(unsigned int index) const {
    return index < mNumBountySpawnPoints ? mBountySpawnPoint[index] : 0;
}

int GManager::GetBountySpawnMarkerTag(unsigned int index) const {
    Attrib::Gen::gameplay marker(GetBountySpawnMarker(index), 0, nullptr);
    const int *tag = static_cast<const int *>(marker.GetAttributePointer(0xDB89AB5C, 0));
    return tag != nullptr ? *tag : 0;
}

// PC: 0x5DDA50
GMilestone *GManager::GetMilestone(unsigned int index) {
    return &mMilestones[index];
}

// PC: 0x5E46D0
GMilestone *GManager::GetFirstMilestone(bool availOnly, unsigned int binNumber) {
    return GetNextMilestone(mMilestones - 1, availOnly, binNumber);
}

// PC: 0x5DDA70. The next one (available or at stake, with availOnly) in the
// bin (any bin for 0).
GMilestone *GManager::GetNextMilestone(GMilestone *current, bool availOnly, unsigned int binNumber) {
    for (GMilestone *milestone = current + 1; milestone < mMilestones + mNumMilestones; milestone++) {
        if (availOnly && !milestone->GetIsAvailable() && !milestone->GetIsDonePendingEscape()) {
            continue;
        }
        if (binNumber == 0 || milestone->GetBinNumber() == binNumber) {
            return milestone;
        }
    }
    return nullptr;
}

// PC: 0x5E46F0. The bin's locked milestones become available.
void GManager::EnableBinMilestones(unsigned int binNumber) {
    for (GMilestone *milestone = GetFirstMilestone(false, binNumber); milestone != nullptr; milestone = GetNextMilestone(milestone, false, binNumber)) {
        milestone->Unlock();
    }
}

// PC: 0x6013F0. The pursuit's values for the milestones, and which of the
// pursuit icons show: the pursuit breakers while chased, the hiding spots
// while cooling down (in free roam, a challenge series race or an epic
// pursuit).
void GManager::UpdatePursuit() {
    GRaceStatus &raceStatus = GRaceStatus::Get();
    const bool roaming = raceStatus.GetPlayMode() == GRaceStatus::kPlayMode_Roaming;
    IPursuit *pursuit = nullptr;
    IPerpetrator *perp = nullptr;
    GetPlayerPursuitInterfaces(pursuit, perp);
    bool coolingDown = false;
    if (pursuit != nullptr) {
        TrackValue("cost_to_state_in_pursuit", static_cast<float>(static_cast<int>(pursuit->CalcTotalCostToState())));
        coolingDown = pursuit->GetPursuitStatus() == PS_COOL_DOWN;
    }
    raceStatus.SetPlayerPursuitInCooldown(coolingDown);
    if (perp != nullptr) {
        TrackValue("cost_to_state", static_cast<float>(perp->GetCostToState()));
        TrackValue("bounty_in_pursuit", static_cast<float>(perp->GetPendingRepPointsFromCopDestruction() + perp->GetPendingRepPointsNormal()));
    }
    GRaceParameters *parms = raceStatus.GetRaceParameters();
    const bool challengeSeries = parms != nullptr && parms->GetIsChallengeSeriesRace();
    const bool pursuitIcons = roaming || challengeSeries || (parms != nullptr && parms->GetIsEpicPursuitRace());
    mHidingSpotIconsShown = (roaming || challengeSeries) && pursuit != nullptr && coolingDown;
    mPursuitBreakerIconsShown = pursuitIcons && pursuit != nullptr && !coolingDown;
}

// ---------------------------------------------------------------------------
// Respawning and warping in free roam.
// ---------------------------------------------------------------------------

// PC: 0x5E4CF0. The override marker if there is one, otherwise the free roam
// start (from the safehouse, or the last one); without either, the track's
// career start marker ("career_start_<track>").
Attrib::Key GManager::GetRespawnMarker() {
    Attrib::Gen::gameplay overrideMarker(mOverrideFreeRoamStartMarker, 0, nullptr);
    if (overrideMarker.IsValid()) {
        return mOverrideFreeRoamStartMarker;
    }
    Attrib::Key marker = mStartFreeRoamFromSafeHouse ? mFreeRoamFromSafeHouseStartMarker : mFreeRoamStartMarker;
    Attrib::Gen::gameplay startMarker(marker, 0, nullptr);
    if (!startMarker.IsValid()) {
        char name[32];
        bSPrintf(name, "career_start_%s", TrackInfo::GetLoadedTrackInfo() != nullptr ? TrackInfo::GetLoadedTrackInfo()->RegionName : "");
        marker = Attrib::StringToLowerCaseKey(name);
        mFreeRoamStartMarker = marker;
    }
    return marker;
}

// PC: 0x5E4DF0. The respawn marker's position and heading (as GMarker reads
// them: vault (x, y, z) -> sim (-y, z, x), Rotation in degrees).
void GManager::GetRespawnLocation(UMath::Vector3 &startLoc, UMath::Vector3 &initialVec) {
    Attrib::Gen::gameplay marker(GetRespawnMarker(), 0, nullptr);
    const UMath::Vector3 &pos = marker.Position();
    startLoc.x = -pos.y;
    startLoc.y = pos.z;
    startLoc.z = pos.x;
    UMath::Matrix4 rotMat = UMath::Matrix4::kIdentity;
    initialVec = UMath::Vector3Make(0.0f, 0.0f, 1.0f);
    UMath::MultYRot(rotMat, marker.Rotation() * (-1.0f / 360.0f), rotMat);
    UMath::Rotate(initialVec, rotMat, initialVec);
}

// PC: 0x5EBC80. In free roam (and not warping already): fade, stream the
// world in at the marker and park the player's car there (deactivated) until
// NotifyWorldService finds the world loaded.
bool GManager::WarpToMarker(Attrib::Key markerKey, bool startPursuit) {
    if (!GRaceStatus::Exists() || GRaceStatus::Get().GetPlayMode() != GRaceStatus::kPlayMode_Roaming || mWarping) {
        return false;
    }
    GRuntimeInstance *instance = FindInstance(markerKey);
    if (instance == nullptr || instance->GetType() != kGameplayObjType_Marker) {
        return false;
    }
    GMarker *marker = static_cast<GMarker *>(instance);
    new EFadeScreenOn(false);
    // TODO(port): PC also clears a track streamer flag (byte 0x9AAC81).
    Sim::SetStream(marker->GetPosition(), false);
    IPlayer *player = IPlayer::First(PLAYER_LOCAL);
    IVehicle *vehicle;
    if (player != nullptr && player->GetSimable() != nullptr && player->GetSimable()->QueryInterface(&vehicle)) {
        vehicle->SetVehicleOnGround(marker->GetPosition(), marker->GetDirection());
        vehicle->Deactivate();
    }
    mWarpStartPursuit = startPursuit;
    mWarpTargetMarker = markerKey;
    mWarping = true;
    return true;
}

// PC: 0x5EBB10 (from the world's service each frame). A warp whose world has
// loaded puts the car down again (and, for a pursuit warp, clears the area
// and starts the cops at heat 1), then fades back in.
void GManager::NotifyWorldService() {
    if (!mWarping || TheTrackStreamer.IsLoadingInProgress()) {
        return;
    }
    if (mWarpTargetMarker != 0) {
        GRuntimeInstance *instance = FindInstance(mWarpTargetMarker);
        IPlayer *player = IPlayer::First(PLAYER_ALL);
        IVehicle *vehicle;
        if (instance != nullptr && instance->GetType() == kGameplayObjType_Marker && player != nullptr && player->GetSimable() != nullptr &&
            player->GetSimable()->QueryInterface(&vehicle)) {
            GMarker *marker = static_cast<GMarker *>(instance);
            vehicle->SetVehicleOnGround(marker->GetPosition(), marker->GetDirection());
            vehicle->Activate();
        }
        mWarpTargetMarker = 0;
        if (mWarpStartPursuit) {
            World_RestoreProps();
            GPS_Disengage();
            if (ITrafficMgr::Exists()) {
                ITrafficMgr::Get()->FlushAllTraffic(true);
            }
            if (ICopMgr::Exists()) {
                ICopMgr::Get()->ResetCopsForRestart(true);
                ICopMgr::Get()->LockoutCops(false);
                ICopMgr::Get()->PursueAtHeatLevel(1);
            }
        }
    }
    if (mWarpStartPursuit) {
        if (ICopMgr::Exists() && !ICopMgr::Get()->PlayerPursuitHasCop()) {
            ICopMgr::Get()->PursueAtHeatLevel(1);
            return;
        }
        mWarpStartPursuit = false;
    }
    mWarping = false;
    new EFadeScreenOff(0x161A918);
}

// PC: 0x5F77F0. Every bin's running activity saves its vars and stops.
void GManager::SuspendAllBinActivities() {
    for (unsigned int i = 0; i < GRaceDatabase::Get().GetBinCount(); i++) {
        GRaceBin *bin = GRaceDatabase::Get().GetBin(i);
        GRuntimeInstance *instance = bin != nullptr ? FindInstance(bin->GetCollectionKey()) : nullptr;
        if (instance != nullptr && instance->GetType() == kGameplayObjType_Activity && static_cast<GActivity *>(instance)->GetIsRunning()) {
            static_cast<GActivity *>(instance)->SerializeVars(false);
            static_cast<GActivity *>(instance)->Suspend();
        }
    }
}

// PC: 0x5FDCB0. In free roam, a race picked from the career menus (the rap
// sheet's races, the rival) runs straight away: a race of another bin
// changes the bin first, its vaults load and its activity starts.
void GManager::StartRaceFromInGame(uint32 raceHash) {
    GRaceParameters *parms = GRaceDatabase::Get().GetRaceFromHash(raceHash);
    if (GRaceStatus::Get().GetPlayMode() != GRaceStatus::kPlayMode_Roaming || parms == nullptr) {
        return;
    }
    GRaceBin *bin = GRaceStatus::Get().GetCurrentBin();
    if (bin != nullptr && parms->GetParentVault() != bin->GetChildVault()) {
        SuspendAllBinActivities();
        GRaceStatus::Get().RefreshBinAfterRace();
    }
    parms->BlockUntilLoaded();
    GActivity *activity = parms->GetActivity();
    if (activity != nullptr) {
        activity->Reset();
        activity->Run();
    }
}

// ---------------------------------------------------------------------------
// SMS: the career's text messages (and voiced cell calls), queued until the
// player can be shown them.
// ---------------------------------------------------------------------------

// PC: 0x5F5120. The cell calls (10-14, 0x5F, 0x60) go out at once; any other
// message not received yet is queued once.
void GManager::AddSMS(int smsID) {
    if ((smsID >= 10 && smsID <= 14) || smsID == 0x5F || smsID == 0x60) {
        DispatchSMSMessage(smsID);
        return;
    }
    SMSMessage *message = FEDatabase->GetCareerSettings()->GetSMSMessage(smsID);
    if (message != nullptr && message->GetFlags() != 0) {
        return;
    }
    for (PendingSMSList::iterator it = mPendingSMS.begin(); it != mPendingSMS.end(); ++it) {
        if (*it == smsID) {
            return;
        }
    }
    mPendingSMS.push_back(smsID);
}

// PC: 0x5DDD20. A cell call not received yet: received (unread), sorted in,
// and called in (ECellCall).
void GManager::DispatchSMSMessage(int smsID) {
    CareerSettings *career = FEDatabase->GetCareerSettings();
    SMSMessage *message = career->GetSMSMessage(smsID);
    if (message == nullptr || message->GetFlags() != 0) {
        return;
    }
    message->Flags = 2;
    message->SetSortOrder(career->GetSMSSortOrder());
    new ECellCall(smsID);
}

// PC: 0x5EC160. Every queued message lands in the inbox (unread, sorted in);
// the one to show is the first voiced one, otherwise the first queued.
int GManager::PushSMSToInbox() {
    CareerSettings *career = FEDatabase->GetCareerSettings();
    int show = -1;
    for (PendingSMSList::iterator it = mPendingSMS.begin(); it != mPendingSMS.end(); ++it) {
        SMSMessage *message = career->GetSMSMessage(*it);
        if (message == nullptr) {
            continue;
        }
        if (message->IsVoice() && show == -1) {
            show = *it;
        }
        message->Flags = 2;
        message->SetSortOrder(career->GetSMSSortOrder());
    }
    if (show == -1 && !mPendingSMS.empty()) {
        show = mPendingSMS.front();
    }
    mPendingSMS.clear();
    return show;
}

// PC: 0x5EDFC0
bool GManager::GetHasPendingSMS() const {
    return !mPendingSMS.empty() && CanPlaySMS();
}

// PC: 0x5EC040. In free roam, in game, with nothing on screen (no NIS, photo
// finish or pause), no cop speech, the HUD up, and the player's car neither
// staging, loading, animating nor in a pursuit.
bool GManager::CanPlaySMS() const {
    if (Speech::Manager::SpeechDisabled() || TheGameFlowManager.GetState() != GAMEFLOW_STATE_RACING || INIS::Exists() || PhotoFinishScreen::mActive ||
        !GRaceStatus::Exists() || GRaceStatus::Get().GetPlayMode() != GRaceStatus::kPlayMode_Roaming) {
        return false;
    }
    if (!FEManager::IsOkayToRequestPauseSimulation(0, true, true) || Speech::Manager::IsCopSpeechPlaying(static_cast<SPCHType_1_EventID>(0xCC))) {
        return false;
    }
    IPlayer *player = IPlayer::First(PLAYER_LOCAL);
    ISimable *simable = player != nullptr ? player->GetSimable() : nullptr;
    IHud *hud = player != nullptr ? player->GetHud() : nullptr;
    if (simable == nullptr || hud == nullptr || !hud->IsHudVisible()) {
        return false;
    }
    IVehicle *vehicle;
    if (!simable->QueryInterface(&vehicle) || vehicle == nullptr || vehicle->IsAnimating() || vehicle->IsStaging() || vehicle->IsLoading()) {
        return false;
    }
    IVehicleAI *ai = vehicle->GetAIVehiclePtr();
    return ai != nullptr && ai->GetPursuit() == nullptr;
}

// PC: 0x5EDFE0. When the player can be shown them, the queued messages go to
// the inbox, the inbox opens on one (EShowSMS) and the HUD shows the phone.
void GManager::UpdatePendingSMS() {
    if (mPendingSMS.empty() || !CanPlaySMS()) {
        return;
    }
    int show = PushSMSToInbox();
    if (show != -1) {
        new EShowSMS(show);
    }
    IPlayer *player = IPlayer::First(PLAYER_ALL);
    IHud *hud = player != nullptr ? player->GetHud() : nullptr;
    IMenuZoneTrigger *izone;
    if (hud != nullptr && hud->QueryInterface(&izone)) {
        izone->RequestCingularLogo();
    }
}

// PC: 0x5E48F0. Shows the pursuit breaker icons of a section again (all of
// them for -1).
void GManager::RestorePursuitBreakerIcons(int sectionID) {
    for (unsigned int i = 0; i < mNumIcons; i++) {
        GIcon *icon = mIcons[i];
        if (icon->GetType() == GIcon::kType_PursuitBreaker &&
            (sectionID == -1 || sectionID == icon->GetSectionID() || sectionID == icon->GetCombinedSectionID())) {
            icon->SetFlag(1);
        }
    }
}
#endif

#if defined(EA_PORT_NATIVE)
// ---------------------------------------------------------------------------
// Object state blocks and the saved gameplay data (the career's first 0x4000
// bytes in a profile).
// ---------------------------------------------------------------------------

// PC: 0x5ED960. The session block first.
void *GManager::GetObjectStateBlock(Attrib::Key collectionKey) {
    const uint32 key32 = Get32BitCollectionKey(collectionKey);
    for (unsigned int i = 0; i < 2; i++) {
        ObjectStateMap &blocks = i == 0 ? mSessionStateBlocks : mPersistentStateBlocks;
        ObjectStateMap::iterator it = blocks.find(key32);
        if (it != blocks.end()) {
            return it->second + 1;
        }
    }
    return nullptr;
}

// PC: 0x5F30F0. The storage is only reclaimed by the next defrag.
void GManager::ClearObjectStateBlock(Attrib::Key collectionKey) {
    const uint32 key32 = Get32BitCollectionKey(collectionKey);
    for (unsigned int i = 0; i < 2; i++) {
        ObjectStateMap &blocks = i == 0 ? mSessionStateBlocks : mPersistentStateBlocks;
        ObjectStateMap::iterator it = blocks.find(key32);
        if (it != blocks.end()) {
            blocks.erase(it);
        }
    }
}

// PC: 0x5F5D40. Moves the live blocks down to the start of the buffer, in
// address order.
void GManager::DefragObjectStateStorage() {
    std::vector<ObjectStateBlockHeader *> live;
    live.reserve(mPersistentStateBlocks.size() + mSessionStateBlocks.size());
    for (ObjectStateMap::iterator it = mPersistentStateBlocks.begin(); it != mPersistentStateBlocks.end(); ++it) {
        live.push_back(it->second);
    }
    for (ObjectStateMap::iterator it = mSessionStateBlocks.begin(); it != mSessionStateBlocks.end(); ++it) {
        live.push_back(it->second);
    }
    std::sort(live.begin(), live.end());
    uint8 *dest = mObjectStateBuffer;
    for (std::size_t i = 0; i < live.size(); i++) {
        ObjectStateBlockHeader *block = live[i];
        const unsigned int size = (block->mSize + 0x17) & ~0xFu;
        if (reinterpret_cast<uint8 *>(block) != dest) {
            std::memmove(dest, block, size);
            const uint32 key32 = Get32BitCollectionKey(reinterpret_cast<ObjectStateBlockHeader *>(dest)->mKey);
            for (unsigned int m = 0; m < 2; m++) {
                ObjectStateMap &blocks = m == 0 ? mSessionStateBlocks : mPersistentStateBlocks;
                ObjectStateMap::iterator it = blocks.find(key32);
                if (it != blocks.end() && it->second == block) {
                    it->second = reinterpret_cast<ObjectStateBlockHeader *>(dest);
                }
            }
        }
        dest += size;
    }
    mObjectStateBufferFree = dest;
}

// PC: 0x5F5F10. Blocks are 16-byte aligned with an 8-byte header.
void *GManager::AllocObjectStateBlock(Attrib::Key collectionKey, unsigned int size, bool persistent) {
    ObjectStateMap &blocks = persistent ? mPersistentStateBlocks : mSessionStateBlocks;
    const uint32 key32 = Get32BitCollectionKey(collectionKey);
    ObjectStateMap::iterator it = blocks.find(key32);
    if (it != blocks.end()) {
        ObjectStateBlockHeader *block = it->second;
        if (size <= block->mSize) {
            block->mSize = size;
            return block + 1;
        }
        ClearObjectStateBlock(collectionKey);
    }
    const unsigned int alignedSize = (size + 0x17) & ~0xFu;
    for (unsigned int attempt = 0; attempt < 2; attempt++) {
        if (static_cast<unsigned int>(mObjectStateBufferFree - mObjectStateBuffer) + alignedSize > mObjectStateBufferSize) {
            if (attempt != 0) {
                return nullptr;
            }
            DefragObjectStateStorage();
        }
    }
    ObjectStateBlockHeader *block = reinterpret_cast<ObjectStateBlockHeader *>(mObjectStateBufferFree);
    mObjectStateBufferFree += alignedSize;
    if (persistent) {
        std::memset(block, 0, alignedSize);
    }
    block->mSize = size;
    block->mKey = collectionKey;
    blocks[key32] = block;
    return block + 1;
}

// PC: 0x5E4B10
void GManager::ResetTimers() {
    for (unsigned int i = 0; i < 8; i++) {
        mTimers[i].mRunning = false;
        mTimers[i].mElapsed = 0.0f;
        mTimers[i].mInterval = 1.0f;
        mTimers[i].mNameHash = 0;
        mTimers[i].mName[0] = '\0';
    }
}

// PC: 0x5E4A40. (Re)starts the named timer, else takes a stopped one.
bool GManager::SetTimer(const char *name, float interval) {
    const uint32 hash = bStringHash(name);
    for (unsigned int i = 0; i < 8; i++) {
        if (mTimers[i].mNameHash == hash) {
            mTimers[i].mInterval = interval;
            mTimers[i].mElapsed = 0.0f;
            mTimers[i].mRunning = true;
            return true;
        }
    }
    for (unsigned int i = 0; i < 8; i++) {
        if (!mTimers[i].mRunning) {
            bSafeStrCpy(mTimers[i].mName, name, 0x13);
            mTimers[i].mNameHash = bStringHash(mTimers[i].mName);
            mTimers[i].mInterval = interval;
            mTimers[i].mElapsed = 0.0f;
            mTimers[i].mRunning = true;
            return true;
        }
    }
    return false;
}

// PC: 0x5E4AD0
void GManager::KillTimer(const char *name) {
    const uint32 hash = bStringHash(name);
    for (unsigned int i = 0; i < 8; i++) {
        if (mTimers[i].mNameHash == hash) {
            mTimers[i].mRunning = false;
            mTimers[i].mElapsed = 0.0f;
            return;
        }
    }
}

// PC: 0x5DDA40
unsigned int GManager::GetNumMilestones() {
    return mNumMilestones;
}

// PC: 0x5E4C40
unsigned int GManager::SaveTimerInfo(SavedTimerInfo *saveInfo) {
    for (unsigned int i = 0; i < 8; i++) {
        saveInfo[i].mElapsed = mTimers[i].mElapsed;
        saveInfo[i].mInterval = mTimers[i].mInterval;
        saveInfo[i].mRunning = mTimers[i].mRunning;
        bSafeStrCpy(saveInfo[i].mName, mTimers[i].mName, 0x13);
    }
    return 8;
}

// PC: 0x5E4C90
void GManager::LoadTimerInfo(SavedTimerInfo *saveInfo, unsigned int count) {
    for (unsigned int i = 0; i < count && i < 8; i++) {
        mTimers[i].mElapsed = saveInfo[i].mElapsed;
        mTimers[i].mInterval = saveInfo[i].mInterval;
        mTimers[i].mRunning = saveInfo[i].mRunning;
        bSafeStrCpy(mTimers[i].mName, saveInfo[i].mName, 0x13);
        mTimers[i].mNameHash = bStringHash(mTimers[i].mName);
    }
}

// PC: 0x5F62F0. One entry per milestone type collection, with no values yet.
void GManager::ResetMilestoneTrackingInfo() {
    mMilestoneTypeInfo.clear();
    if (mMilestoneClass == nullptr) {
        return;
    }
    for (Attrib::Key key = mMilestoneClass->GetFirstCollection(); key != 0; key = mMilestoneClass->GetNextCollection(key)) {
        Attrib::Gen::milestonetypes type(key, 0, nullptr);
        MilestoneTypeInfo info;
        info.mTypeKey = key;
        info.mLastKnownValue = -1.0f;
        info.mBestValue = -1.0f;
        info.mFlags = type.MilestoneType() != 2 ? 1 : 0;
        if (type.ResetWhenPursuitStarts()) {
            info.mFlags |= 2;
        }
        mMilestoneTypeInfo[key] = info;
    }
}

// PC: 0x5EDCB0
unsigned int GManager::SaveMilestoneInfo(MilestoneTypeInfo *dest) {
    for (MilestoneInfoMap::iterator it = mMilestoneTypeInfo.begin(); it != mMilestoneTypeInfo.end(); ++it) {
        *dest++ = it->second;
    }
    return static_cast<unsigned int>(mMilestoneTypeInfo.size());
}

// PC: 0x5F6430
void GManager::LoadMilestoneInfo(MilestoneTypeInfo *savedInfo, unsigned int count) {
    ResetMilestoneTrackingInfo();
    for (unsigned int i = 0; i < count; i++) {
        MilestoneInfoMap::iterator it = mMilestoneTypeInfo.find(savedInfo[i].mTypeKey);
        if (it != mMilestoneTypeInfo.end()) {
            it->second.mBestValue = savedInfo[i].mBestValue;
            it->second.mLastKnownValue = savedInfo[i].mLastKnownValue;
        }
    }
}


// PC: inlined into SaveGameplayData.
unsigned int GManager::SaveMilestones(GMilestone *dest) {
    if (mNumMilestones != 0) {
        std::memcpy(dest, mMilestones, mNumMilestones * sizeof(GMilestone));
    }
    return mNumMilestones;
}

// PC: 0x5DDBD0
void GManager::LoadMilestones(GMilestone *src, unsigned int count) {
    for (; count != 0; count--, src++) {
        for (unsigned int i = 0; i < mNumMilestones; i++) {
            if (mMilestones[i].GetChallengeKey() == src->GetChallengeKey()) {
                mMilestones[i] = *src;
                break;
            }
        }
    }
}

// PC: inlined into SaveGameplayData.
unsigned int GManager::SaveSpeedTraps(GSpeedTrap *dest) {
    if (mNumSpeedTraps != 0) {
        std::memcpy(dest, mSpeedTraps, mNumSpeedTraps * sizeof(GSpeedTrap));
    }
    return mNumSpeedTraps;
}

// PC: inlined into SaveGameplayData. The PC steps two slots per message, so
// with more than one pending only every other one is saved (and the rest of
// the slots stay zero); kept for the format.
unsigned int GManager::SaveSMSInfo(int *saveInfo) {
    const unsigned int count = static_cast<unsigned int>(mPendingSMS.size());
    PendingSMSList::iterator it = mPendingSMS.begin();
    for (unsigned int i = 0; i < count; i += 2, ++it) {
        saveInfo[i] = *it;
    }
    return count;
}

// PC: 0x5F50C0
void GManager::LoadSMSInfo(int *loadInfo, unsigned int count) {
    mPendingSMS.clear();
    for (; count != 0; count--) {
        mPendingSMS.push_back(*loadInfo++);
    }
}

// PC: 0x5F7730
void GManager::ResetAllGameplayData() {
    mPersistentStateBlocks.clear();
    mSessionStateBlocks.clear();
    mObjectStateBufferFree = mObjectStateBuffer;
    ResetMilestoneTrackingInfo();
    ResetMilestones();
    ResetSpeedTraps();
    bMemSet(mHidingSpotFound, 0, sizeof(mHidingSpotFound));
    ResetTimers();
    mFreeRoamStartMarker = 0;
    mOverrideFreeRoamStartMarker = 0;
    mFreeRoamFromSafeHouseStartMarker = 0;
    mStartFreeRoamFromSafeHouse = false;
    mPendingSMS.clear();
}

namespace {
// The PC aligns the addresses; its buffers are 16-byte aligned, so natively
// the offsets are aligned instead, for the same layout whatever the buffer.
inline uint8 *AlignSave16(uint8 *base, uint8 *p) {
    return base + ((p - base + 0xF) & ~static_cast<intptr_t>(0xF));
}
} // namespace

// PC: 0x5F7410. A SavedGameplayDataHeader (with the MD5 of the rest of the
// buffer first), then from 0x80, each section 16-byte aligned: the persistent
// object state blocks, the timers, the milestone types, the milestones, the
// speed traps, the hiding spot bits, the race bins, the pending SMS and the
// free roam start markers. (The alignment is of the addresses, as on PC.)
bool GManager::SaveGameplayData(uint8 *dest, unsigned int maxSize) {
    static_assert(sizeof(GMilestone) == 0x14 && sizeof(GSpeedTrap) == 0x14 && sizeof(SavedTimerInfo) == 0x20 && sizeof(MilestoneTypeInfo) == 0x10,
                  "PC gameplay data layout");
    bMemSet(dest, 0, maxSize);
    for (GObjectIterator<GActivity> iter(0xffffffff); iter.IsValid(); iter.Advance()) {
        iter.GetInstance()->SerializeVars(false);
    }
    if (maxSize < 0x80) {
        return false;
    }
    SavedGameplayDataHeader *header = reinterpret_cast<SavedGameplayDataHeader *>(dest);
    header->mMagic = 0x656d6147; // 'Game'
    header->mVersion = 8;
    header->mNumPersistent = static_cast<uint32>(mPersistentStateBlocks.size());
    uint8 *p = dest + 0x80;
    uint8 *end = dest + maxSize;
    for (ObjectStateMap::iterator it = mPersistentStateBlocks.begin(); it != mPersistentStateBlocks.end(); ++it) {
        const unsigned int size = (it->second->mSize + 0x17) & ~0xFu;
        if (p + size > end) {
            return false;
        }
        std::memcpy(p, it->second, size);
        p += size;
    }
    p = AlignSave16(dest, p);
    header->mNumSavedTimers = SaveTimerInfo(reinterpret_cast<SavedTimerInfo *>(p));
    p += header->mNumSavedTimers * sizeof(SavedTimerInfo);
    header->mNumMilestoneTypes = SaveMilestoneInfo(reinterpret_cast<MilestoneTypeInfo *>(p));
    p += header->mNumMilestoneTypes * sizeof(MilestoneTypeInfo);
    header->mNumMilestoneRecords = SaveMilestones(reinterpret_cast<GMilestone *>(p));
    p = AlignSave16(dest, p + header->mNumMilestoneRecords * sizeof(GMilestone));
    header->mNumSpeedTrapRecords = SaveSpeedTraps(reinterpret_cast<GSpeedTrap *>(p));
    p = AlignSave16(dest, p + header->mNumSpeedTrapRecords * sizeof(GSpeedTrap));
    header->mNumHidingSpotFlags = sizeof(mHidingSpotFound) * 8;
    std::memcpy(p, mHidingSpotFound, sizeof(mHidingSpotFound));
    p += sizeof(mHidingSpotFound);
    header->mNumBytesBinStats = GRaceDatabase::Exists() ? GRaceDatabase::Get().SerializeBins(p) : 0;
    p = AlignSave16(dest, p + header->mNumBytesBinStats);
    header->mNumPendingSMS = SaveSMSInfo(reinterpret_cast<int *>(p));
    p = AlignSave16(dest, p + header->mNumPendingSMS * sizeof(int));
    *reinterpret_cast<uint32 *>(p) = mFreeRoamStartMarker;
    *reinterpret_cast<uint32 *>(p + 0x10) = mFreeRoamFromSafeHouseStartMarker;
    MD5 md5;
    md5.Update(dest + 0x10, static_cast<int>(maxSize - 0x10));
    std::memcpy(header->mChecksum, md5.GetRaw(), sizeof(header->mChecksum));
    return true;
}

// PC: 0x5F81D0
bool GManager::LoadGameplayData(uint8 *src, unsigned int maxSize) {
    if (maxSize < 0x80) {
        return false;
    }
    SavedGameplayDataHeader *header = reinterpret_cast<SavedGameplayDataHeader *>(src);
    MD5 md5;
    md5.Update(src + 0x10, static_cast<int>(maxSize - 0x10));
    if (std::memcmp(md5.GetRaw(), header->mChecksum, sizeof(header->mChecksum)) != 0 || header->mMagic != 0x656d6147 || header->mVersion < 8) {
        return false;
    }
    ResetAllGameplayData();
    uint8 *p = src + 0x80;
    for (uint32 i = 0; i < header->mNumPersistent; i++) {
        ObjectStateBlockHeader *saved = reinterpret_cast<ObjectStateBlockHeader *>(p);
        void *block = AllocObjectStateBlock(saved->mKey, saved->mSize, true);
        if (block != nullptr) {
            std::memcpy(block, saved + 1, saved->mSize);
        }
        p += (saved->mSize + 0x17) & ~0xFu;
    }
    p = AlignSave16(src, p);
    LoadTimerInfo(reinterpret_cast<SavedTimerInfo *>(p), header->mNumSavedTimers);
    p += header->mNumSavedTimers * sizeof(SavedTimerInfo);
    LoadMilestoneInfo(reinterpret_cast<MilestoneTypeInfo *>(p), header->mNumMilestoneTypes);
    p += header->mNumMilestoneTypes * sizeof(MilestoneTypeInfo);
    LoadMilestones(reinterpret_cast<GMilestone *>(p), header->mNumMilestoneRecords);
    p = AlignSave16(src, p + header->mNumMilestoneRecords * sizeof(GMilestone));
    LoadSpeedTraps(reinterpret_cast<GSpeedTrap *>(p), header->mNumSpeedTrapRecords);
    p = AlignSave16(src, p + header->mNumSpeedTrapRecords * sizeof(GSpeedTrap));
    const unsigned int hidingBytes = (header->mNumHidingSpotFlags + 7) >> 3;
    std::memcpy(mHidingSpotFound, p, hidingBytes < sizeof(mHidingSpotFound) ? hidingBytes : sizeof(mHidingSpotFound));
    p = AlignSave16(src, p + hidingBytes);
    if (GRaceDatabase::Exists()) {
        p = AlignSave16(src, p + GRaceDatabase::Get().DeserializeBins(p));
    }
    LoadSMSInfo(reinterpret_cast<int *>(p), header->mNumPendingSMS);
    p = AlignSave16(src, p + header->mNumPendingSMS * sizeof(int));
    // The saved start markers are only taken when they still exist.
    const uint32 freeRoamStart = *reinterpret_cast<uint32 *>(p);
    if (Attrib::Gen::gameplay(freeRoamStart, 0, nullptr).IsValid()) {
        mFreeRoamStartMarker = freeRoamStart;
    }
    const uint32 safeHouseStart = *reinterpret_cast<uint32 *>(p + 0x10);
    if (Attrib::Gen::gameplay(safeHouseStart, 0, nullptr).IsValid()) {
        mFreeRoamFromSafeHouseStartMarker = safeHouseStart;
    }
    return true;
}
#endif
