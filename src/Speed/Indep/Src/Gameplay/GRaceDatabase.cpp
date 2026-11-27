#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"

#include "Speed/Indep/Libs/Support/Utility/FastMem.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/Frontend/HUD/feMinimap.hpp"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GObjectBlock.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Gameplay/GVault.h"
#include "Speed/Indep/Src/Misc/Config.h"
#include "Speed/Indep/Src/World/TimeOfDay.hpp"
#include "Speed/Indep/Src/World/TrackInfo.hpp"
#include "Speed/Indep/Src/World/WCollisionAssets.h"
#include "Speed/Indep/Src/World/WRoadNetwork.h"
#include "Speed/Indep/bWare/Inc/bMemory.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include <new>

// Defined in Misc/GameFlow.cpp.
void EnableBarrierSceneryGroup(const char *group_name, bool flip_artwork);
void RedoTopologyAndSceneryGroups();

const char GRaceDatabase::sDDayRaces[5][8] = {"16.1.0", "16.2.2", "16.2.3", "16.1.1", "16.2.1"};

GRaceDatabase *GRaceDatabase::mObj = nullptr;

// Bits of GRaceIndexData::mFlags (PC: GRaceParameters::GenerateIndex, 0x600CD0).
enum RaceIndexFlags {
    kIndexFlag_UnlockedQuickRace = 1 << 0,
    kIndexFlag_UnlockedOnline = 1 << 1,
    kIndexFlag_UnlockedChallenge = 1 << 2,
    kIndexFlag_CanBeReversed = 1 << 3,
    kIndexFlag_DDayRace = 1 << 4,
    kIndexFlag_BossRace = 1 << 5,
    kIndexFlag_MarkerRace = 1 << 6,
    kIndexFlag_PursuitRace = 1 << 7,
    kIndexFlag_LoopingRace = 1 << 8,
    kIndexFlag_RankByPoints = 1 << 9,
    kIndexFlag_RankByDistance = 1 << 10,
    kIndexFlag_CopsEnabled = 1 << 11,
    kIndexFlag_ScriptedCops = 1 << 12,
    kIndexFlag_Sunset = 1 << 13,
    kIndexFlag_NeverInQuickRace = 1 << 14,
    kIndexFlag_ChallengeSeries = 1 << 15,
    kIndexFlag_CollectorsEdition = 1 << 16,
    kIndexFlag_Midday = 1 << 17,
};

// Writes value into a (dynamic) collection's attribute, adding it if the
// collection doesn't have its own copy yet (PC: GRaceCustom::SetAttribute,
// 0x5E74C0 / 0x5E7550 / 0x5E75E0).
namespace {

struct AttributeAccess : public Attrib::Attribute {
    using Attrib::Attribute::GetElementPointer;
};

template <typename T> bool SetCollectionAttribute(Attrib::Gen::gameplay &record, Attrib::Key key, const T &value, unsigned int index) {
    record.Add(key, 1);
    Attrib::Attribute attrib = record.Get(key);
    void *(Attrib::Attribute::*getElement)(unsigned int) const = &AttributeAccess::GetElementPointer;
    T *dest = static_cast<T *>((attrib.*getElement)(index));
    if (dest == nullptr) {
        return false;
    }
    *dest = value;
    return true;
}

// PC: GRuntimeInstance ring lists (0x5E1820 activity, 0x5E1880 character,
// 0x5E18C0 marker).
template <typename T> T *FindRuntimeInstance(Attrib::Key key) {
    return GRuntimeInstance::FindObject<T>(key);
}

} // namespace

// ---------------------------------------------------------------------------
// GRaceParameters
// ---------------------------------------------------------------------------

// PC: inlined in every accessor below (same as BlockUntilLoaded, 0x5FA960).
void GRaceParameters::EnsureLoaded() const {
    if (mParentVault != nullptr && !mParentVault->IsLoaded()) {
        GManager::Get().LoadVaultSync(mParentVault);
    }
    if (mChildVault != nullptr && !mChildVault->IsLoaded()) {
        GManager::Get().LoadVaultSync(mChildVault);
    }
}

void GRaceParameters::BlockUntilLoaded() {
    EnsureLoaded();
#if defined(EA_PORT_NATIVE)
    // Every vault stays loaded natively, but on PC a race's vault streamed in
    // during gameplay (engaged from free roam) is when its objects (start
    // grid, checkpoints, characters) are made: GManager::LoadVaultSync keeps
    // the slots' objects as PC does.
    if (GManager::Get().GetInGameplay()) {
        if (mParentVault != nullptr && mParentVault->IsTransient() && mParentVault->GetObjectBlock() == nullptr) {
            GManager::Get().LoadVaultSync(mParentVault);
        }
        if (mChildVault != nullptr && mChildVault->IsTransient() && mChildVault->GetObjectBlock() == nullptr) {
            GManager::Get().LoadVaultSync(mChildVault);
        }
    }
#endif
}

// PC: 0x5E3A00.
bool GRaceParameters::GetIsLoaded() const {
    if (mParentVault != nullptr && !mParentVault->IsLoaded()) {
        return false;
    }
    if (mChildVault != nullptr && !mChildVault->IsLoaded()) {
        return false;
    }
    return true;
}

// PC: 0x602590.
GRaceParameters::GRaceParameters(unsigned int collectionKey, GRaceIndexData *index) {
    mIndex = nullptr;
    mRaceRecord = new Attrib::Gen::gameplay(collectionKey, 0, nullptr);
    mParentVault = nullptr;
    mChildVault = nullptr;
    GenerateIndex(index);
    mIndex = index;

    const char *childVaultName = mRaceRecord->gameplayvault();
    if (childVaultName != nullptr) {
        mChildVault = GManager::Get().FindVault(childVaultName);
        // PC reads Num_Children() here and discards it.
        mRaceRecord->Num_Children();
    }
    mParentVault = GManager::Get().FindVaultContaining(mRaceRecord->GetCollection());
}

// PC: 0x5E9980.
GRaceParameters::~GRaceParameters() {
    if (mRaceRecord != nullptr) {
        delete mRaceRecord;
        mRaceRecord = nullptr;
    }
    mIndex = nullptr;
}

// PC: 0x600CD0. Caches what the frontend lists so a race can be shown while
// its vaults are streamed out.
void GRaceParameters::GenerateIndex(GRaceIndexData *index) {
    if (index == nullptr) {
        return;
    }
    index->mKey = GetCollectionKey();
    index->mRaceHash = GetEventHash();
    index->mLocalizationTag = static_cast<short>(GetLocalizationTag());
    index->mLength = GetRaceLengthMeters();
    index->mRivalBest = udecfix16(GetRivalBestTime());
    index->mReputation = decimal16(static_cast<float>(GetReputation()));
    index->mCash = decimal16(GetCashValue());
    index->mChallengeType = GetChallengeType();
    index->mChallengeGoal = decimal16(GetChallengeGoal());
    index->mNumLaps = static_cast<uint8>(GetNumLaps());
    index->mCopDensity = static_cast<int8>(GetCopDensity());
    index->mRaceType = static_cast<int8>(GetRaceType());
    index->mRegion = static_cast<uint8>(GetRegion());

    index->mFlags = 0;
    if (GetInitiallyUnlockedQuickRace()) {
        index->mFlags |= kIndexFlag_UnlockedQuickRace;
    }
    if (GetInitiallyUnlockedOnline()) {
        index->mFlags |= kIndexFlag_UnlockedOnline;
    }
    if (GetInitiallyUnlockedChallenge()) {
        index->mFlags |= kIndexFlag_UnlockedChallenge;
    }
    if (GetCanBeReversed()) {
        index->mFlags |= kIndexFlag_CanBeReversed;
    }
    if (GetIsDDayRace()) {
        index->mFlags |= kIndexFlag_DDayRace;
    }
    if (GetIsBossRace()) {
        index->mFlags |= kIndexFlag_BossRace;
    }
    if (GetIsMarkerRace()) {
        index->mFlags |= kIndexFlag_MarkerRace;
    }
    if (GetIsPursuitRace()) {
        index->mFlags |= kIndexFlag_PursuitRace;
    }
    if (GetIsLoopingRace()) {
        index->mFlags |= kIndexFlag_LoopingRace;
    }
    if (GetRankPlayersByPoints()) {
        index->mFlags |= kIndexFlag_RankByPoints;
    }
    if (GetRankPlayersByDistance()) {
        index->mFlags |= kIndexFlag_RankByDistance;
    }
    if (GetCopsEnabled()) {
        index->mFlags |= kIndexFlag_CopsEnabled;
    }
    if (GetScriptedCopsInRace()) {
        index->mFlags |= kIndexFlag_ScriptedCops;
    }
    if (GetTimeOfDay() > 0.8f) {
        index->mFlags |= kIndexFlag_Sunset;
    }
    if (GetNeverInQuickRace()) {
        index->mFlags |= kIndexFlag_NeverInQuickRace;
    }
    if (GetIsChallengeSeriesRace()) {
        index->mFlags |= kIndexFlag_ChallengeSeries;
    }
    if (GetIsCollectorsEditionRace()) {
        index->mFlags |= kIndexFlag_CollectorsEdition;
    }
    if (!(GetTimeOfDay() < 0.8f)) {
        index->mFlags |= kIndexFlag_Sunset;
    } else if (!(GetTimeOfDay() < 0.0f)) {
        index->mFlags |= kIndexFlag_Midday;
    }

    bMemSet(index->mEventID, 0, sizeof(index->mEventID));
    bSafeStrCpy(index->mEventID, GetEventID(), sizeof(index->mEventID));

    UMath::Vector2 topLeft;
    UMath::Vector2 botRight;
    GetBoundingBox(topLeft, botRight);
    index->mMapX1 = static_cast<uint8>(static_cast<int>(topLeft.x * 255.0f));
    index->mMapY1 = static_cast<uint8>(static_cast<int>(topLeft.y * 255.0f));
    index->mMapX2 = static_cast<uint8>(static_cast<int>(botRight.x * 255.0f));
    index->mMapY2 = static_cast<uint8>(static_cast<int>(botRight.y * 255.0f));
}

// PC: 0x5E3AE0-ish (inlined in GRaceDatabase::NotifyVaultUnloading, 0x5E38D0).
void GRaceParameters::NotifyParentVaultUnloading() {
    if (mRaceRecord != nullptr) {
        delete mRaceRecord;
        mRaceRecord = nullptr;
    }
}

// PC: 0x5E3A70.
void GRaceParameters::NotifyParentVaultLoaded() {
    if (mRaceRecord == nullptr) {
        mRaceRecord = new Attrib::Gen::gameplay(mIndex->mKey, 0, nullptr);
    }
}

const Attrib::Gen::gameplay *GRaceParameters::GetGameplayObj() const {
    return mRaceRecord;
}

GVault *GRaceParameters::GetChildVault() const {
    return mChildVault;
}

GVault *GRaceParameters::GetParentVault() const {
    return mParentVault;
}

// PC: 0x5FCE40.
GActivity *GRaceParameters::GetActivity() const {
    if (mIndex != nullptr) {
        return FindRuntimeInstance<GActivity>(mIndex->mKey);
    }
    EnsureLoaded();
    return FindRuntimeInstance<GActivity>(mRaceRecord->GetCollection());
}

// PC: 0x5FB800.
unsigned int GRaceParameters::GetCollectionKey() const {
    if (mIndex != nullptr) {
        return mIndex->mKey;
    }
    EnsureLoaded();
    return mRaceRecord->GetCollection();
}

// Accessors that prefer the index and otherwise read the race record.
#define GRACE_INDEXED_GETTER(RET, NAME, INDEXEXPR, RECORDEXPR)                                                                                       \
    RET GRaceParameters::NAME() const {                                                                                                              \
        if (mIndex != nullptr) {                                                                                                                     \
            return INDEXEXPR;                                                                                                                        \
        }                                                                                                                                            \
        EnsureLoaded();                                                                                                                              \
        return RECORDEXPR;                                                                                                                           \
    }

// Accessors that always read the race record.
#define GRACE_RECORD_GETTER(RET, NAME, RECORDEXPR)                                                                                                   \
    RET GRaceParameters::NAME() const {                                                                                                              \
        EnsureLoaded();                                                                                                                              \
        return RECORDEXPR;                                                                                                                           \
    }

GRACE_INDEXED_GETTER(float, GetRaceLengthMeters, mIndex->mLength, mRaceRecord->RaceLength())                                 // 0x5FB850
GRACE_INDEXED_GETTER(int, GetReputation, static_cast<int>(static_cast<float>(mIndex->mReputation)), mRaceRecord->Reputation()) // 0x5FB8B0
GRACE_INDEXED_GETTER(float, GetCashValue, static_cast<float>(mIndex->mCash), mRaceRecord->CashValue())                     // 0x5FB920
GRACE_INDEXED_GETTER(int, GetLocalizationTag, mIndex->mLocalizationTag, mRaceRecord->LocalizationTag())                     // 0x5FB990
GRACE_INDEXED_GETTER(int, GetNumLaps, mIndex->mNumLaps, mRaceRecord->NumLaps())                                             // 0x5FBA00
GRACE_INDEXED_GETTER(const char *, GetEventID, mIndex->mEventID, mRaceRecord->EventID())                                    // 0x5FBA70
GRACE_INDEXED_GETTER(float, GetRivalBestTime, static_cast<float>(mIndex->mRivalBest), mRaceRecord->RivalBestTime())         // 0x5FBAD0
GRACE_INDEXED_GETTER(float, GetChallengeGoal, static_cast<float>(mIndex->mChallengeGoal), mRaceRecord->ThreshholdValue())   // 0x5FBB60
GRACE_INDEXED_GETTER(unsigned int, GetEventHash, mIndex->mRaceHash, Attrib::StringHash32(mRaceRecord->EventID()))           // 0x5FAB40

#define GRACE_FLAG_GETTER(NAME, FLAG, ATTR) GRACE_INDEXED_GETTER(bool, NAME, (mIndex->mFlags & (FLAG)) != 0, mRaceRecord->ATTR())

GRACE_FLAG_GETTER(GetInitiallyUnlockedQuickRace, kIndexFlag_UnlockedQuickRace, AvailableQR)   // 0x5FBBD0
GRACE_FLAG_GETTER(GetInitiallyUnlockedOnline, kIndexFlag_UnlockedOnline, AvailableOnline)     // 0x5FBC40
GRACE_FLAG_GETTER(GetInitiallyUnlockedChallenge, kIndexFlag_UnlockedChallenge, InitiallyUnlocked) // 0x5FBCB0
GRACE_FLAG_GETTER(GetIsDDayRace, kIndexFlag_DDayRace, DDayRace)                              // 0x5FBD20
GRACE_FLAG_GETTER(GetIsBossRace, kIndexFlag_BossRace, BossRace)                              // 0x5FBD90
GRACE_FLAG_GETTER(GetIsMarkerRace, kIndexFlag_MarkerRace, IsMarkerRace)                      // 0x5FBE00
GRACE_FLAG_GETTER(GetIsPursuitRace, kIndexFlag_PursuitRace, PursuitRace)                     // 0x5FBE70
GRACE_FLAG_GETTER(GetIsLoopingRace, kIndexFlag_LoopingRace, IsLoopingRace)                   // 0x5FBEE0
GRACE_FLAG_GETTER(GetRankPlayersByPoints, kIndexFlag_RankByPoints, RankPlayersByPoints)      // 0x5FBF50
GRACE_FLAG_GETTER(GetRankPlayersByDistance, kIndexFlag_RankByDistance, RankPlayersByDistance) // 0x5FBFC0
GRACE_FLAG_GETTER(GetCopsEnabled, kIndexFlag_CopsEnabled, CopsInRace)                        // 0x5FC030
GRACE_FLAG_GETTER(GetScriptedCopsInRace, kIndexFlag_ScriptedCops, ScriptedCopsInRace)        // 0x5FC0A0
GRACE_FLAG_GETTER(GetNeverInQuickRace, kIndexFlag_NeverInQuickRace, NeverInQuickRace)        // 0x5FC110
GRACE_FLAG_GETTER(GetIsChallengeSeriesRace, kIndexFlag_ChallengeSeries, ChallengeSeriesRace) // 0x5FC180
GRACE_FLAG_GETTER(GetIsCollectorsEditionRace, kIndexFlag_CollectorsEdition, CollectorsEditionRace) // 0x5FC1F0

// The PC build has no record fallback here: without an index this is false.
GRACE_INDEXED_GETTER(bool, GetCanBeReversed, (mIndex->mFlags & kIndexFlag_CanBeReversed) != 0, false) // 0x5FAC80

GRACE_RECORD_GETTER(float, GetTimeLimit, mRaceRecord->TimeLimit())                           // 0x5FC260
GRACE_RECORD_GETTER(float, GetMaxHeatLevel, mRaceRecord->MaxHeatLevel())                     // 0x5FC2C0
GRACE_RECORD_GETTER(bool, GetNoPostRaceScreen, mRaceRecord->NoPostRaceScreen())              // 0x5FC320
GRACE_RECORD_GETTER(bool, GetUseWorldHeatInRace, mRaceRecord->UseWorldHeat())                // 0x5FC380
GRACE_RECORD_GETTER(float, GetForceHeatLevel, static_cast<float>(mRaceRecord->ForceHeatLevel())) // 0x5FC3E0
GRACE_RECORD_GETTER(float, GetMaxRaceHeatLevel, mRaceRecord->MaxHeatLevel())                 // 0x5FC440
GRACE_RECORD_GETTER(float, GetInitialPlayerSpeed, mRaceRecord->InitialPlayerSpeed())         // 0x5FC4A0
GRACE_RECORD_GETTER(bool, GetIsRollingStart, mRaceRecord->RollingStart())                    // 0x5FC500
GRACE_RECORD_GETTER(bool, GetIsEpicPursuitRace, mRaceRecord->IsEpicPursuitRace())            // 0x5FC560
GRACE_RECORD_GETTER(const char *, GetPlayerCarType, mRaceRecord->PlayerCarType())            // 0x5FC5C0
GRACE_RECORD_GETTER(float, GetPlayerCarPerformance, mRaceRecord->PlayerCarPerformance())     // 0x5FC620
GRACE_RECORD_GETTER(int, GetBustedLives, mRaceRecord->BustedLives())                         // 0x5FC680
GRACE_RECORD_GETTER(int, GetKnockoutsPerLap, mRaceRecord->KnockoutsPerLap())                 // 0x5FC6E0
GRACE_RECORD_GETTER(float, GetTimedKnockout, mRaceRecord->KnockoutTime())                    // 0x5FC740
GRACE_RECORD_GETTER(bool, GetCatchUp, mRaceRecord->CatchUp())                                // 0x5FC7A0
GRACE_RECORD_GETTER(bool, GetCatchUpOverride, mRaceRecord->CatchUpOverride())                // 0x5FC800
GRACE_RECORD_GETTER(const char *, GetCatchUpSkill, mRaceRecord->CatchUpSkill())              // 0x5FC860
GRACE_RECORD_GETTER(const char *, GetCatchUpSpread, mRaceRecord->CatchUpSpread())            // 0x5FC8C0
GRACE_RECORD_GETTER(float, GetCatchUpIntegral, mRaceRecord->CatchUpIntegral())               // 0x5FC920
GRACE_RECORD_GETTER(float, GetCatchUpDerivative, mRaceRecord->CatchUpDerivative())           // 0x5FC980
GRACE_RECORD_GETTER(bool, GetPhotofinish, mRaceRecord->DoPhotofinish())                      // 0x5FC9E0
GRACE_RECORD_GETTER(unsigned int, GetNumCheckpoints, mRaceRecord->Num_Checkpoint())          // 0x5FCA40
GRACE_RECORD_GETTER(bool, GetCheckpointsVisible, mRaceRecord->CheckpointsVisible())          // 0x5FCA80
GRACE_RECORD_GETTER(unsigned int, GetNumShortcuts, mRaceRecord->Num_Shortcuts())             // 0x5FCAE0
GRACE_RECORD_GETTER(unsigned int, GetNumBarrierExemptions, mRaceRecord->Num_BarrierExemptions()) // 0x5FCB20
GRACE_RECORD_GETTER(unsigned int, GetBarrierCount, mRaceRecord->Num_Barriers())              // 0x5FCB60
GRACE_RECORD_GETTER(const char *, GetTrafficPattern, mRaceRecord->TrafficPattern())          // 0x5FCBA0
GRACE_RECORD_GETTER(const char *, GetPhotoFinishCamera, mRaceRecord->FinishCamera())         // 0x5FCC00
GRACE_RECORD_GETTER(const char *, GetPhotoFinishTexture, mRaceRecord->PostRaceScreenTexture()) // 0x5FCC60
GRACE_RECORD_GETTER(float, GetTimeOfDay, mRaceRecord->TOD())                                 // 0x5FCCC0
GRACE_RECORD_GETTER(float, GetStartTime, mRaceRecord->StartTime())                           // 0x5FCD20
GRACE_RECORD_GETTER(float, GetStartPercent, mRaceRecord->StartPercent())                     // 0x5FCD80
GRACE_RECORD_GETTER(const char *, GetSpeedTrapCamera, mRaceRecord->SpeedTrapCamera())        // 0x5FCDE0
GRACE_RECORD_GETTER(int, GetTrafficDensity, mRaceRecord->TrafficLevel())                     // 0x5FABB0
GRACE_RECORD_GETTER(int, GetNumOpponents, static_cast<int>(mRaceRecord->Num_Opponents()))    // 0x5FACD0

// PC: 0x5FA9A0.
unsigned int GRaceParameters::GetChallengeType() const {
    if (mIndex != nullptr) {
        return mIndex->mChallengeType;
    }
    EnsureLoaded();
    const char *milestoneName = mRaceRecord->MilestoneName();
    if (milestoneName != nullptr && *milestoneName != '\0') {
        return Attrib::StringHash32(milestoneName);
    }
    return 0;
}

// PC: 0x5FAA20.
GRace::Type GRaceParameters::GetRaceType() const {
    static const struct {
        const char *mTypeName;
        GRace::Type mType;
    } typeTable[11] = {
        {"circuit", GRace::kRaceType_Circuit},
        {"p2p", GRace::kRaceType_P2P},
        {"drag", GRace::kRaceType_Drag},
        {"knockout", GRace::kRaceType_Knockout},
        {"tollbooth", GRace::kRaceType_Tollbooth},
        {"speedtrap", GRace::kRaceType_SpeedTrap},
        {"cashgrab", GRace::kRaceType_CashGrab},
        {"checkpointrace", GRace::kRaceType_Checkpoint},
        {"challenge", GRace::kRaceType_Challenge},
        {"speedtrapjump", GRace::kRaceType_JumpToSpeedTrap},
        {"milestonejump", GRace::kRaceType_JumpToMilestone},
    };

    if (mIndex != nullptr) {
        return static_cast<GRace::Type>(mIndex->mRaceType);
    }
    EnsureLoaded();
    const char *eventType = mRaceRecord->EventIconType();
    for (int onType = 0; onType < 11; onType++) {
        if (bStrCmp(eventType, typeTable[onType].mTypeName) == 0) {
            return typeTable[onType].mType;
        }
    }
    return GRace::kRaceType_None;
}

// PC: 0x5FAAB0.
GRace::Region GRaceParameters::GetRegion() const {
    static const struct {
        const char *mRegionName;
        GRace::Region mRegion;
    } regionTable[3] = {
        {"college", GRace::kRaceRegion_College},
        {"coastal", GRace::kRaceRegion_Coastal},
        {"city", GRace::kRaceRegion_City},
    };

    if (mIndex != nullptr) {
        return static_cast<GRace::Region>(static_cast<int8>(mIndex->mRegion));
    }
    EnsureLoaded();
    const char *regionName = mRaceRecord->Region();
    for (int onRegion = 0; onRegion < 3; onRegion++) {
        if (bStrCmp(regionName, regionTable[onRegion].mRegionName) == 0) {
            return regionTable[onRegion].mRegion;
        }
    }
    return GRace::kRaceRegion_None;
}

// PC: 0x5FAC10.
GRace::Difficulty GRaceParameters::GetDifficulty() const {
    EnsureLoaded();
    int difficulty = mRaceRecord->DifficultyLevel();
    if (difficulty < 34) {
        return GRace::kRaceDifficulty_Easy;
    }
    return difficulty >= 67 ? GRace::kRaceDifficulty_Hard : GRace::kRaceDifficulty_Medium;
}

// PC: 0x5FD240.
GRace::CopDensity GRaceParameters::GetCopDensity() const {
    if (mIndex != nullptr) {
        return static_cast<GRace::CopDensity>(mIndex->mCopDensity);
    }
    EnsureLoaded();
    int density = mRaceRecord->CopDensity();
    if (!GetCopsEnabled()) {
        return GRace::kRaceCops_Off;
    }
    if (density < 34) {
        return GRace::kRaceCops_Light;
    }
    return density >= 67 ? GRace::kRaceCops_Heavy : GRace::kRaceCops_Medium;
}

// PC: 0x5FD160.
bool GRaceParameters::GetIsAvailable(GRace::Context context) const {
    if (UnlockAllThings) {
        return true;
    }
    GRaceDatabase &db = GRaceDatabase::Get();
    switch (context) {
        case GRace::kRaceContext_QuickRace:
            if (GetNeverInQuickRace()) {
                return false;
            }
            return db.IsQuickRaceUnlocked(GetEventHash());
        case GRace::kRaceContext_Online:
            return db.IsOnlineRaceUnlocked(GetEventHash());
        case GRace::kRaceContext_Career:
            if (!db.IsCareerRaceUnlocked(GetEventHash())) {
                return false;
            }
            return !db.IsCareerRaceComplete(GetEventHash());
        default:
            return false;
    }
}

// PC: 0x5FD2D0. A boss race outside the career is raced against the generic
// "character_smart" opponent.
GCharacter *GRaceParameters::GetOpponentChar(unsigned int index) const {
    EnsureLoaded();
    if (GetIsBossRace() && GRaceStatus::Get().GetRaceContext() != GRace::kRaceContext_Career) {
        return FindRuntimeInstance<GCharacter>(Attrib::StringToLowerCaseKey("character_smart"));
    }
    return FindRuntimeInstance<GCharacter>(mRaceRecord->Opponents(index).mCollectionKey);
}

// PC: 0x5DCCA0. Converts a marker's position from the attribute's coordinate
// frame into the simulation's.
void GRaceParameters::ExtractPosition(Attrib::Gen::gameplay &collection, UMath::Vector3 &pos) const {
    const UMath::Vector3 &c = collection.Position();
    UMath::Vector3 result;
    result.x = -c.y;
    result.y = c.z;
    result.z = c.x;
    pos = result;
}

// PC: 0x5DCD00. rotate and the marker's Rotation are in degrees.
void GRaceParameters::ExtractDirection(Attrib::Gen::gameplay &collection, UMath::Vector3 &dir, float rotate) const {
    UMath::Matrix4 rotMat = UMath::Matrix4::kIdentity;
    UMath::Vector3 initialVec;
    initialVec.x = 0.0f;
    initialVec.y = 0.0f;
    initialVec.z = 1.0f;
    float angle = (rotate + collection.Rotation()) * (-1.0f / 360.0f);
    UMath::MultYRot(rotMat, angle, rotMat);
    UMath::Rotate(initialVec, rotMat, initialVec);
    dir = initialVec;
}

// PC: 0x5FAD10 .. 0x5FB240.
void GRaceParameters::GetStartPosition(UMath::Vector3 &pos) const {
    EnsureLoaded();
    UMath::Vector3 startPos = UMath::Vector3::kZero;
    Attrib::Key key = mRaceRecord->racestart().mCollectionKey;
    if (key != 0) {
        Attrib::Gen::gameplay startMarkerCollection(key, 0, nullptr);
        ExtractPosition(startMarkerCollection, startPos);
    }
    pos = startPos;
}

void GRaceParameters::GetStartDirection(UMath::Vector3 &dir) const {
    EnsureLoaded();
    UMath::Vector3 startDir = UMath::Vector3::kZero;
    Attrib::Key key = mRaceRecord->racestart().mCollectionKey;
    if (key != 0) {
        Attrib::Gen::gameplay startMarkerCollection(key, 0, nullptr);
        ExtractDirection(startMarkerCollection, startDir, 0.0f);
    }
    dir = startDir;
}

// PC: 0x5FAF10.
bool GRaceParameters::HasFinishLine() const {
    EnsureLoaded();
    return mRaceRecord->racefinish().mCollectionKey != 0;
}

void GRaceParameters::GetFinishPosition(UMath::Vector3 &pos) const {
    EnsureLoaded();
    UMath::Vector3 finishPos = UMath::Vector3::kZero;
    Attrib::Key key = mRaceRecord->racefinish().mCollectionKey;
    if (key != 0) {
        Attrib::Gen::gameplay finishMarkerCollection(key, 0, nullptr);
        ExtractPosition(finishMarkerCollection, finishPos);
    }
    pos = finishPos;
}

void GRaceParameters::GetFinishDirection(UMath::Vector3 &dir) const {
    EnsureLoaded();
    UMath::Vector3 finishDir = UMath::Vector3::kZero;
    Attrib::Key key = mRaceRecord->racefinish().mCollectionKey;
    if (key != 0) {
        Attrib::Gen::gameplay finishMarkerCollection(key, 0, nullptr);
        ExtractDirection(finishMarkerCollection, finishDir, 0.0f);
    }
    dir = finishDir;
}

void GRaceParameters::GetCheckpointPosition(unsigned int index, UMath::Vector3 &pos) const {
    EnsureLoaded();
    UMath::Vector3 checkPos = UMath::Vector3::kZero;
    Attrib::Key key = mRaceRecord->Checkpoint(index).mCollectionKey;
    if (key != 0) {
        Attrib::Gen::gameplay checkTriggerCollection(key, 0, nullptr);
        ExtractPosition(checkTriggerCollection, checkPos);
    }
    pos = checkPos;
}

void GRaceParameters::GetCheckpointDirection(unsigned int index, UMath::Vector3 &dir) const {
    EnsureLoaded();
    UMath::Vector3 checkDir = UMath::Vector3::kZero;
    Attrib::Key key = mRaceRecord->Checkpoint(index).mCollectionKey;
    if (key != 0) {
        Attrib::Gen::gameplay checkTriggerCollection(key, 0, nullptr);
        ExtractDirection(checkTriggerCollection, checkDir, 0.0f);
    }
    dir = checkDir;
}

// PC: 0x5FB340 / 0x5FB3B0.
GMarker *GRaceParameters::GetShortcut(unsigned int index) const {
    EnsureLoaded();
    return FindRuntimeInstance<GMarker>(mRaceRecord->Shortcuts(index).mCollectionKey);
}

GMarker *GRaceParameters::GetBarrierExemption(unsigned int index) const {
    EnsureLoaded();
    return FindRuntimeInstance<GMarker>(mRaceRecord->BarrierExemptions(index).mCollectionKey);
}

// PC: 0x5FB420. A leading '*' marks a barrier whose artwork is flipped.
const char *GRaceParameters::GetBarrierName(unsigned int index) const {
    EnsureLoaded();
    const char *barrierName = mRaceRecord->Barriers(index);
    if (*barrierName == '*') {
        barrierName++;
    }
    return barrierName;
}

unsigned int GRaceParameters::GetBarrierHash(unsigned int index) const {
    EnsureLoaded();
    return bStringHash(GetBarrierName(index));
}

bool GRaceParameters::GetBarrierIsFlipped(unsigned int index) const {
    EnsureLoaded();
    const char *barrierName = mRaceRecord->Barriers(index);
    return *barrierName == '*';
}

// PC: 0x5FCEA0. The race's extent on the world map, normalised to [0,1].
void GRaceParameters::GetBoundingBox(UMath::Vector2 &topLeft, UMath::Vector2 &botRight) const {
    if (mIndex != nullptr) {
        const float scale = 1.0f / 255.0f;
        topLeft.x = static_cast<float>(mIndex->mMapX1) * scale;
        topLeft.y = static_cast<float>(mIndex->mMapY1) * scale;
        botRight.x = static_cast<float>(mIndex->mMapX2) * scale;
        botRight.y = static_cast<float>(mIndex->mMapY2) * scale;
        return;
    }

    EnsureLoaded();
    UMath::Vector3 pos;
    GetStartPosition(pos);
    float x1 = pos.z;
    float x2 = pos.z;
    float y1 = -pos.x;
    float y2 = -pos.x;

    if (HasFinishLine()) {
        GetFinishPosition(pos);
        x1 = bMin(x1, pos.z);
        x2 = bMax(x2, pos.z);
        y1 = bMin(y1, -pos.x);
        y2 = bMax(y2, -pos.x);
    }

    for (unsigned int onCheck = 0;; onCheck++) {
        EnsureLoaded();
        if (onCheck >= mRaceRecord->Num_Checkpoint()) {
            break;
        }
        GetCheckpointPosition(onCheck, pos);
        x1 = bMin(x1, pos.z);
        x2 = bMax(x2, pos.z);
        y1 = bMin(y1, -pos.x);
        y2 = bMax(y2, -pos.x);
    }

    TrackInfo *trackInfo = TrackInfo::GetTrackInfo(2000);
    bVector2 topLeftWorld(x1, y1);
    bVector2 botRightWorld(x2, y2);
    bVector2 topLeftMap;
    bVector2 botRightMap;
    Minimap::ConvertPos(topLeftWorld, topLeftMap, trackInfo);
    Minimap::ConvertPos(botRightWorld, botRightMap, trackInfo);
    topLeft.x = topLeftMap.x;
    topLeft.y = topLeftMap.y;
    botRight.x = botRightMap.x;
    botRight.y = botRightMap.y;
}

// ---------------------------------------------------------------------------
// GRaceCustom
// ---------------------------------------------------------------------------

// PC: 0x602680. A copy of a race whose settings can be changed: it gets its own
// dynamic collection parented to the original race.
GRaceCustom::GRaceCustom(const GRaceParameters &other)
    : GRaceParameters(other.GetCollectionKey(), nullptr), //
      mRaceActivity(nullptr),                             //
      mNumOpponents(0),                                   //
      mReversed(false),                                   //
      mFreedByOwner(false),                               //
      mHeatLevel(-1) {
    Attrib::Key customKey = mRaceRecord->GenerateUniqueKey("GRaceCustom", true);
    mRaceRecord->Modify(customKey, 0);
    mRaceRecord->SetParent(other.GetGameplayObj()->GetCollection());
    mNumOpponents = mRaceRecord->Num_Opponents();
}

// PC: 0x5E3B00.
GRaceCustom::~GRaceCustom() {
    if (mRaceActivity != nullptr) {
        delete mRaceActivity;
        mRaceActivity = nullptr;
    }
}

GActivity *GRaceCustom::GetRaceActivity() const {
    return mRaceActivity;
}

void GRaceCustom::SetAttribute(Attrib::Key key, const int &value, Attrib::Key index) {
    SetCollectionAttribute(*mRaceRecord, key, value, index);
}

void GRaceCustom::SetAttribute(Attrib::Key key, const float &value, unsigned int index) {
    SetCollectionAttribute(*mRaceRecord, key, value, index);
}

void GRaceCustom::SetAttribute(unsigned int key, const bool &value, unsigned int index) {
    SetCollectionAttribute(*mRaceRecord, key, value, index);
}

// PC: 0x5E9A70.
void GRaceCustom::SetNumLaps(int numLaps) {
    SetAttribute(0x0ebdc165, numLaps, 0); // NumLaps
}

// PC: 0x5E9BA0.
void GRaceCustom::SetBustedLives(int bustedLives) {
    SetAttribute(0x462f2e36, bustedLives, 0); // BustedLives
}

// PC: 0x5E9A10.
void GRaceCustom::SetPositionalKnockout(bool enabled, int knockoutsPerLap) {
    if (!enabled) {
        knockoutsPerLap = 0;
    }
    SetAttribute(0x181462da, knockoutsPerLap, 0); // KnockoutsPerLap
}

// PC: 0x5E9B80.
void GRaceCustom::SetCopsEnabled(bool copsEnabled) {
    SetAttribute(0x3918e889, copsEnabled, 0); // CopsInRace
}

// PC: 0x5E9BC0.
void GRaceCustom::SetForceHeatLevel(int level) {
    SetAttribute(0xe4211f4f, level, 0); // ForceHeatLevel
}

// PC: 0x5E9A90.
void GRaceCustom::SetTrafficDensity(int density) {
    if (density < 0) {
        density = 0;
    } else if (density > 100) {
        density = 100;
    }
    SetAttribute(0xc64bc341, density, 0); // TrafficLevel
}

// PC: 0x5E9AE0.
void GRaceCustom::SetDifficulty(GRace::Difficulty difficulty) {
    int difficultyPct;
    switch (difficulty) {
        case GRace::kRaceDifficulty_Easy:
            difficultyPct = 33;
            break;
        case GRace::kRaceDifficulty_Medium:
            difficultyPct = 66;
            break;
        case GRace::kRaceDifficulty_Hard:
            difficultyPct = 100;
            break;
        default:
            difficultyPct = 0;
            break;
    }
    SetAttribute(0x88a7e3be, difficultyPct, 0); // DifficultyLevel
}

void GRaceCustom::SetReversed(bool isReverseDir) {
    mReversed = isReverseDir;
}

// PC: 0x5E9B60.
void GRaceCustom::SetCatchUp(bool catchUpEnabled) {
    SetAttribute(0x10db04e6, catchUpEnabled, 0); // CatchUp
}

void GRaceCustom::SetNumOpponents(int numOpponents) {
    mNumOpponents = numOpponents;
}

// PC: 0x5FD680. A reversed race runs its checkpoints backwards.
void GRaceCustom::GetCheckpointPosition(unsigned int index, UMath::Vector3 &pos) const {
    if (mReversed) {
        EnsureLoaded();
        GRaceParameters::GetCheckpointPosition(mRaceRecord->Num_Checkpoint() - index - 1, pos);
    } else {
        GRaceParameters::GetCheckpointPosition(index, pos);
    }
}

// PC: 0x5FD6F0.
void GRaceCustom::GetCheckpointDirection(unsigned int index, UMath::Vector3 &dir) const {
    if (mReversed) {
        EnsureLoaded();
        index = mRaceRecord->Num_Checkpoint() - index - 1;
    }
    UMath::Vector3 checkDir = UMath::Vector3::kZero;
    Attrib::Key key = mRaceRecord->Checkpoint(index).mCollectionKey;
    if (key != 0) {
        Attrib::Gen::gameplay checkTriggerCollection(key, 0, nullptr);
        ExtractDirection(checkTriggerCollection, checkDir, mReversed ? 180.0f : 0.0f);
    }
    dir = checkDir;
}

// PC: 0x5FD370. Applies the custom settings to the race record (reversed
// start/finish, opponent list, heat) and creates the race's activity.
void GRaceCustom::CreateRaceActivity() {
    if (mReversed) {
        if (GetCanBeReversed()) {
            GCollectionKey startReverse = mRaceRecord->racestartReverse();
            GCollectionKey finishReverse = mRaceRecord->racefinishReverse();
            SetCollectionAttribute(*mRaceRecord, 0xe43b2ccc, startReverse, 0);  // racestart
            SetCollectionAttribute(*mRaceRecord, 0xb0a24adc, finishReverse, 0); // racefinish
        } else {
            mReversed = false;
        }
    }

    EnsureLoaded();
    unsigned int existingOpponents = mRaceRecord->Num_Opponents();
    bool bossInQuickRace = false;
    if (GRaceStatus::Get().GetRaceContext() != GRace::kRaceContext_Career && GetIsBossRace()) {
        bossInQuickRace = true;
        existingOpponents = 0;
    }

    if (mNumOpponents != existingOpponents) {
        Attrib::Key opponents[16];
        unsigned int numOpponents = 0;
        if (!bossInQuickRace) {
            numOpponents = mRaceRecord->Num_Opponents();
            for (unsigned int i = 0; i < numOpponents; i++) {
                opponents[i] = mRaceRecord->Opponents(i).mCollectionKey;
            }
        }
        if (mNumOpponents > numOpponents) {
            if (numOpponents == 0) {
                for (unsigned int i = 0; i < mNumOpponents; i++) {
                    opponents[i] = 0x9f3b88c5;
                }
            } else {
                for (unsigned int i = numOpponents; i < mNumOpponents; i++) {
                    opponents[i] = opponents[i % numOpponents];
                }
            }
        }

        mRaceRecord->Add(0x5839fa1a, mNumOpponents); // Opponents
        for (unsigned int i = 0; i < mNumOpponents; i++) {
            GCollectionKey key;
            key.mCollectionKey = opponents[i];
            SetCollectionAttribute(*mRaceRecord, 0x5839fa1a, key, i);
        }
    }

    if (mHeatLevel != -1) {
        if (mHeatLevel == 0) {
            SetCollectionAttribute(*mRaceRecord, 0x3918e889, false, 0); // CopsInRace
        } else {
            SetCollectionAttribute(*mRaceRecord, 0xe4211f4f, mHeatLevel, 0); // ForceHeatLevel
        }
    }

    // The race's activity runs on the customized record (a dynamic collection
    // whose parent is the race); GManager connects it with the rest.
    Attrib::Key activityKey = mRaceRecord->GetCollection();
    mRaceActivity = new GActivity(activityKey);
    mRaceActivity->AllocateConnectionBuffer(GObjectBlock::CalcNumConnections(activityKey));
}

// ---------------------------------------------------------------------------
// GRaceBin
// ---------------------------------------------------------------------------

// PC: 0x5E9BE0.
GRaceBin::GRaceBin(unsigned int collectionKey) : mBinRecord(collectionKey, 0, nullptr), mChildVault(nullptr) {
    mStats.mChallengesCompleted = 0;
    mStats.mRacesWon = 0;
    const char *childVaultName = mBinRecord.gameplayvault();
    if (childVaultName != nullptr) {
        mChildVault = GManager::Get().FindVault(childVaultName);
        mChildVault->SetRaceBin();
    }
}

unsigned int GRaceBin::GetCollectionKey() const {
    return mBinRecord.GetCollection();
}

const Attrib::Gen::gameplay *GRaceBin::GetGameplayObj() const {
    return &mBinRecord;
}

GVault *GRaceBin::GetChildVault() const {
    return mChildVault;
}

int GRaceBin::GetBinNumber() const {
    return mBinRecord.BinIndex();
}

int GRaceBin::GetBossReputation() const {
    return mBinRecord.BossReputation();
}

float GRaceBin::GetBaseOpenWorldHeat() const {
    return mBinRecord.BaseOpenWorldHeat();
}

float GRaceBin::GetMaxOpenWorldHeat() const {
    return mBinRecord.MaxOpenWorldHeat();
}

float GRaceBin::GetScaleOpenWorldHeat() const {
    return mBinRecord.ScaleOpenWorldHeat();
}

unsigned int GRaceBin::GetBossKey() const {
    return mBinRecord.Boss().mCollectionKey;
}

unsigned int GRaceBin::GetBossRaceCount() const {
    return mBinRecord.Num_BossRaces();
}

// PC: 0x601020. Race lists hold collection keys; the frontend wants event hashes.
static unsigned int RaceKeyToEventHash(Attrib::Key raceKey) {
    GRaceParameters *parms = GRaceDatabase::Get().GetRaceFromKey(raceKey);
    return parms != nullptr ? parms->GetEventHash() : 0;
}

unsigned int GRaceBin::GetBossRaceHash(unsigned int index) const {
    return RaceKeyToEventHash(mBinRecord.BossRaces(index).mCollectionKey);
}

unsigned int GRaceBin::GetWorldRaceCount() const {
    return mBinRecord.Num_WorldRaces();
}

// PC: 0x601060.
unsigned int GRaceBin::GetWorldRaceHash(unsigned int index) const {
    return RaceKeyToEventHash(mBinRecord.WorldRaces(index).mCollectionKey);
}

unsigned int GRaceBin::GetJumpRaceCount() const {
    return mBinRecord.Num_JumpRaces();
}

unsigned int GRaceBin::GetJumpRaceHash(unsigned int index) const {
    return RaceKeyToEventHash(mBinRecord.JumpRaces(index).mCollectionKey);
}

unsigned int GRaceBin::GetBaselineUnlockCount() const {
    return mBinRecord.Num_BaselineUnlocks();
}

unsigned int GRaceBin::GetBaselineUnlock(unsigned int index) const {
    return mBinRecord.BaselineUnlocks(index).mCollectionKey;
}

unsigned int GRaceBin::GetBarrierCount() const {
    return mBinRecord.Num_Barriers();
}

// PC: 0x5DCFE0 .. 0x5DD050.
const char *GRaceBin::GetBarrierName(unsigned int index) const {
    const char *barrierName = mBinRecord.Barriers(index);
    if (barrierName != nullptr && *barrierName == '*') {
        barrierName++;
    }
    return barrierName;
}

unsigned int GRaceBin::GetBarrierHash(unsigned int index) const {
    return bStringHash(GetBarrierName(index));
}

bool GRaceBin::GetBarrierIsFlipped(unsigned int index) const {
    const char *barrierName = mBinRecord.Barriers(index);
    return barrierName != nullptr && *barrierName == '*';
}

// PC: 0x5DD090.
void GRaceBin::EnableBarriers() {
    for (unsigned int onBarrier = 0; onBarrier < GetBarrierCount(); onBarrier++) {
        EnableBarrierSceneryGroup(GetBarrierName(onBarrier), GetBarrierIsFlipped(onBarrier));
    }
    WCollisionAssets::Get().SetExclusionFlags();
    WRoadNetwork::Get().ResolveBarriers();
}

// PC: 0x5DD140.
void GRaceBin::DisableBarriers() {
    RedoTopologyAndSceneryGroups();
    WRoadNetwork::Get().ResetBarriers();
    WRoadNetwork::Get().ResetRaceSegments();
}

int GRaceBin::GetRequiredBounty() const {
    return mBinRecord.RequiredBounty();
}

int GRaceBin::GetRequiredChallenges() const {
    return mBinRecord.RequiredChallenges();
}

int GRaceBin::GetRequiredRaceWins() const {
    return mBinRecord.RequiredRacesWon();
}

int GRaceBin::GetCompletedChallenges() const {
    return mStats.mChallengesCompleted;
}

int GRaceBin::GetAwardedRaceWins() const {
    return mStats.mRacesWon;
}

// PC: 0x6010E0.
void GRaceBin::RefreshProgress() {
    int numRaces = 0;
    for (unsigned int onRace = 0; onRace < GetWorldRaceCount(); onRace++) {
        if (GRaceDatabase::Get().IsCareerRaceComplete(GetWorldRaceHash(onRace))) {
            numRaces++;
        }
    }

    int numChallenges = 0;
    unsigned int binNumber = static_cast<unsigned int>(GetBinNumber());
    for (GMilestone *binMilestone = GManager::Get().GetFirstMilestone(false, binNumber); binMilestone != nullptr;
         binMilestone = GManager::Get().GetNextMilestone(binMilestone, false, binNumber)) {
        if (binMilestone->GetIsAwarded()) {
            numChallenges++;
        }
    }
    for (GSpeedTrap *binSpeedTrap = GManager::Get().GetFirstSpeedTrap(false, binNumber); binSpeedTrap != nullptr;
         binSpeedTrap = GManager::Get().GetNextSpeedTrap(binSpeedTrap, false, binNumber)) {
        if (binSpeedTrap->GetIsCompleted()) {
            numChallenges++;
        }
    }

    mStats.mChallengesCompleted = static_cast<uint16>(numChallenges);
    mStats.mRacesWon = static_cast<uint16>(numRaces);
}

unsigned int GRaceBin::Serialize(unsigned char *dest) {
    bMemCpy(dest, &mStats, sizeof(mStats));
    return sizeof(mStats);
}

unsigned int GRaceBin::Deserialize(unsigned char *src) {
    bMemCpy(&mStats, src, sizeof(mStats));
    return sizeof(mStats);
}

void GRaceBin::SetCompletedChallenges(int numChallenges) {
    mStats.mChallengesCompleted = static_cast<uint16>(numChallenges);
}

void GRaceBin::SetRacesWon(int numRaces) {
    mStats.mRacesWon = static_cast<uint16>(numRaces);
}

// ---------------------------------------------------------------------------
// GRaceDatabase
// ---------------------------------------------------------------------------

// PC: 0x603A40.
void GRaceDatabase::Init() {
    mObj = new GRaceDatabase();
}

// PC: 0x6039C0.
GRaceDatabase::GRaceDatabase() {
    mRaceCountStatic = 0;
    mRaceCountDynamic = 0;
    mRaceIndex = nullptr;
    mRaceParameters = nullptr;
    for (unsigned int i = 0; i < 4; i++) {
        mRaceCustom[i] = nullptr;
    }
    mBinCount = 0;
    mBins = nullptr;
    mGameplayClass = Attrib::Database::Get().GetClass(Attrib::Gen::gameplay::ClassKey());
    mStartupRace = nullptr;
    mStartupRaceContext = GRace::kRaceContext_QuickRace;
    mNumInitialUnlocks = 0;
    mInitialUnlockHash = nullptr;
    mRaceScoreInfo = nullptr;

    mBinCount = BuildBinList(nullptr);
    mBins = static_cast<GRaceBin *>(gFastMem.Alloc(mBinCount * sizeof(GRaceBin), nullptr));
    BuildBinList(mBins);
    BuildRaceList();
    mRaceScoreInfo = static_cast<GRaceSaveInfo *>(bMalloc(mRaceCountStatic * sizeof(GRaceSaveInfo), 0x800));
    ClearRaceScores();
}

// PC: 0x5E3800. Bins are the collections derived directly from the bin template.
bool GRaceDatabase::CollectionIsRaceBin(Attrib::Gen::gameplay &instanceObj) {
    Attrib::Key parentKey = instanceObj.GetParent();
    Attrib::Gen::gameplay templateObj(0x022eb0ee, 0, nullptr);
    Attrib::Gen::gameplay parentObj(parentKey, 0, nullptr);
    return parentObj.GetCollection() == templateObj.GetCollection();
}

// PC: 0x5E36D0. Races are the non-template collections derived (at any depth)
// from the race activity template.
bool GRaceDatabase::CollectionIsRaceActivity(Attrib::Gen::gameplay &instanceObj) {
    Attrib::Gen::gameplay templateObj(0xd1e66b67, 0, nullptr);
    if (instanceObj.Template()) {
        return false;
    }
    for (Attrib::Key parentKey = instanceObj.GetParent(); parentKey != 0;) {
        Attrib::Gen::gameplay parentObj(parentKey, 0, nullptr);
        if (parentObj.GetCollection() == templateObj.GetCollection()) {
            return true;
        }
        parentKey = parentObj.GetParent();
    }
    return false;
}

// PC: 0x5EB640. Counts the bins, constructing them into list when given.
unsigned int GRaceDatabase::BuildBinList(GRaceBin *list) {
    unsigned int count = 0;
    for (Attrib::Key collectionKey = mGameplayClass->GetFirstCollection(); collectionKey != 0;
         collectionKey = mGameplayClass->GetNextCollection(collectionKey)) {
        Attrib::Gen::gameplay element(collectionKey, 0, nullptr);
        if (CollectionIsRaceBin(element)) {
            if (list != nullptr) {
                new (&list[count]) GRaceBin(collectionKey);
            }
            count++;
        }
    }
    return count;
}

// PC: 0x603340. Counts the races, constructing them into list when given.
unsigned int GRaceDatabase::StoreRaceList(GRaceParameters *list) {
    unsigned int count = 0;
    for (Attrib::Key collectionKey = mGameplayClass->GetFirstCollection(); collectionKey != 0;
         collectionKey = mGameplayClass->GetNextCollection(collectionKey)) {
        Attrib::Gen::gameplay element(collectionKey, 0, nullptr);
        if (CollectionIsRaceActivity(element)) {
            if (list != nullptr) {
                new (&list[count]) GRaceParameters(collectionKey, &mRaceIndex[count]);
            }
            count++;
        }
    }
    return count;
}

// PC: 0x603970.
void GRaceDatabase::BuildRaceList() {
    mRaceCountStatic = StoreRaceList(nullptr);
    mRaceCountDynamic = 0;
    mRaceIndex = static_cast<GRaceIndexData *>(bMalloc(mRaceCountStatic * sizeof(GRaceIndexData), 0));
    mRaceParameters = static_cast<GRaceParameters *>(bMalloc(mRaceCountStatic * sizeof(GRaceParameters), 0));
    for (unsigned int i = 0; i < 4; i++) {
        mRaceCustom[i] = nullptr;
    }
    StoreRaceList(mRaceParameters);
}

unsigned int GRaceDatabase::GetRaceCount() {
    return mRaceCountStatic + mRaceCountDynamic;
}

// PC: 0x5DC930. The static races, then the custom ones.
GRaceParameters *GRaceDatabase::GetRaceParameters(unsigned int index) {
    if (index < mRaceCountStatic) {
        return &mRaceParameters[index];
    }
    return mRaceCustom[index - mRaceCountStatic];
}

// PC: 0x5FB710.
GRaceParameters *GRaceDatabase::GetRaceFromHash(unsigned int hash) {
    for (unsigned int onRace = 0; onRace < GetRaceCount(); onRace++) {
        GRaceParameters *parms = GetRaceParameters(onRace);
        if (parms->GetEventHash() == hash) {
            return parms;
        }
    }
    return nullptr;
}

// PC: 0x600A20.
GRaceParameters *GRaceDatabase::GetRaceFromKey(Attrib::Key key) {
    for (unsigned int onRace = 0; onRace < GetRaceCount(); onRace++) {
        GRaceParameters *parms = GetRaceParameters(onRace);
        if (parms->GetCollectionKey() == key) {
            return parms;
        }
    }
    return nullptr;
}

// PC: 0x6022D0.
GRaceParameters *GRaceDatabase::GetRaceFromActivity(GActivity *activity) {
    return GetRaceFromKey(activity->GetCollection());
}

unsigned int GRaceDatabase::GetBinCount() {
    return mBinCount;
}

GRaceBin *GRaceDatabase::GetBin(unsigned int index) {
    return &mBins[index];
}

// PC: 0x5E3980.
GRaceBin *GRaceDatabase::GetBinNumber(int number) {
    for (unsigned int onBin = 0; onBin < mBinCount; onBin++) {
        if (mBins[onBin].GetBinNumber() == number) {
            return &mBins[onBin];
        }
    }
    return nullptr;
}

#if defined(EA_PORT_NATIVE)
// PC: 0x602320. The winning player's records for the race just run: best
// top / average speed, the race type's best score (time, lap, points), and
// with raceCompleted the race's completed flag for the context (career or
// quick race); the bins' progress follows.
void GRaceDatabase::UpdateRaceScore(bool raceCompleted) {
    GRaceStatus &raceStatus = GRaceStatus::Get();
    GRaceParameters *parms = raceStatus.GetRaceParameters();
    GRacerInfo *winner = parms != nullptr ? raceStatus.GetWinningPlayerInfo() : nullptr;
    if (winner == nullptr) {
        return;
    }
    GRaceSaveInfo *info = GetScoreInfo(parms->GetEventHash());
    const bool hasScore = (info->mFlags & kCompleted_ContextAny) != 0;
    float topSpeed = static_cast<float>(info->mTopSpeed);
    info->mTopSpeed = winner->GetTopSpeed() > topSpeed ? winner->GetTopSpeed() : topSpeed;
    float averageSpeed = static_cast<float>(info->mAverageSpeed);
    float raceAverage = winner->CalcAverageSpeed();
    info->mAverageSpeed = raceAverage > averageSpeed ? raceAverage : averageSpeed;
    switch (parms->GetRaceType()) {
        case GRace::kRaceType_P2P:
        case GRace::kRaceType_Drag:
        case GRace::kRaceType_Tollbooth:
        case GRace::kRaceType_JumpToSpeedTrap:
        case GRace::kRaceType_JumpToMilestone:
            if (!hasScore || winner->GetRaceTime() < info->mHighScores.mBestTime) {
                info->mHighScores.mBestTime = winner->GetRaceTime();
            }
            break;
        case GRace::kRaceType_Circuit:
        case GRace::kRaceType_Knockout: {
            float bestLap = raceStatus.GetBestLapTime(winner->GetIndex());
            if (!hasScore || bestLap < info->mHighScores.mBestTime) {
                info->mHighScores.mBestTime = bestLap;
            }
            break;
        }
        case GRace::kRaceType_SpeedTrap:
        case GRace::kRaceType_CashGrab:
            if (!hasScore || winner->GetPointTotal() > info->mHighScores.mBestSpeed) {
                info->mHighScores.mBestSpeed = winner->GetPointTotal();
            }
            break;
        case GRace::kRaceType_Checkpoint: {
            uint32 points = static_cast<uint32>(winner->GetPointTotal());
            if (!hasScore || points > info->mHighScores.mBestPoints) {
                info->mHighScores.mBestPoints = points;
            }
            break;
        }
        default:
            break;
    }
    if (raceCompleted) {
        info->mFlags |= raceStatus.GetRaceContext() == GRace::kRaceContext_Career ? kCompleted_ContextCareer : kCompleted_ContextQuickRace;
        RefreshBinProgress();
    }
}
#endif

// PC: 0x6022A0.
void GRaceDatabase::RefreshBinProgress() {
    for (unsigned int onBin = 0; onBin < mBinCount; onBin++) {
        mBins[onBin].RefreshProgress();
    }
}

// PC: 0x5E35D0. Per bin: u16 bin number, u16 size of what follows, the stats.
unsigned int GRaceDatabase::SerializeBins(uint8 *dest) {
    uint8 *destStart = dest;
    *reinterpret_cast<uint32 *>(dest) = mBinCount;
    dest += sizeof(uint32);
    for (unsigned int onBin = 0; onBin < mBinCount; onBin++) {
        *reinterpret_cast<uint16 *>(dest) = static_cast<uint16>(mBins[onBin].GetBinNumber());
        dest += sizeof(uint16);
        uint16 *serializedSize = reinterpret_cast<uint16 *>(dest);
        dest += sizeof(uint16);
        bMemCpy(dest, &mBins[onBin].mStats, sizeof(GRaceBin::BinStats));
        dest += sizeof(GRaceBin::BinStats);
        *serializedSize = sizeof(GRaceBin::BinStats);
    }
    return static_cast<unsigned int>(dest - destStart);
}

// PC: 0x5E98D0.
unsigned int GRaceDatabase::DeserializeBins(uint8 *src) {
    uint8 *srcStart = src;
    uint32 savedBins = *reinterpret_cast<uint32 *>(src);
    src += sizeof(uint32);
    for (unsigned int onBin = 0; onBin < savedBins; onBin++) {
        uint16 binNumber = *reinterpret_cast<uint16 *>(src);
        uint16 serializedSize = *reinterpret_cast<uint16 *>(src + sizeof(uint16));
        src += 2 * sizeof(uint16);
        GRaceBin *targetBin = GetBinNumber(binNumber);
        if (targetBin != nullptr) {
            bMemCpy(&targetBin->mStats, src, sizeof(GRaceBin::BinStats));
        }
        src += serializedSize;
    }
    return static_cast<unsigned int>(src - srcStart);
}

// PC: 0x603440.
GRaceCustom *GRaceDatabase::AllocCustomRace(GRaceParameters *parms) {
    if (parms == nullptr) {
        return nullptr;
    }
    parms->EnsureLoaded();
    unsigned int slot = mRaceCountDynamic++;
    void *mem = gFastMem.Alloc(sizeof(GRaceCustom), nullptr);
    GRaceCustom *custom = mem != nullptr ? new (mem) GRaceCustom(*parms) : nullptr;
    mRaceCustom[slot] = custom;
    return custom;
}

// PC: 0x5DC970.
void GRaceDatabase::DestroyCustomRace(GRaceCustom *custom) {
    if (custom != nullptr) {
        custom->~GRaceCustom();
        gFastMem.Free(custom, sizeof(GRaceCustom), nullptr);
    }
    for (unsigned int onCustom = 0; onCustom < 4; onCustom++) {
        if (mRaceCustom[onCustom] == custom) {
            if (onCustom < mRaceCountDynamic - 1) {
                mRaceCustom[onCustom] = mRaceCustom[mRaceCountDynamic - 1];
            }
            mRaceCountDynamic--;
            mRaceCustom[mRaceCountDynamic] = nullptr;
            return;
        }
    }
}

// PC: 0x5E39E0. The startup race is freed when it's cleared instead.
void GRaceDatabase::FreeCustomRace(GRaceCustom *custom) {
    if (custom == nullptr) {
        return;
    }
    if (custom == mStartupRace) {
        custom->SetFreedByOwner();
        return;
    }
    DestroyCustomRace(custom);
}

GRaceCustom *GRaceDatabase::GetStartupRace() {
    return mStartupRace;
}

// PC: 0x5DC9D0.
void GRaceDatabase::ClearStartupRace() {
    if (mStartupRace != nullptr && mStartupRace->GetFreedByOwner()) {
        DestroyCustomRace(mStartupRace);
    }
    mStartupRace = nullptr;
    mStartupRaceContext = GRace::kRaceContext_QuickRace;
}

// PC: 0x600AB0.
void GRaceDatabase::SetStartupRace(GRaceCustom *custom, GRace::Context context) {
    if (mStartupRace != nullptr) {
        if (mStartupRace->GetFreedByOwner()) {
            DestroyCustomRace(mStartupRace);
        }
        mStartupRace = nullptr;
        mStartupRaceContext = GRace::kRaceContext_QuickRace;
    }
    mStartupRace = custom;
    mStartupRaceContext = context;
    if (custom != nullptr && context == GRace::kRaceContext_Career && !(custom->GetTimeOfDay() < 0.0f)) {
        SetCurrentTimeOfDay(custom->GetTimeOfDay());
    }
}

// PC: 0x5E9930.
void GRaceDatabase::NotifyVaultLoaded(GVault *vault) {
    for (unsigned int onRace = 0; onRace < GetRaceCount(); onRace++) {
        GRaceParameters *parms = GetRaceParameters(onRace);
        if (parms->mParentVault == vault) {
            parms->NotifyParentVaultLoaded();
        }
    }
}

// PC: 0x5E38D0.
void GRaceDatabase::NotifyVaultUnloading(GVault *vault) {
    for (unsigned int onRace = 0; onRace < GetRaceCount(); onRace++) {
        GRaceParameters *parms = GetRaceParameters(onRace);
        if (parms->mParentVault == vault) {
            parms->NotifyParentVaultUnloading();
        }
    }
    if (mStartupRace != nullptr && mStartupRace->mParentVault == vault) {
        if (mStartupRace->GetFreedByOwner()) {
            DestroyCustomRace(mStartupRace);
        }
        mStartupRace = nullptr;
        mStartupRaceContext = GRace::kRaceContext_QuickRace;
    }
}

// PC: 0x5DCA40. Finds the race's entry, claiming the first free one for it.
GRaceSaveInfo *GRaceDatabase::GetScoreInfo(unsigned int eventHash) {
    GRaceSaveInfo *info = mRaceScoreInfo;
    for (unsigned int i = 0; i < mRaceCountStatic && info->mRaceHash != 0; i++, info++) {
        if (info->mRaceHash == eventHash) {
            return info;
        }
    }
    info->mRaceHash = eventHash;
    return info;
}

// PC: 0x5DCA70.
bool GRaceDatabase::CheckRaceScoreFlags(unsigned int eventHash, ScoreFlags mask) {
    return (GetScoreInfo(eventHash)->mFlags & mask) != 0;
}

// PC: 0x5DCBB0.
void GRaceDatabase::ResetCareerCompleteFlag(unsigned int eventHash) {
    GetScoreInfo(eventHash)->mFlags &= ~kCompleted_ContextCareer;
}

// PC: 0x600B20. Resets every score and grants the initial unlocks.
void GRaceDatabase::ClearRaceScores() {
    if (mRaceScoreInfo == nullptr) {
        return;
    }
    bMemSet(mRaceScoreInfo, 0, mRaceCountStatic * sizeof(GRaceSaveInfo));
    for (unsigned int onRace = 0; onRace < mRaceCountStatic; onRace++) {
        GRaceParameters *parms = &mRaceParameters[onRace];
        if (parms->GetIsDDayRace()) {
            continue;
        }
        bool unlockedQuickRace = parms->GetInitiallyUnlockedQuickRace();
        bool unlockedOnline = parms->GetInitiallyUnlockedOnline();
        bool unlockedChallenge = parms->GetInitiallyUnlockedChallenge();
        if (parms->GetIsBossRace()) {
            unlockedChallenge = false;
        }
        if (!unlockedQuickRace && !unlockedOnline && !unlockedChallenge) {
            continue;
        }
        GRaceSaveInfo *info = GetScoreInfo(parms->GetEventHash());
        if (unlockedQuickRace || unlockedChallenge) {
            info->mFlags |= kUnlocked_QuickRace;
        }
        if (unlockedOnline) {
            info->mFlags |= kUnlocked_Online;
        }
    }
}

// PC: 0x5FB770. Keeps the saved entries of races that still exist.
void GRaceDatabase::LoadBestScores(GRaceSaveInfo *entries, unsigned int count) {
    bMemSet(mRaceScoreInfo, 0, mRaceCountStatic * sizeof(GRaceSaveInfo));
    GRaceSaveInfo *dest = mRaceScoreInfo;
    unsigned int stored = 0;
    for (unsigned int i = 0; i < count && stored != mRaceCountStatic; i++) {
        if (entries[i].mRaceHash != 0 && GetRaceFromHash(entries[i].mRaceHash) != nullptr) {
            *dest++ = entries[i];
            stored++;
        }
    }
}

// PC: 0x5DD240 (empty in the release build).
void GRaceDatabase::SimulateDDayComplete() {}

// PC: 0x600C30. The first D-Day race not completed in the career, if any.
const char *GRaceDatabase::GetNextDDayRace() {
    for (int onRace = 0; onRace < 5; onRace++) {
        GRaceParameters *parms = GetRaceFromHash(Attrib::StringHash32(sDDayRaces[onRace]));
        if (!IsCareerRaceComplete(parms->GetEventHash())) {
            return sDDayRaces[onRace];
        }
    }
    return nullptr;
}
