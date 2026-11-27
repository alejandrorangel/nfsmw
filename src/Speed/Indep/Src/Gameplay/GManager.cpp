#include "Speed/Indep/Src/Gameplay/GManager.h"

#include "Speed/Indep/Src/Gameplay/GActivity.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
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
    // TODO(port): icons, milestones, bounty spawn points and the section icon
    // callback aren't ported yet.
#if defined(EA_PORT_NATIVE)
    AllocateSpeedTraps();
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
    for (unsigned int onVault = 0; onVault < mVaultCount; onVault++) {
        if (mVaults[onVault].IsLoaded()) {
            mVaults[onVault].CreateGameplayObjects();
        }
    }
    GRaceCustom *startupRace = GRaceDatabase::Get().GetStartupRace();
    if (startupRace != nullptr) {
        startupRace->CreateRaceActivity();
    }
    ConnectRuntimeInstances();
    StartActivities();
    // TODO(port): WCollisionAssets::AddPackLoadCallback(NotifyCollisionPackLoaded)
    // (icons per collision pack aren't ported).
    mPursuitBreakerIconsShown = false;
    mHidingSpotIconsShown = false;
    mEventIconsShown = false;
    mMenuGateIconsShown = false;
    mSpeedTrapIconsShown = false;
    mSpeedTrapRaceIconsShown = false;
    // TODO(port): RefreshEngageTriggerIcons, SpawnAllLoadedSectionIcons
    // (icons aren't ported).
#if defined(EA_PORT_NATIVE)
    RefreshSpeedTrapIcons();
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
    // TODO(port): UnspawnUselessCharacters, ServicePendingCharacters and
    // UpdatePursuit (characters and the pursuit bookkeeping aren't ported).
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
#endif
    // TODO(port): UpdatePendingSMS, UpdateTriggerAvailability and
    // UpdateIconVisibility (SMS, triggers and icons aren't ported).
}

#if defined(EA_PORT_NATIVE)
// Milestone value tracking and the pursuit notifications. The milestone type
// table (mMilestoneTypeInfo) and the milestones themselves aren't allocated
// natively yet (AllocateMilestones/ResetMilestoneTrackingInfo aren't ported),
// so these mostly find nothing; PC looks values up with find() and writes
// through whatever it returns, natively an unknown value is ignored.

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

// PC: 0x5F4D40 resets every milestone from its challenge collection.
// TODO(port): the milestones aren't allocated natively (AllocateMilestones),
// so there are none to reset.
void GManager::ResetMilestones() {}

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
