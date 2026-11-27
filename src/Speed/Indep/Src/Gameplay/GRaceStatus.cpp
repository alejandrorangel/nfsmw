#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"

#include "Speed/Indep/Libs/Support/Utility/FastMem.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Libs/Support/Utility/UVectorMath.h"
#include "Speed/Indep/Src/EAXSound/EAXSOund.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/Database/VehicleDB.hpp"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Generated/Events/EAutoSave.hpp"
#include "Speed/Indep/Src/Generated/Events/EFadeScreenOn.hpp"
#include "Speed/Indep/Src/Generated/Events/EReloadHud.hpp"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Generated/Messages/MLoadingComplete.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyRaceTime.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyRaceTimeExpired.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyRaceTimeSecTick.h"
#include "Speed/Indep/Src/Generated/Messages/MTriggerEnter.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/ICopMgr.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/ITrafficMgr.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/IAI.h"
#include "Speed/Indep/Src/Interfaces/Simables/IAudible.h"
#include "Speed/Indep/Src/Interfaces/Simables/IEngine.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRenderable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRigidBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Misc/Config.h"
#include "Speed/Indep/Src/Physics/PVehicle.h"
#include "Speed/Indep/Src/Physics/PhysicsInfo.hpp"
#include "Speed/Indep/Src/Sim/SimServer.h"
#include "Speed/Indep/Src/Sim/Simulation.h"
#include "Speed/Indep/Src/World/CarInfo.hpp"
#include "Speed/Indep/Src/World/OnlineManager.hpp"
#include "Speed/Indep/Src/World/TrackStreamer.hpp"
#include "Speed/Indep/Src/World/WCollisionAssets.h"
#include "Speed/Indep/Src/World/WRoadNetwork.h"
#include "Speed/Indep/bWare/Inc/bPrintf.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include <algorithm>
#include <iterator>
#include <set>

// Defined in Misc/GameFlow.cpp.
void EnableBarrierSceneryGroup(const char *group_name, bool flip_artwork);
void RedoTopologyAndSceneryGroups();
// Defined in World/rain.cpp.
void SetOverRideRainIntensity(float rov);
// Defined in Sim/Activities/QuickGame.cpp's TU (Frontend/Database/VehicleDB.cpp).
PresetCar *FindFEPresetCar(unsigned int key);
// Defined in Misc/Config.cpp.
extern const char *SkipFEOpponentPresetRide;


GRaceStatus *GRaceStatus::fObj = nullptr;

// Fixed thresholds used for the race stats (PC: function-local statics in
// GRacerInfo::Update, 0x91E0B0/0x91E0B4).
static const float kQuarterMileMeters = 402.335f;
static const float kSixtyMphMps = 26.8218f;

// ---------------------------------------------------------------------------
// GTimer
// ---------------------------------------------------------------------------

GTimer::GTimer() : mStartTime(Sim::GetTime()), mTotalTime(0.0f), mRunning(false) {}

GTimer::~GTimer() {}

// PC: inlined everywhere (e.g. GRaceStatus::StartMasterTimer).
void GTimer::Start() {
    mTotalTime = GetTime();
    mStartTime = Sim::GetTime();
    mRunning = true;
}

// PC: inlined everywhere (e.g. GRacerInfo::KnockOut).
void GTimer::Stop() {
    mTotalTime = GetTime();
    mRunning = false;
}

// PC: 0x5DDEA0.
void GTimer::Reset(float value) {
    mTotalTime = value;
    mStartTime = Sim::GetTime();
}

// PC: 0x5DDEC0.
float GTimer::GetTime() const {
    if (mRunning) {
        return Sim::GetTime() - mStartTime + mTotalTime;
    }
    return mTotalTime;
}

// PC: 0x5DDEE0.
void GTimer::SetTime(float time) {
    mRunning = false;
    mTotalTime = time;
}

// ---------------------------------------------------------------------------
// Catch-up data parsing (free functions in the PC TU)
// ---------------------------------------------------------------------------

// PC: 0x5DBFC0.
static int NotNumeric(char c) {
    if (c == '-' || c == '.') {
        return 0;
    }
    return c < '0' || c > '9';
}

// PC: 0x5DC000. Splits in place into words separated by characters func()
// accepts; *array is FastMem allocated.
static int SplitChars(char *in, char ***array, int (*func)(char)) {
    while (*in != '\0' && func(*in)) {
        in++;
    }

    int count = 0;
    for (char *temp = in; *temp != '\0';) {
        if (!func(*temp)) {
            count++;
            while (*temp != '\0' && !func(*temp)) {
                temp++;
            }
        } else {
            temp++;
        }
    }

    *array = static_cast<char **>(gFastMem.Alloc(count * sizeof(char *), nullptr));
    char *end = in;
    for (int i = 0; i < count; i++) {
        while (*end != '\0' && func(*end)) {
            end++;
        }
        (*array)[i] = end;
        while (*end != '\0' && !func(*end)) {
            end++;
        }
        if (*end != '\0') {
            *end++ = '\0';
        }
    }
    return count;
}

// PC: 0x5DC0F0. The PC skips a leading '-' but never negates the result.
static float ParseFloat(const char *word) {
    float total = 0.0f;
    float decimal = 1.0f;
    bool after_decimal = false;
    for (const char *c = word + (*word == '-'); *c != '\0'; c++) {
        if (*c == '.') {
            after_decimal = true;
            continue;
        }
        if (after_decimal) {
            decimal *= 0.1f;
        } else {
            total *= 10.0f;
        }
        int a = *c - '0';
        if (a < 1) {
            a = 0;
        } else if (a > 9) {
            a = 9;
        }
        total += static_cast<float>(a) * decimal;
    }
    return total;
}

// PC: 0x5E8CC0. Parses up to max floats out of string; when it holds more the
// values are resampled down to max. A single value is duplicated.
static int ParseArray(const char *string, float *data, int max) {
    int len = bStrLen(string);
    char *copy = static_cast<char *>(gFastMem.Alloc(len + 1, nullptr));
    bMemCpy(copy, string, len + 1);

    char **words;
    int num = SplitChars(copy, &words, NotNumeric);
    if (num > max) {
        float *temp = static_cast<float *>(gFastMem.Alloc(num * sizeof(float), nullptr));
        for (int i = 0; i < num; i++) {
            temp[i] = ParseFloat(words[i]);
        }
        Table table(temp, num, 0.0f, static_cast<float>(max - 1));
        for (int i = 0; i < max; i++) {
            data[i] = table.GetValue(static_cast<float>(i));
        }
        gFastMem.Free(temp, num * sizeof(float), nullptr);
        num = max;
    } else {
        for (int i = 0; i < num; i++) {
            data[i] = ParseFloat(words[i]);
        }
        if (num == 1) {
            data[1] = data[0];
            num = 2;
        }
    }
    gFastMem.Free(words, 0, nullptr);
    gFastMem.Free(copy, len + 1, nullptr);
    return num;
}

// ---------------------------------------------------------------------------
// GRacerInfo
// ---------------------------------------------------------------------------

// PC: 0x5EB460.
GRacerInfo::GRacerInfo() {
    ClearAll();
}

// PC: nullsub_156.
GRacerInfo::~GRacerInfo() {}

// PC: 0x5E84D0 (named SetIndex in the PC symbols).
void GRacerInfo::ClearAll() {
    mhSimable = 0;
    mGameCharacter = nullptr;
    mName = nullptr;
    mSavedHeatLevel = 0.0f;
    mSavedSpeed = 0.0f;
    mIndex = -1;
    mSavedPosition = UMath::Vector3::kZero;
    mSavedDirection = UMath::Vector3::kZero;
    ClearRaceStats();
}

// PC: 0x5E8300.
void GRacerInfo::ClearRaceStats() {
    mRanking = 0;
    mAiRanking = 0;
    mPctRaceComplete = 0.0f;
    mKnockedOut = false;
    mTotalled = false;
    mEngineBlown = false;
    mBusted = false;
    mChallengeComplete = false;
    mFinishedRacing = false;
    mCameraDetached = false;
    mPctLapComplete = 0.0f;
    mLapsCompleted = 0;
    mCheckpointsHitThisLap = 0;
    mTollboothsCrossed = 0;
    mSpeedTrapsCrossed = 0;
    mDistToNextCheckpoint = 0.0f;
    mDistanceDriven = 0.0f;
    mTopSpeed = 0.0f;
    mFinishingSpeed = 0.0f;
    mPoundsNOSUsed = 0.0f;
    mTimeCrossedLastCheck = 0.0f;
    mTotalUpdateTime = 0.0f;
    mPointTotal = 0.0f;
    mZeroToSixtyTime = 0.0f;
    mQuarterMileTime = 0.0f;
    mNumPerfectShifts = 0;
    mNumTrafficCarsHit = 0;
    mSpeedBreakerTime = 0.0f;
#ifndef EA_BUILD_A124
    mDNF = false;
#endif
    mRaceTimer.Stop();
    mRaceTimer.Reset(0.0f);
    mLapTimer.Stop();
    mLapTimer.Reset(0.0f);
    mCheckTimer.Stop();
    mCheckTimer.Reset(0.0f);
    for (int i = 0; i < 16; i++) {
        mTimeRemainingToBooth[i] = 0.0f;
        mSpeedTrapSpeed[i] = 0.0f;
        mSpeedTrapPosition[i] = -1;
    }
#ifndef EA_BUILD_A124
    for (int i = 0; i < 4; i++) {
        mSplitTimes[i] = 0.0f;
        mSplitRankings[i] = 0;
    }
#endif
}

// PC: 0x6004D0. A race can start part-way through (GetStartPercent); the HUD
// counts that part as done.
float GRacerInfo::GetHudPctRaceComplete() const {
    float start = GRaceStatus::Get().GetRaceParameters()->GetStartPercent();
    if (start <= 0.0f) {
        start = 0.0f;
    } else if (start > 1.0f) {
        start = 1.0f;
    }
    return start * 100.0f + (1.0f - start) * mPctRaceComplete;
}

#ifndef EA_BUILD_A124
// PC: 0x5E3400. Records the race time and ranking at 20/40/60/80 percent.
void GRacerInfo::UpdateSplits() {
    int split;
    if (mPctRaceComplete >= 80.0f && mSplitTimes[3] == 0.0f) {
        split = 3;
    } else if (mPctRaceComplete >= 60.0f && mSplitTimes[2] == 0.0f) {
        split = 2;
    } else if (mPctRaceComplete >= 40.0f && mSplitTimes[1] == 0.0f) {
        split = 1;
    } else if (mPctRaceComplete >= 20.0f && mSplitTimes[0] == 0.0f) {
        split = 0;
    } else {
        return;
    }
    mSplitTimes[split] = mRaceTimer.GetTime();
    mSplitRankings[split] = mRanking;
}

// PC: 0x5DC7D0.
bool GRacerInfo::AreStatsReady() const {
    if (GRaceStatus::Get().GetRaceContext() == GRace::kRaceContext_Online && !mFinishedRacing && !mEngineBlown && !mTotalled) {
        return false;
    }
    return true;
}
#endif

// PC: 0x5E9290.
bool GRacerInfo::IsLoading() const {
    ISimable *isim = GetSimable();
    if (isim != nullptr) {
        IVehicle *ivehicle;
        if (isim->QueryInterface(&ivehicle)) {
            return ivehicle->IsLoading();
        }
    }
    return false;
}

// PC: 0x5E9130.
void GRacerInfo::GetPosition(UMath::Vector3 &pos) const {
    ISimable *isim = GetSimable();
    if (isim != nullptr) {
        pos = isim->GetPosition();
        return;
    }
    pos = UMath::Vector3::kZero;
}

// PC: 0x5E91A0.
void GRacerInfo::GetLinearVelocity(UMath::Vector3 &vel) const {
    ISimable *isim = GetSimable();
    if (isim != nullptr) {
        isim->GetLinearVelocity(vel);
        return;
    }
    vel = UMath::Vector3::kZero;
}

// PC: 0x5E3390.
float GRacerInfo::CalcAverageSpeed() const {
    float time = mRaceTimer.GetTime();
    if (time > 0.0f) {
        return mDistanceDriven / time;
    }
    return 0.0f;
}

// PC: 0x5E33E0.
void GRacerInfo::SetSimable(ISimable *simable) {
    mhSimable = simable != nullptr ? simable->GetInstanceHandle() : 0;
}

// PC: 0x5DC6B0.
void GRacerInfo::SetName(char *name) {
    mName = name;
}

// PC: inlined in GRaceStatus::AddRacer/AddSimablePlayer.
void GRacerInfo::SetIndex(int index) {
    mIndex = index;
}

// PC: 0x5DC6C0.
void GRacerInfo::SetRanking(int ranking) {
    mRanking = ranking;
}

// PC: 0x5DC6D0.
void GRacerInfo::AddToPointTotal(float points) {
    mPointTotal += points;
    if (mPointTotal < 0.0f) {
        mPointTotal = 0.0f;
    }
}

// PC: 0x600210.
void GRacerInfo::KnockOut() {
    if (!mFinishedRacing) {
        mKnockedOut = true;
        Kill();
    }
}

// PC: 0x600420.
void GRacerInfo::BlowEngine() {
    if (!mFinishedRacing) {
        mEngineBlown = true;
        Kill();
    }
}

// PC: 0x6002C0.
void GRacerInfo::TotalVehicle() {
    if (!mFinishedRacing) {
        mTotalled = true;
        Kill();
    }
}

// PC: 0x600370.
void GRacerInfo::Busted() {
    if (!mFinishedRacing) {
        mBusted = true;
        Kill();
    }
}

// PC: 0x5DC6A0.
void GRacerInfo::ChallengeComplete() {
    mChallengeComplete = true;
}

// PC: inlined in KnockOut/BlowEngine/TotalVehicle/Busted. Stops the racer's
// clocks and re-ranks.
void GRacerInfo::Kill() {
    mRaceTimer.Stop();
    mLapTimer.Stop();
    mCheckTimer.Stop();
    GRaceStatus::Get().CalculateRankings();
}

// PC: 0x5E9240.
void GRacerInfo::ForceStop() {
    ISimable *isim = GetSimable();
    if (isim != nullptr) {
        IVehicle *ivehicle;
        if (isim->QueryInterface(&ivehicle)) {
            ivehicle->ForceStopOn(1);
        }
    }
}

// PC: 0x5E91F0.
void GRacerInfo::EndForceStop() {
    ISimable *isim = GetSimable();
    if (isim != nullptr) {
        IVehicle *ivehicle;
        if (isim->QueryInterface(&ivehicle)) {
            ivehicle->ForceStopOff(ivehicle->GetForceStop());
        }
    }
}

// PC: 0x5E92E0.
void GRacerInfo::Update(float dT) {
    if (mFinishedRacing || mEngineBlown || mTotalled || mKnockedOut) {
        return;
    }
    ISimable *isim = GetSimable();
    if (isim == nullptr) {
        return;
    }
    GRaceStatus &raceStatus = GRaceStatus::Get();

    IEngine *engine;
    if (isim->QueryInterface(&engine) && engine->IsNOSEngaged()) {
        mPoundsNOSUsed += engine->GetNOSFlowRate() * dT;
    }
    IPlayer *player = isim->GetPlayer();
    if (player != nullptr && player->InGameBreaker()) {
        mSpeedBreakerTime += dT;
    }

    UMath::Vector3 vel;
    isim->GetLinearVelocity(vel);
    float speed = UMath::Length(vel);
    float dist = speed * dT;
    if (speed > mTopSpeed) {
        mTopSpeed = speed;
    }
    mDistanceDriven += dist;
    mTotalUpdateTime += dT;
    if (mQuarterMileTime == 0.0f && mDistanceDriven >= kQuarterMileMeters) {
        mQuarterMileTime = mRaceTimer.GetTime();
    }
    if (mZeroToSixtyTime == 0.0f && mTopSpeed >= kSixtyMphMps) {
        mZeroToSixtyTime = mRaceTimer.GetTime();
    }

    float raceLength = raceStatus.GetRaceLength();
    IVehicleAI *ai;
    float distToNext = isim->QueryInterface(&ai) ? ai->GetPathDistanceRemaining() : 0.0f;
    mDistToNextCheckpoint = distToNext;
    if (raceLength > 0.0f) {
        int lap = mLapsCompleted;
        float lapsDistance = 0.0f;
        if (lap > 0) {
            lapsDistance = raceStatus.GetFirstLapLength();
        }
        if (lap > 1) {
            lapsDistance += static_cast<float>(lap - 1) * raceStatus.GetSubsequentLapLength();
        }
        int checks = mCheckpointsHitThisLap;
        float checksDistance = 0.0f;
        for (int i = 0; i < checks; i++) {
            checksDistance += raceStatus.GetSegmentLength(i, lap);
        }
        if (distToNext != 0.0f) {
            float lapDistance = raceStatus.GetSegmentLength(checks, lap) - distToNext + checksDistance;
            float totalDistance = lapDistance + lapsDistance;
            float lapLength = raceStatus.GetLapLength(lap);
            if (lapLength > 0.0f) {
                mPctLapComplete = bClamp(lapDistance / lapLength, 0.0f, 1.0f) * 100.0f;
            } else {
                mPctLapComplete = 100.0f;
            }
            mPctRaceComplete = bClamp(totalDistance / raceLength, 0.0f, 1.0f) * 100.0f;
        }
    }
}

// PC: 0x600540.
void GRacerInfo::SaveStartPosition() {
    mSavedHeatLevel = 0.0f;
    mSavedSpeed = 0.0f;
    mSavedPosition = UMath::Vector3::kZero;
    mSavedDirection = UMath::Vector3::kZero;
    ISimable *isim = GetSimable();
    if (isim == nullptr) {
        return;
    }
    IRigidBody *body = isim->GetRigidBody();
    if (body != nullptr) {
        mSavedPosition = body->GetPosition();
        mSavedSpeed = body->GetSpeed();
        body->GetForwardVector(mSavedDirection);
        if (mSavedSpeed == 0.0f) {
            mSavedSpeed = GRaceStatus::Get().GetRaceParameters()->GetInitialPlayerSpeed();
        }
    }
    IPerpetrator *perp;
    if (isim->QueryInterface(&perp)) {
        mSavedHeatLevel = perp->GetHeat();
    }
}

// PC: 0x5E96D0.
void GRacerInfo::RestoreStartPosition() {
    ISimable *isim = GetSimable();
    if (isim == nullptr) {
        return;
    }
    IRacer *racer;
    if (isim->QueryInterface(&racer)) {
        RacePreparationInfo rpi;
        rpi.Position = mSavedPosition;
        rpi.Speed = mSavedSpeed;
        rpi.Direction = mSavedDirection;
        rpi.HeatLevel = mSavedHeatLevel;
        rpi.Flags = RacePreparationInfo::RESET_DAMAGE;
        racer->PrepareForRace(rpi);
    }
}

// PC: 0x5DC750.
void GRacerInfo::ForceStartPosition(const UMath::Vector3 &pos, const UMath::Vector3 &dir) {
    mSavedPosition = pos;
    mSavedDirection = dir;
}

// PC: 0x6032A0.
void GRacerInfo::StartRace() {
    GRaceParameters *parms = GRaceStatus::Get().GetRaceParameters();
    float startTime = parms != nullptr ? parms->GetStartTime() : 0.0f;
    if (TheOnlineManager.IsOnlineRace()) {
        startTime = GRaceStatus::Get().GetRaceTimeRemaining();
    }
    mRaceTimer.Reset(startTime);
    mRaceTimer.Start();
    StartLap(1);
}

// PC: 0x602200.
void GRacerInfo::StartLap(int lapIndexOneBased) {
    mLapsCompleted = lapIndexOneBased - 1;
    mCheckpointsHitThisLap = 0;
    float startTime = 0.0f;
    if (TheOnlineManager.IsOnlineRace() && mLapsCompleted == 0) {
        startTime = GRaceStatus::Get().GetRaceTimeRemaining();
    }
    mLapTimer.Reset(startTime);
    mLapTimer.Start();
    StartCheckpoint(0);
}

// PC: 0x600660.
void GRacerInfo::StartCheckpoint(int checkIndex) {
    if (checkIndex == mCheckpointsHitThisLap + 1) {
        mCheckpointsHitThisLap = checkIndex;
    }
    float startTime = 0.0f;
    if (TheOnlineManager.IsOnlineRace() && mLapsCompleted == 0 && checkIndex == 0) {
        startTime = GRaceStatus::Get().GetRaceTimeRemaining();
    }
    mCheckTimer.Reset(startTime);
    mCheckTimer.Start();
    mTimeCrossedLastCheck = mRaceTimer.GetTime();
    mDistToNextCheckpoint = 0.0f;
}

// PC: 0x5E97B0.
void GRacerInfo::FinishRace() {
    mRaceTimer.Stop();
    mFinishedRacing = true;
    ISimable *isim = GetSimable();
    if (isim != nullptr) {
        UMath::Vector3 vel;
        isim->GetLinearVelocity(vel);
        mFinishingSpeed = UMath::Length(vel);
    }
}

// PC: 0x5E31D0. True when this racer ranks behind rhs.
bool GRacerInfo::IsBehind(const GRacerInfo &rhs) {
    if (mFinishedRacing) {
        if (rhs.mFinishedRacing) {
            return !(mRaceTimer.GetTime() <= rhs.mRaceTimer.GetTime());
        }
        return false;
    }
    if (rhs.mFinishedRacing) {
        return true;
    }
    if (mKnockedOut && rhs.mKnockedOut) {
        return !(rhs.mRaceTimer.GetTime() <= mRaceTimer.GetTime());
    }
    if (mEngineBlown && rhs.mEngineBlown) {
        return !(rhs.mRaceTimer.GetTime() <= mRaceTimer.GetTime());
    }
    if (mTotalled && rhs.mTotalled) {
        return mRaceTimer.GetTime() < rhs.mRaceTimer.GetTime();
    }
#ifndef EA_BUILD_A124
    if (mDNF && rhs.mDNF) {
        return rhs.mPctRaceComplete < mPctRaceComplete;
    }
#endif
    if (mKnockedOut || mEngineBlown || mTotalled) {
        return true;
    }
    if (rhs.mKnockedOut || rhs.mEngineBlown || rhs.mTotalled) {
        return false;
    }
    if (mLapsCompleted != rhs.mLapsCompleted) {
        return mLapsCompleted < rhs.mLapsCompleted;
    }
    return mPctRaceComplete < rhs.mPctRaceComplete;
}

// PC: 0x5E8E60. Spawns an AI racer's car from its character (preset ride or
// car type), falling back to default_key.
IVehicle *GRacerInfo::CreateVehicle(unsigned int default_key) {
    if (mGameCharacter == nullptr) {
        return nullptr;
    }
    const char *carType = mGameCharacter->CarType();
    mGameCharacter->CarTypeLowMem();
    const char *presetRide = mGameCharacter->PresetRide();

    FECustomizationRecord customization;
    if (SkipFE && bStrLen(SkipFEOpponentPresetRide) > 0) {
        presetRide = SkipFEOpponentPresetRide;
    }

    unsigned int key = 0;
    if (presetRide != nullptr) {
        PresetCar *preset = FindFEPresetCar(bStringHashUpper(presetRide));
        if (preset != nullptr) {
            customization.BecomePreset(preset);
            key = preset->VehicleKey;
        }
    }
    if (key == 0 && carType != nullptr && *carType != '\0') {
        key = Attrib::StringToKey(carType);
    }
    if (key == 0) {
        key = default_key;
    }

    Attrib::Gen::pvehicle attributes(key, 0, nullptr);
    if (!attributes.IsValid()) {
        return nullptr;
    }
    if (!customization.IsPreset()) {
        RideInfo ride;
        ride.Init(static_cast<CarType>(-1), CarRenderUsage_Player, 0, 0);
        const char *model = attributes.MODEL().GetString();
        if (model == nullptr) {
            model = "";
        }
        ride.Init(CarPartDB.GetCarType(bStringHashUpper(model)), CarRenderUsage_AIRacer, 0, 0);
        ride.SetRandomParts();
        customization.WriteRideIntoRecord(&ride);
    }

    UMath::Vector3 initialVec = {0.0f, 0.0f, 1.0f};
    Physics::Info::Performance match(1.0f, 1.0f, 1.0f);
    ISimable *isim = ISimable::CreateInstance(
        UCrc32("PVehicle"), VehicleParams(&GRaceStatus::Get(), DRIVER_RACER, key, initialVec, UMath::Vector3::kZero, 9, &customization, &match));
    if (isim != nullptr) {
        IVehicle *ivehicle;
        if (isim->QueryInterface(&ivehicle)) {
            mhSimable = isim->GetInstanceHandle();
            return ivehicle;
        }
    }
    return nullptr;
}

// PC: 0x6007B0. Fixes the racer's final time (time limit penalties, drag
// DNFs, the last lap time, untaken splits) and finishes it.
void GRacerInfo::FinalizeRaceStats() {
    if (mFinishedRacing) {
        return;
    }
    GRaceStatus &raceStatus = GRaceStatus::Get();
    GRaceParameters *parms = raceStatus.GetRaceParameters();
    float raceTime = mRaceTimer.GetTime();
    float pct = mPctRaceComplete;
    float finalTime = raceTime;
    if (pct > 0.1f) {
        finalTime = raceTime / (pct * 0.01f);
    }

    if (parms != nullptr && parms->GetRaceType() == GRace::kRaceType_Drag && (mTotalled || mEngineBlown || pct < 40.0f)) {
        finalTime = 0.0f;
#ifndef EA_BUILD_A124
        mDNF = true;
#endif
    }

    if (parms != nullptr && parms->GetRaceType() == GRace::kRaceType_SpeedTrap && mGameCharacter != nullptr &&
        raceStatus.GetRaceParameters() != nullptr) {
        parms->EnsureLoaded();
        int penaltyPerSec = parms->GetGameplayObj()->OvertimePenaltyPerSec();
        if (finalTime - raceTime > 0.0f) {
            AddToPointTotal(-((finalTime - raceTime) * static_cast<float>(penaltyPerSec)));
        }
    }

    if (parms->GetIsLoopingRace()) {
        if (mGameCharacter == nullptr) {
            goto finish;
        }
        int numLaps = parms->GetNumLaps();
        float lapsTotal = 0.0f;
        int lap = mLapsCompleted;
        for (int i = 0; i < numLaps; i++) {
            float lapTime = raceStatus.GetLapTime(i, mIndex, false);
            if (lapTime == 0.0f) {
                lap = i;
                break;
            }
            lapsTotal += lapTime;
            lap = mLapsCompleted;
        }
        raceStatus.SetLapTime(lap, mIndex, finalTime - lapsTotal);
    }

#ifndef EA_BUILD_A124
    if (mGameCharacter != nullptr) {
        float reached = mDNF ? pct : 1.0f;
        static const float splitPoints[4] = {0.2f, 0.4f, 0.6f, 0.8f};
        for (int i = 0; i < 4; i++) {
            if (mSplitTimes[i] == 0.0f) {
                mSplitTimes[i] = reached < splitPoints[i] ? 0.0f : finalTime * splitPoints[i];
            }
            if (mSplitRankings[i] == 0) {
                mSplitRankings[i] = mRanking;
            }
        }
    }
#endif

finish:
#ifndef EA_BUILD_A124
    if (!mDNF)
#endif
    {
        mRaceTimer.SetTime(finalTime);
        FinishRace();
    }
}

// PC: 0x5DC800.
void GRacerInfo::ChooseRandomName() {
    if (!DoesStringExist(bStringHash("RACERNAME_000"))) {
        mName = "UNKNOWN";
        return;
    }
    const char *name;
    bool taken;
    do {
        char buffer[32];
        bSPrintf(buffer, "RACERNAME_%03d", bRandom(150));
        name = GetLocalizedString(bStringHash(buffer));
        taken = false;
        GRaceStatus &raceStatus = GRaceStatus::Get();
        for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
            const char *other = raceStatus.GetRacerInfo(i).GetName();
            if (other != nullptr && bStrCmp(name, other) == 0) {
                taken = true;
                break;
            }
        }
    } while (taken);
    mName = name;
}

// PC: 0x600730. Career boss races show the blacklist rival's name.
bool GRacerInfo::ChooseBossName() {
    if (GRaceStatus::Exists() && GRaceStatus::Get().GetRaceContext() == GRace::kRaceContext_Career &&
        GRaceStatus::Get().GetRaceParameters() != nullptr && GRaceStatus::Get().GetRaceParameters()->GetIsBossRace()) {
        char buffer[32];
        bSNPrintf(buffer, 32, "BLACKLIST_RIVAL_%02d_LEADERBOARD", GRaceStatus::Get().GetCurrentBin()->GetBinNumber());
        mName = GetLocalizedString(bStringHash(buffer));
        return true;
    }
    return false;
}

// PC: 0x5E3500 (wraps 0x5DD2A0, which returns the name's string hash).
bool GRacerInfo::ChooseRacerName() {
    if (GRaceStatus::Exists() && GRaceStatus::Get().GetRaceContext() == GRace::kRaceContext_Career) {
        const char *racerName = mGameCharacter->RacerName();
        if (racerName != nullptr && bStrLen(racerName) != 0) {
            unsigned int hash = bStringHash(racerName);
            if (hash != 0) {
                mName = GetLocalizedString(hash);
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// GRaceStatus
// ---------------------------------------------------------------------------

// PC: 0x603900.
void GRaceStatus::Init() {
    fObj = new GRaceStatus();
}

// PC: 0x5DBD50.
void GRaceStatus::Shutdown() {
    if (fObj != nullptr) {
        delete fObj;
    }
    fObj = nullptr;
}

// PC: 0x603720.
GRaceStatus::GRaceStatus()
    : UTL::COM::Object(1),         //
      IVehicleCache(this),         //
      mRacerCount(0),              //
      mIsLoading(false),           //
      mPlayMode(kPlayMode_Racing), //
      mRaceContext(GRace::kRaceContext_Career),
      mRaceParms(nullptr),        //
      mRaceBin(nullptr),          //
#ifndef EA_BUILD_A124
      mPlayerPursuitInCooldown(false), //
#endif
      mBonusTime(0.0f),           //
      mTaskTime(0.0f),            //
      mSuddenDeathMode(false),    //
      mTimeExpiredMsgSent(false), //
      mActivelyRacing(false),     //
      mLastSecondTickSent(0),     //
      mCheckpointModel(nullptr),  //
      mCheckpointEmitter(nullptr), //
      mQueueBinChange(false),     //
#ifndef EA_BUILD_A124
      mWarpWhenInFreeRoam(0), //
#endif
      mNumTollbooths(0),          //
      mScriptWaitingForLoad(false), //
#ifndef EA_BUILD_A124
      mRefreshBinAfterRace(false), //
#endif
      mNextCheckpoint(nullptr), //
      fRaceLength(0.0f),        //
      fFirstLapLength(0.0f),    //
      fSubsequentLapLength(0.0f), //
#ifndef EA_BUILD_A124
      mCaluclatedAdaptiveGain(false), //
#endif
      fCatchUpIntegral(0.0f),       //
      fCatchUpDerivative(0.0f),     //
      fCatchUpAdaptiveBonus(0.0f),  //
      fAveragePercentComplete(0.0f), //
      nCatchUpSkillEntries(0),      //
      nCatchUpSpreadEntries(0),     //
      nSpeedTraps(0),               //
      mVehicleCacheLocked(false),   //
      bRaceRouteError(false),       //
      mTrafficDensity(0),           //
      mTrafficPattern(0),           //
      mHasBeenWon(false) {
    unsigned int currentBin = FEDatabase->GetCareerSettings()->GetCurrentBin();
    mRaceBin = GRaceDatabase::Get().GetBinNumber(currentBin);
    bMemSet(mSegmentLengths, 0, sizeof(mSegmentLengths));
    bMemSet(aCatchUpSkillData, 0, sizeof(aCatchUpSkillData));
    bMemSet(aCatchUpSpreadData, 0, sizeof(aCatchUpSpreadData));
    bMemSet(aSpeedTraps, 0, sizeof(aSpeedTraps));
    ClearTimes();
    SyncronizeAdaptiveBonus();
    MakeDefaultCatchUpData();
    if (GRaceDatabase::Get().GetStartupRace() == nullptr) {
        EnterBin(currentBin);
        SetRoaming();
    }
}

// PC: 0x5EE920.
GRaceStatus::~GRaceStatus() {
    if (mCheckpointModel != nullptr) {
        delete mCheckpointModel;
        mCheckpointModel = nullptr;
    }
    if (mCheckpointEmitter != nullptr) {
        mCheckpointEmitter->UnSubscribe();
        delete mCheckpointEmitter;
        mCheckpointEmitter = nullptr;
    }
    ClearCheckpoints();
}

#ifndef EA_BUILD_A124
// PC: 0x5EEA50.
const char *GRaceStatus::GetCacheName() const {
    return "GRaceStatus";
}
#endif

// PC: 0x5E3020.
void GRaceStatus::EnableBinBarriers() {
    if (mRaceBin != nullptr) {
        mRaceBin->EnableBarriers();
    }
}

// PC: 0x5E3030.
void GRaceStatus::DisableBinBarriers() {
    if (mRaceBin != nullptr) {
        mRaceBin->DisableBarriers();
    }
}

// PC: 0x5DBD70.
void GRaceStatus::RefreshBinWhileInGame() {
    mQueueBinChange = true;
}

// PC: 0x602BB0.
void GRaceStatus::EnterBin(unsigned int binNumber) {
    if (mRaceBin != nullptr) {
        mRaceBin->DisableBarriers();
    }
    mRaceBin = GRaceDatabase::Get().GetBinNumber(binNumber);
    if (mRaceBin != nullptr) {
        mRaceBin->EnableBarriers();
        GVault *childVault = mRaceBin->GetChildVault();
        if (childVault != nullptr && !childVault->IsLoaded()) {
            GManager::Get().LoadVaultSync(childVault);
        }
    }
    if (GManager::Get().GetInGameplay()) {
        // TODO(port): the PC now restarts the world activities and runs the
        // bin's GActivity (GManager::StartWorldActivities, GActivity::Run) and
        // refreshes the engage/speed trap icons; the gameplay runtime
        // instances aren't ported.
    }
}

// PC: 0x5FDE80. Sorts by IsBehind (or by points) and assigns rankings; AI
// rankings skip the human racers.
void GRaceStatus::CalculateRankings() {
    GRacerInfo *sortedRacers[16];
    for (int idx = 0; idx < mRacerCount; idx++) {
        sortedRacers[idx] = &mRacerInfo[idx];
    }
    bool sortByPoints = mRaceParms->GetRankPlayersByPoints();
    for (int sort1 = mRacerCount - 1; sort1 > 0; sort1--) {
        for (int sort2 = 0; sort2 < sort1; sort2++) {
            GRacerInfo *a = sortedRacers[sort2];
            GRacerInfo *b = sortedRacers[sort1];
            bool swap;
            if (sortByPoints && a->GetPointTotal() + b->GetPointTotal() > 1.0f) {
                swap = a->GetPointTotal() < b->GetPointTotal();
            } else {
                swap = a->IsBehind(*b);
            }
            if (swap) {
                sortedRacers[sort1] = a;
                sortedRacers[sort2] = b;
            }
        }
    }
    int players_encountered = 0;
    for (int ranking = 1; ranking <= mRacerCount; ranking++) {
        GRacerInfo *info = sortedRacers[ranking - 1];
        info->mRanking = ranking;
        if (info->GetGameCharacter() == nullptr) {
            info->mAiRanking = 0;
            players_encountered++;
        } else {
            info->mAiRanking = ranking - players_encountered;
        }
    }
}

// PC: 0x5DBD80. Ranks each speed trap's speeds, fastest first.
void GRaceStatus::SortCheckPointRankings() {
    for (int i = 0; i < 16; i++) {
        GRacerInfo *sortedRacers[16];
        for (int idx = 0; idx < mRacerCount; idx++) {
            sortedRacers[idx] = &mRacerInfo[idx];
        }
        for (int sort1 = mRacerCount - 1; sort1 > 0; sort1--) {
            for (int sort2 = 0; sort2 < sort1; sort2++) {
                float spd1 = sortedRacers[sort2]->mSpeedTrapSpeed[i];
                float spd2 = sortedRacers[sort1]->mSpeedTrapSpeed[i];
                if (spd1 < spd2) {
                    GRacerInfo *temp = sortedRacers[sort1];
                    sortedRacers[sort1] = sortedRacers[sort2];
                    sortedRacers[sort2] = temp;
                }
            }
        }
        for (int rankIndex = 0; rankIndex < mRacerCount; rankIndex++) {
            sortedRacers[rankIndex]->mSpeedTrapPosition[i] = rankIndex + 1;
        }
    }
}

// PC: 0x602C80.
void GRaceStatus::Update(float dT) {
#ifndef EA_BUILD_A124
    if (mPlayMode == kPlayMode_Racing && mRefreshBinAfterRace) {
        mQueueBinChange = true;
        mRefreshBinAfterRace = false;
    }
#endif
    if (mPlayMode == kPlayMode_Roaming && mQueueBinChange) {
        EnterBin(FEDatabase->GetCareerSettings()->GetCurrentBin());
        mQueueBinChange = false;
    }
#ifndef EA_BUILD_A124
    if (mPlayMode == kPlayMode_Roaming && mWarpWhenInFreeRoam != 0 && GManager::Get().WarpToMarker(mWarpWhenInFreeRoam, false)) {
        mWarpWhenInFreeRoam = 0;
    }
#endif

    int num_racers = mRacerCount;
    if (mPlayMode == kPlayMode_Racing && num_racers > 0) {
        int num_ai_racers = 0;
        for (int idx = 0; idx < num_racers; idx++) {
            if (mRacerInfo[idx].GetGameCharacter() != nullptr) {
                num_ai_racers++;
            }
        }

        float percent_complete = 0.0f;
        float weights = 0.0f;
        for (int idx = 0; idx < num_racers; idx++) {
            GRacerInfo &info = mRacerInfo[idx];
            info.Update(dT);
            if (info.mhSimable != 0) {
                // The human counts as much as all of the AI together.
                float weight = 1.0f;
                if (info.GetGameCharacter() == nullptr && static_cast<float>(num_ai_racers) >= 1.0f) {
                    weight = static_cast<float>(num_ai_racers);
                }
                percent_complete += weight * info.mPctRaceComplete;
                weights += weight;
            }
        }
        if (weights < 1.0f) {
            weights = 1.0f;
        }
        fAveragePercentComplete = percent_complete / weights;

        CalculateRankings();
#ifndef EA_BUILD_A124
        for (int idx = 0; idx < num_racers; idx++) {
            mRacerInfo[idx].UpdateSplits();
        }
#endif

        MNotifyRaceTime(GetRaceTimeElapsed(), GetIsTimeLimited(), GetRaceTimeRemaining()).Post(UCrc32(UCRC32_Gameplay));
        float elapsed = GetRaceTimeElapsed();
        int elapsedSec = static_cast<int>(elapsed);
        if (elapsedSec > mLastSecondTickSent) {
            mLastSecondTickSent = elapsedSec;
            MNotifyRaceTimeSecTick(elapsed).Post(UCrc32(UCRC32_Gameplay));
        }

        if (GetIsTimeLimited()) {
            // Challenge races pause the clock while the player's pursuit cools
            // down.
            if (IsChallengeRace()) {
                if (mPlayerPursuitInCooldown) {
                    if (mRaceMasterTimer.IsRunning()) {
                        mRaceMasterTimer.Stop();
                    }
                } else if (!mRaceMasterTimer.IsRunning()) {
                    mRaceMasterTimer.Start();
                }
            }
            if (!mTimeExpiredMsgSent && GetRaceTimeRemaining() <= 0.0f) {
                MNotifyRaceTimeExpired().Post(UCrc32(UCRC32_Gameplay));
                mTimeExpiredMsgSent = true;
                for (int idx = 0; idx < mRacerCount; idx++) {
                    GRacerInfo &info = mRacerInfo[idx];
                    if (info.GetGameCharacter() == nullptr && !info.mFinishedRacing) {
                        ISimable *isim = info.GetSimable();
                        IVehicle *ivehicle;
                        if (isim != nullptr && isim->QueryInterface(&ivehicle)) {
                            ivehicle->ForceStopOn(1);
                        }
                    }
                }
            }
        }
    }

    if (mScriptWaitingForLoad) {
        bool racersLoading = IsLoading();
        bool trackLoading = TheTrackStreamer.IsLoadingInProgress() && TheTrackStreamer.IsFarLoadingInProgress();
        bool copsSpawning = ICopMgr::Exists() && ICopMgr::Get()->IsCopSpawnPending();
        if (!racersLoading && !trackLoading && !copsSpawning) {
            MLoadingComplete().Post(UCrc32(UCRC32_Gameplay));
            mScriptWaitingForLoad = false;
        }
    }
}

// PC: 0x5E8A20. While racing (or locked) the racers' cars are wanted; while
// roaming the cop and traffic managers may take a car once it is out of view.
eVehicleCacheResult GRaceStatus::OnQueryVehicleCache(const IVehicle *removethis, const IVehicleCache *whosasking) const {
    if (mPlayMode == kPlayMode_Racing || mVehicleCacheLocked) {
        for (int onRacer = 0; onRacer < mRacerCount; onRacer++) {
            ISimable *isim = mRacerInfo[onRacer].GetSimable();
            if (isim == nullptr) {
                if (removethis == nullptr) {
                    return VCR_WANT;
                }
            } else if (removethis != nullptr && UTL::COM::ComparePtr(isim, removethis)) {
                return VCR_WANT;
            }
        }
        return VCR_DONTCARE;
    }

    bool askedByCops;
    if (whosasking == nullptr) {
        askedByCops = !ICopMgr::Exists();
    } else {
        askedByCops = ICopMgr::Exists() && UTL::COM::ComparePtr(whosasking, ICopMgr::Get());
    }
    if (!askedByCops && !UTL::COM::ComparePtr(whosasking, ITrafficMgr::Get())) {
        return VCR_DONTCARE;
    }
    return CanUnspawnRoamer(removethis) ? VCR_DONTCARE : VCR_WANT;
}

// PC: 0x5DBEB0.
void GRaceStatus::OnRemovedVehicleCache(IVehicle *ivehicle) {}

// PC: 0x5E8960. A roaming racer's car can go once it's been off screen for a
// while and is far from the camera.
bool GRaceStatus::CanUnspawnRoamer(const IVehicle *roamer) const {
    if (!roamer->IsActive()) {
        return true;
    }
    for (int onRacer = 0; onRacer < mRacerCount; onRacer++) {
        ISimable *isim = mRacerInfo[onRacer].GetSimable();
        if (isim != nullptr && UTL::COM::ComparePtr(isim, roamer)) {
            if (roamer->GetOffscreenTime() >= 4.0f && Sim::DistanceToCamera(roamer->GetPosition()) >= 100.0f) {
                return true;
            }
            return false;
        }
    }
    return true;
}

// PC: 0x5DBEC0.
void GRaceStatus::SetRaceContext(GRace::Context context) {
    mRaceContext = context;
}

// PC: 0x5DBED0.
GRacerInfo &GRaceStatus::GetRacerInfo(int index) {
    return mRacerInfo[index];
}

// PC: 0x5E8B20.
GRacerInfo *GRaceStatus::GetRacerInfo(ISimable *isim) {
    for (int i = 0; i < mRacerCount; i++) {
        if (mRacerInfo[i].GetSimable() == isim) {
            return &mRacerInfo[i];
        }
    }
    return nullptr;
}

// PC: 0x5DBEF0.
int GRaceStatus::GetRacerCount() const {
    return mRacerCount;
}

// PC: 0x5EB4E0. The first player that finished in first place.
GRacerInfo *GRaceStatus::GetWinningPlayerInfo() {
    const IPlayer::List &players = IPlayer::GetList(PLAYER_ALL);
    for (IPlayer::List::const_iterator iter = players.begin(); iter != players.end(); ++iter) {
        GRacerInfo *info = GetRacerInfo((*iter)->GetSimable());
        if (info != nullptr && info->IsFinishedRacing() && info->GetRanking() == 1) {
            return info;
        }
    }
    return nullptr;
}

// PC: 0x5FDFF0.
void GRaceStatus::StartMasterTimer() {
    float startTime = mRaceParms != nullptr ? mRaceParms->GetStartTime() : 0.0f;
    if (TheOnlineManager.IsOnlineRace()) {
        // TODO(port): online races sync to the session start time
        // (Sim::GetTime() - online start - 3s); online isn't supported.
        startTime = 0.0f;
    }
    mRaceMasterTimer.Reset(startTime);
    mRaceMasterTimer.Start();
}

// PC: 0x5E8BA0.
void GRaceStatus::StopMasterTimer() {
    mRaceMasterTimer.Stop();
}

// PC: 0x5E3060.
float GRaceStatus::GetRaceTimeElapsed() const {
    if (mRaceParms == nullptr) {
        return 0.0f;
    }
    return mRaceMasterTimer.GetTime();
}

// PC: 0x5FE090.
float GRaceStatus::GetRaceTimeRemaining() const {
    if (mRaceParms != nullptr) {
        float limit = mRaceParms->GetTimeLimit() + mBonusTime;
        if (limit > 0.0f) {
            float remaining = limit - mRaceMasterTimer.GetTime();
            return remaining < 0.0f ? 0.0f : remaining;
        }
    }
    return 0.0f;
}

// PC: 0x5FFD80. Also rewinds the master clock and the time limit state.
void GRaceStatus::ClearTimes() {
    bMemSet(mLapTimes, 0, sizeof(mLapTimes));
    bMemSet(mCheckTimes, 0, sizeof(mCheckTimes));
    float startTime = mRaceParms != nullptr ? mRaceParms->GetStartTime() : 0.0f;
    mRaceMasterTimer.Stop();
    mRaceMasterTimer.Reset(startTime);
    mBonusTime = 0.0f;
    mTaskTime = 0.0f;
    mTimeExpiredMsgSent = false;
    mSuddenDeathMode = false;
    mLastSecondTickSent = 0;
}

// PC: 0x5DC260.
void GRaceStatus::SetLapTime(int lapIndex, int racerIndex, float time) {
    mLapTimes[lapIndex][racerIndex] = time;
}

// PC: 0x5DC280.
float GRaceStatus::GetLapTime(int lapIndex, int racerIndex, bool bCumulativeTimeAtLap) {
    if (!bCumulativeTimeAtLap) {
        return mLapTimes[lapIndex][racerIndex];
    }
    float total = 0.0f;
    for (int lap = 0; lap <= lapIndex; lap++) {
        if (mLapTimes[lap][racerIndex] <= 0.0f) {
            return 0.0f;
        }
        total += mLapTimes[lap][racerIndex];
    }
    return total;
}

// PC: 0x5DC2F0.
void GRaceStatus::SetCheckpointTime(int lapIndex, int checkIndex, int racerIndex, float time) {
    mCheckTimes[lapIndex][checkIndex][racerIndex] = time;
}

// PC: 0x5FFE30.
float GRaceStatus::GetCheckpointTime(int lapIndex, int checkIndex, int racerIndex, bool bCumulative) {
    if (!bCumulative) {
        return mCheckTimes[lapIndex][checkIndex][racerIndex];
    }
    unsigned int numChecks = mRaceParms->GetNumCheckpoints();
    float total = 0.0f;
    for (unsigned int check = 1; check < numChecks; check++) {
        float time = mCheckTimes[lapIndex][check][racerIndex];
        if (time <= 0.0f) {
            total = 0.0f;
            break;
        }
        total += time;
        if (check == static_cast<unsigned int>(checkIndex)) {
            break;
        }
    }
    if (static_cast<unsigned int>(checkIndex) == numChecks) {
        float time = mCheckTimes[lapIndex][0][racerIndex];
        if (time > 0.0f) {
            return total + time;
        }
        return 0.0f;
    }
    return total;
}

// PC: 0x5DC320.
int GRaceStatus::GetLapPosition(int lapIndex, int racerIndex, bool bOverallPosition) {
    float time = GetLapTime(lapIndex, racerIndex, bOverallPosition);
    int position = 0;
    for (int i = 0; i < mRacerCount; i++) {
        if (i == racerIndex) {
            continue;
        }
        float other = GetLapTime(lapIndex, i, bOverallPosition);
        if (other < time && other > 0.0f) {
            position++;
        }
    }
    return position + 1;
}

// PC: 0x5FFF50.
int GRaceStatus::GetCheckpointPosition(int lapIndex, int checkIndex, int racerIndex) {
    float time = GetCheckpointTime(lapIndex, checkIndex, racerIndex, true);
    int position = 0;
    for (int i = 0; i < mRacerCount; i++) {
        if (i == racerIndex) {
            continue;
        }
        float other = GetCheckpointTime(lapIndex, checkIndex, i, true);
        if (other < time && other > 0.0f) {
            position++;
        }
    }
    return position + 1;
}

// PC: 0x5E3120. Race time behind the fastest other racer.
float GRaceStatus::GetFinishTimeBehind(int racerIndex) {
    float time = mRacerInfo[racerIndex].GetRaceTime();
    float best = time;
    for (int i = 0; i < mRacerCount; i++) {
        if (i != racerIndex && mRacerInfo[i].GetRaceTime() < best) {
            best = mRacerInfo[i].GetRaceTime();
        }
    }
    return time - best;
}

// PC: 0x5FFFD0.
float GRaceStatus::GetBestLapTime(int racerIndex) {
    float best = mLapTimes[0][racerIndex];
    for (int lap = 1; lap < mRaceParms->GetNumLaps(); lap++) {
        float time = mLapTimes[lap][racerIndex];
        if (time > 0.0f && time < best) {
            best = time;
        }
    }
    return best;
}

// PC: 0x6000A0.
float GRaceStatus::GetWorstLapTime(int racerIndex) {
    float worst = mLapTimes[0][racerIndex];
    for (int lap = 1; lap < mRaceParms->GetNumLaps(); lap++) {
        float time = mLapTimes[lap][racerIndex];
        if (time > 0.0f && time > worst) {
            worst = time;
        }
    }
    return worst;
}

// PC: 0x600170.
int GRaceStatus::GetLapsLed(int racerIndex) {
    int lapsLed = 0;
    for (int lap = 0; lap < mRaceParms->GetNumLaps(); lap++) {
        if (GetLapPosition(lap, racerIndex, false) == 1) {
            lapsLed++;
        }
    }
    return lapsLed;
}

// PC: 0x5DC720. The first lap's first segment starts at the start line; later
// laps start it at the finish line.
float GRaceStatus::GetSegmentLength(int segment, int lap) {
    float length = mSegmentLengths[segment];
    if (lap > 0 && segment == 0) {
        length += fSubsequentLapLength - fFirstLapLength;
    }
    return length;
}

// PC: 0x5ED880.
bool GRaceStatus::IsLoading() {
    return IsAudioLoading() || IsModelsLoading() || !GManager::Get().StockCarsLoaded();
}

// PC: 0x5E95F0.
bool GRaceStatus::IsAudioLoading() {
    for (int idx = 0; idx < mRacerCount; idx++) {
        ISimable *isim = mRacerInfo[idx].GetSimable();
        IAudible *audible;
        if (isim != nullptr && isim->QueryInterface(&audible) && !audible->IsAudible()) {
            return true;
        }
    }
    return false;
}

// PC: 0x5E9660.
bool GRaceStatus::IsModelsLoading() {
    for (int idx = 0; idx < mRacerCount; idx++) {
        ISimable *isim = mRacerInfo[idx].GetSimable();
        IRenderable *renderable;
        if (isim != nullptr && isim->QueryInterface(&renderable) && !renderable->IsRenderable()) {
            return true;
        }
    }
    return false;
}

// PC: 0x5FE110. Fakes the racer through every lap: equal lap times and a
// trigger-enter message per checkpoint.
void GRaceStatus::SkipToEndOfRaceForRacer(ISimable *thisPlayer, int index, float time) {
    if (mPlayMode != kPlayMode_Racing) {
        return;
    }
    GRaceParameters *parms = mRaceParms;
    float lapTime = time / static_cast<float>(parms->GetNumLaps());
    for (int lap = 0; lap < parms->GetNumLaps(); lap++) {
        mLapTimes[lap][index] = lapTime;
        for (int i = 0; i < GetCheckpointCount(); i++) {
            GTrigger *trigger = mCheckpoints[i];
            GCollectionKey sender;
            sender.mCollectionKey = trigger != nullptr ? trigger->GetCollection() : 0;
            MTriggerEnter(sender, thisPlayer->GetInstanceHandle()).Post(UCrc32(UCRC32_Gameplay));
        }
    }
}

// PC: 0x5FE260. The AI racers ahead of place finish just before finishTime,
// then the local player.
void GRaceStatus::SkipToEndOfRaceInPlace(int place, float finishTime) {
    int index = place;
    if (mRacerCount > 0) {
        index = place - 1;
        for (int i = 0; i < mRacerCount; i++) {
            if (i == index) {
                break;
            }
            ISimable *isim = mRacerInfo[i].GetSimable();
            if (isim == nullptr) {
                continue;
            }
            if (isim->IsPlayer()) {
                index++;
            } else {
                SkipToEndOfRaceForRacer(isim, i, finishTime - 0.9f);
            }
        }
    }
    IPlayer *player = IPlayer::First(PLAYER_LOCAL);
    SkipToEndOfRaceForRacer(player->GetSimable(), index, finishTime);
}

// PC: 0x5E30A0. Kills the AI racers' cars.
void GRaceStatus::ClearRacers() {
    const IVehicle::List &racers = IVehicle::GetList(VEHICLE_RACERS);
    for (IVehicle::List::const_iterator iter = racers.begin(); iter != racers.end(); ++iter) {
        ISimable *isim = (*iter)->GetSimable();
        if (isim != nullptr && !isim->IsPlayer()) {
            isim->Kill();
        }
    }
    mRacerCount = 0;
}

// PC: 0x5E8BF0.
GRacerInfo &GRaceStatus::AddSimablePlayer(ISimable *isim) {
    GRacerInfo &info = mRacerInfo[mRacerCount++];
    info.ClearAll();
    info.mhSimable = isim != nullptr ? isim->GetInstanceHandle() : 0;
    info.mIndex = mRacerCount - 1;
    if (Sim::GetUserMode() == Sim::USER_SPLIT_SCREEN) {
        info.mName = GetLocalizedString(mRacerCount == 1 ? 0x7b070984 : 0x7b070985);
        return info;
    }
    int settingsIndex = isim->GetPlayer()->GetSettingsIndex();
    UserProfile *profile = FEDatabase->GetUserProfile(settingsIndex);
    if (profile != nullptr) {
        info.mName = profile->GetProfileName();
    } else {
        info.mName = GetLocalizedString(0xf760eabe);
    }
    return info;
}

// PC: 0x601F90.
void GRaceStatus::AddRacer(GRuntimeInstance *racer) {
    GRacerInfo &info = mRacerInfo[mRacerCount++];
    info.ClearAll();
    info.mGameCharacter = static_cast<GCharacter *>(racer);
    info.mIndex = mRacerCount - 1;
    if (!info.ChooseBossName() && !info.ChooseRacerName()) {
        info.ChooseRandomName();
    }
}

// PC: 0x5EEA60. The script is told (MLoadingComplete, from Update) once the
// racers, the track and the cops are loaded; the screen fades meanwhile.
void GRaceStatus::NotifyScriptWhenLoaded() {
    bool racersLoading = IsLoading();
    bool trackLoading = TheTrackStreamer.IsLoadingInProgress() && TheTrackStreamer.IsFarLoadingInProgress();
    bool copsSpawning = ICopMgr::Exists() && ICopMgr::Get()->IsCopSpawnPending();
    if (racersLoading || trackLoading || copsSpawning) {
        new EFadeScreenOn(false);
    }
    mScriptWaitingForLoad = true;
}

// PC: 0x5FE3E0.
void GRaceStatus::SetRoaming() {
    bool finalBossRace = false;
    if (mRaceParms != nullptr && bStrCmp(mRaceParms->GetEventID(), "16.2.1") == 0) {
        finalBossRace = true;
    }
    if (mRaceParms == nullptr) {
        g_pEAXSound->StartNewGamePlay();
    } else {
        mRaceParms->EnsureLoaded();
        if (mRaceParms->GetGameplayObj()->PostRaceActivity().mCollectionKey == 0) {
            g_pEAXSound->StartNewGamePlay();
        }
    }

    mPlayMode = kPlayMode_Roaming;
    mRaceContext = GRace::kRaceContext_Career;
    mRaceParms = nullptr;
    mIsLoading = false;
    WRoadNetwork::Get().ResetShortcuts();

    const IPlayer::List &players = IPlayer::GetList(PLAYER_ALL);
    for (IPlayer::List::const_iterator iter = players.begin(); iter != players.end(); ++iter) {
        IPlayer *player = *iter;
        if (player->InGameBreaker()) {
            player->ToggleGameBreaker();
        }
        ISimable *isim = player->GetSimable();
        IVehicle *ivehicle;
        if (isim != nullptr && isim->QueryInterface(&ivehicle)) {
            ivehicle->ForceStopOff(ivehicle->GetForceStop());
            IVehicleAI *ai;
            if (ivehicle->QueryInterface(&ai) && ai->GetPursuit() == nullptr) {
                ICopMgr::Get()->LockoutCops(true);
            }
        }
    }

    if (!finalBossRace && !GManager::Get().GetHasPendingRestartEvent()) {
        new EReloadHud();
    }
    new EAutoSave();
    if (mRaceParms == nullptr || !mRaceParms->GetIsDDayRace()) {
        SetOverRideRainIntensity(0.0f);
    }
    // TODO(port): GManager::SpawnAllLoadedSectionIcons (icons aren't ported).
    ICopMgr::mDisableCops = 0;
}

// PC: 0x602010 (called from Game_StartRace).
void GRaceStatus::SetRacing() {
    mPlayMode = kPlayMode_Racing;
    ClearTimes();

    const IPlayer::List &players = IPlayer::GetList(PLAYER_ALL);
    for (IPlayer::List::const_iterator iter = players.begin(); iter != players.end(); ++iter) {
        IPlayer *player = *iter;
        if (player->InGameBreaker()) {
            player->ToggleGameBreaker();
        }
    }

    mNumTollbooths = 0;
    // TODO(port): the PC counts the enabled tollbooth triggers here
    // (GObjectIterator<GTrigger> over the runtime instance ring, flag 0x800);
    // the gameplay runtime instances aren't ported, so there are none.

    bool autoSave = mRaceParms != nullptr && mRaceParms->GetIsDDayRace() && bStrCmp(mRaceParms->GetEventID(), "16.1.0") != 0;
    if (autoSave || FEDatabase->IsFinalEpicChase()) {
        new EAutoSave();
    }
    new EReloadHud();
#ifndef EA_BUILD_A124
    mCaluclatedAdaptiveGain = false;
#endif
}

// PC: 0x6031F0 (called from Game_SetRaceActivity).
void GRaceStatus::SetRaceActivity(GActivity *activity) {
    mRaceParms = GRaceDatabase::Get().GetRaceFromKey(activity->GetCollection());
}

// PC: 0x5FE330.
void GRaceStatus::EnableBarriers() {
    if (mRaceParms == nullptr) {
        return;
    }
    for (unsigned int i = 0; i < mRaceParms->GetBarrierCount(); i++) {
        EnableBarrierSceneryGroup(mRaceParms->GetBarrierName(i), mRaceParms->GetBarrierIsFlipped(i));
    }
    WCollisionAssets::Get().SetExclusionFlags();
    WRoadNetwork::Get().ResolveBarriers();
}

// PC: 0x5DBF00.
void GRaceStatus::DisableBarriers() {
    RedoTopologyAndSceneryGroups();
    WRoadNetwork::Get().ResetBarriers();
    WRoadNetwork::Get().ResetRaceSegments();
}

// PC: 0x5DBF20.
void GRaceStatus::AddAvailableEventToMap(GRuntimeInstance *trigger, GRuntimeInstance *activity) {}

// PC: 0x5DBF30.
void GRaceStatus::AddSpeedTrapToMap(GRuntimeInstance *trigger) {}

// PC: 0x5FE600. Each racer remembers how much time was left at each booth.
void GRaceStatus::AwardBonusTime(float seconds) {
    for (int i = 0; i < mRacerCount; i++) {
        mRacerInfo[i].NotifyTollboothCrossed(GRaceStatus::Get().GetRaceTimeRemaining());
    }
    mBonusTime += seconds;
}

// PC: 0x5EEB40.
void GRaceStatus::ClearCheckpoints() {
    mCheckpoints.clear();
    TriggerList().swap(mCheckpoints);
}

// PC: 0x5F5C20.
void GRaceStatus::AddCheckpoint(GRuntimeInstance *trigger) {
    mCheckpoints.push_back(static_cast<GTrigger *>(trigger));
}

// PC: 0x5DBF40.
void GRaceStatus::NotifyEmitterGroupDelete(void *obj, EmitterGroup *group) {
    GRaceStatus *raceStatus = static_cast<GRaceStatus *>(obj);
    if (group == raceStatus->mCheckpointEmitter) {
        raceStatus->mCheckpointEmitter = nullptr;
    }
}

// PC: 0x5FE660. Moves the checkpoint marker model to the next checkpoint (or
// removes it when there's none or they aren't shown).
void GRaceStatus::SetNextCheckpointPos(GRuntimeInstance *trigger) {
    mNextCheckpoint = static_cast<GTrigger *>(trigger);
    bool visible = false;
    if (mRaceParms != nullptr) {
        visible = mRaceParms->GetCheckpointsVisible();
    }
    if (trigger == nullptr || !visible) {
        if (mCheckpointModel != nullptr) {
            delete mCheckpointModel;
        }
        mCheckpointModel = nullptr;
        if (mCheckpointEmitter != nullptr) {
            mCheckpointEmitter->UnSubscribe();
            delete mCheckpointEmitter;
            mCheckpointEmitter = nullptr;
        }
        return;
    }
    if (mCheckpointModel == nullptr) {
        mCheckpointModel = new WorldModel(0x738b1f9b, nullptr, false);
    }
    UMath::Vector3 pos;
    mNextCheckpoint->GetPosition(pos);
    bMatrix4 matrix;
    bIdentity(&matrix);
    matrix.v3.x = pos.z;
    matrix.v3.y = -pos.x;
    matrix.v3.z = pos.y;
    mCheckpointModel->SetMatrix(&matrix);
    if (mCheckpointEmitter != nullptr) {
        mCheckpointEmitter->SetLocalWorld(&matrix);
    }
}

namespace {

DECLARE_CONTAINER_TYPE(ID_ROAD_SET);
DECLARE_CONTAINER_TYPE(ID_PATH_SET);

typedef UTL::Std::set<short, _type_ID_ROAD_SET> ROAD_SET;

// One path the route can take between two checkpoints and the roads it uses.
struct PathSegment {
    float Length;   // offset 0x0, size 0x4
    ROAD_SET Roads; // offset 0x4, size 0x10

    bool operator<(const PathSegment &rhs) const {
        return Length < rhs.Length;
    }
};

typedef UTL::Std::set<PathSegment, _type_ID_PATH_SET> PATH_SET;

} // namespace

// PC: 0x5FE840. Route distance between two path points. Every shortcut on the
// route is tried in turn; roads that only the longer alternatives use get
// scaled so the AI sees the real cost of taking them.
float GRaceStatus::DetermineRaceSegmentLength(const UMath::Vector4 *positions, const UMath::Vector4 *directions, int start, int end) {
    WRoadNav nav;
    nav.SetDecisionFilter(true);
    nav.SetNavType(WRoadNav::kTypeDirection);
    nav.SetPathType(WRoadNav::kPathRaceRoute);
    mRaceParms->EnsureLoaded();
    mRaceParms->GetNumCheckpoints();

    const UMath::Vector3 &startPos = UMath::Vector4To3(positions[start]);
    const UMath::Vector3 &endPos = UMath::Vector4To3(positions[end]);
    const UMath::Vector3 &startDir = UMath::Vector4To3(directions[start]);
    const UMath::Vector3 &endDir = UMath::Vector4To3(directions[end]);
    float distance = UMath::Distance(startPos, endPos);

    nav.InitAtPoint(startPos, startDir, true, 1.0f);
    if (!nav.IsValid()) {
        bRaceRouteError = true;
        mRaceParms->EnsureLoaded();
        mRaceParms->GetNumCheckpoints();
        return distance;
    }

    float extra_distance = 0.0f;
    if (start == end) {
        // A lap that ends where it starts: get off the starting segment first.
        short old_segment_number = nav.GetSegmentInd();
        float old_segment_length = WRoadNetwork::Get().GetSegment(old_segment_number)->GetLength();
        do {
            float f = (1.0f - nav.GetSegmentTime()) * old_segment_length;
            if (f <= 0.01f) {
                f = 0.01f;
            }
            nav.IncNavPosition(f, startDir, 0.0f);
            extra_distance += f;
        } while (old_segment_number == nav.GetSegmentInd());
    }

    PATH_SET path_set;
    char shortcut_allowed[32];
    bMemSet(shortcut_allowed, 1, sizeof(shortcut_allowed));
    bool shortcut_free = true;
    int num_paths;
    for (;;) {
        nav.SetNumPathSegments(0);
        nav.SetNavType(WRoadNav::kTypeDirection);
        nav.FindPathNow(&endPos, &endDir, shortcut_allowed);
        if (nav.GetNavType() != WRoadNav::kTypePath) {
            num_paths = shortcut_free ? static_cast<int>(path_set.size()) : 0;
            break;
        }
        WRoadNetwork::Get().AddRaceSegments(&nav);
        distance = nav.GetPathDistanceRemaining();
        uint8 shortcut = nav.FirstShortcutInPath();
        shortcut_free = shortcut == 0xff;
        if (!shortcut_free) {
            shortcut_allowed[shortcut] = 0;
        }

        PathSegment path_segment;
        path_segment.Length = distance;
        int num_segments = nav.GetNumPathSegments();
        for (int K = 0; K < num_segments; K++) {
            const WRoadSegment *segment = WRoadNetwork::Get().GetSegment(nav.GetPathSegment(K));
            if (!segment->IsDecision()) {
                path_segment.Roads.insert(segment->fRoadID);
            }
        }
        path_set.insert(path_segment);
        if (shortcut_free) {
            num_paths = static_cast<int>(path_set.size());
            break;
        }
    }
    distance = extra_distance + distance;

    if (num_paths == 0) {
        bRaceRouteError = true;
    } else if (num_paths > 1) {
        const PathSegment *path_segments[32];
        int n = 0;
        for (PATH_SET::const_iterator it = path_set.begin(); it != path_set.end() && n < 32; ++it) {
            path_segments[n++] = &*it;
        }
        for (int k = 1; k < num_paths; k++) {
            const PathSegment *shorter_path = path_segments[k - 1];
            const PathSegment *longer_path = path_segments[k];
            ROAD_SET unique_roads;
            std::set_difference(longer_path->Roads.begin(), longer_path->Roads.end(), shorter_path->Roads.begin(), shorter_path->Roads.end(),
                                std::inserter(unique_roads, unique_roads.begin()));
            float longer_by = longer_path->Length - shorter_path->Length;
            float unique_length = 0.0f;
            for (ROAD_SET::const_iterator it = unique_roads.begin(); it != unique_roads.end(); ++it) {
                unique_length += static_cast<float>(WRoadNetwork::Get().GetRoad(*it)->nLength) * (4000.0f / 65535.0f);
            }
            if (unique_length > 0.0f && !unique_roads.empty()) {
                float scale = (longer_by + unique_length) / unique_length;
                uint16 nScale = static_cast<uint16>(static_cast<int>(scale * 65536.0f) >> 8);
                for (ROAD_SET::const_iterator it = unique_roads.begin(); it != unique_roads.end(); ++it) {
                    WRoadNetwork::Get().GetRoadNonConst(*it)->nScale = nScale;
                }
            }
        }
    }
    return distance;
}

// PC: 0x5FEF30 (called from Game_StartRace).
void GRaceStatus::DetermineRaceLength() {
    nSpeedTraps = 0;
    fRaceLength = 0.0f;
    fFirstLapLength = 0.0f;
    fSubsequentLapLength = 0.0f;
    bMemSet(mSegmentLengths, 0, sizeof(mSegmentLengths));
    bRaceRouteError = false;
    WRoadNetwork::Get().ResolveShortcuts();

    GRaceParameters *race_parameters = mRaceParms;
    if (race_parameters == nullptr || !race_parameters->HasFinishLine()) {
        return;
    }
    WRoadNetwork::Get().SetRaceFilterValid(true);

    int num_checkpoints = race_parameters->GetNumCheckpoints();
    int num_path_points = num_checkpoints + 2;
    UMath::Vector4 positions[18];
    UMath::Vector4 directions[18];
    race_parameters->GetStartPosition(UMath::Vector4To3(positions[0]));
    race_parameters->GetStartDirection(UMath::Vector4To3(directions[0]));
    race_parameters->GetFinishPosition(UMath::Vector4To3(positions[num_path_points - 1]));
    race_parameters->GetFinishDirection(UMath::Vector4To3(directions[num_path_points - 1]));
    for (int i = 0; i < num_checkpoints; i++) {
        race_parameters->GetCheckpointPosition(i, UMath::Vector4To3(positions[i + 1]));
        race_parameters->GetCheckpointDirection(i, UMath::Vector4To3(directions[i + 1]));
    }

    float total_distance = 0.0f;
    bool race_loops = race_parameters->GetIsLoopingRace();
    int num_segment_lengths = race_loops ? num_path_points : num_path_points - 1;
    for (int i = 0; i < num_segment_lengths; i++) {
        mSegmentLengths[i] = DetermineRaceSegmentLength(positions, directions, i, i % (num_path_points - 1) + 1);
        total_distance += mSegmentLengths[i];
    }
    if (!race_loops) {
        fRaceLength = total_distance;
        fFirstLapLength = total_distance;
        fSubsequentLapLength = total_distance;
    } else {
        fSubsequentLapLength = total_distance - mSegmentLengths[0];
        fFirstLapLength = total_distance - mSegmentLengths[num_path_points - 1];
        fRaceLength = static_cast<float>(race_parameters->GetNumLaps() - 1) * fSubsequentLapLength + fFirstLapLength;
    }

    // Mark the road past the finish line as part of the race.
    WRoadNav nav;
    nav.SetDecisionFilter(true);
    nav.SetNavType(WRoadNav::kTypeDirection);
    nav.SetPathType(WRoadNav::kPathRaceRoute);
    nav.InitAtPoint(UMath::Vector4To3(positions[num_path_points - 1]), UMath::Vector4To3(directions[num_path_points - 1]), true, 1.0f);
    if (nav.IsValid()) {
        for (int i = 0; i < 100; i++) {
            nav.IncNavPosition(1.0f, UMath::Vector3::kZero, 0.0f);
            WRoadSegment *segment = WRoadNetwork::Get().GetSegmentNonConst(nav.GetSegmentInd());
            segment->SetInRace(true);
            segment->SetRaceRouteForward(nav.GetNodeInd() == 1);
        }
    }

    // TODO(port): the PC now collects the race's speed traps (the GTrigger
    // runtime instances not flagged OpenWorldSpeedTrap) into aSpeedTraps; the
    // gameplay runtime instances aren't ported, so there are none.
    nSpeedTraps = 0;
}

// PC: 0x5E8E20.
void GRaceStatus::ParseCatchUpData(const char *skill, const char *spread) {
    nCatchUpSkillEntries = ParseArray(skill, aCatchUpSkillData, 11);
    nCatchUpSpreadEntries = ParseArray(spread, aCatchUpSpreadData, 11);
}

// PC: 0x5DC200.
void GRaceStatus::MakeDefaultCatchUpData() {
    aCatchUpSkillData[1] = 0.2f;
    aCatchUpSkillData[0] = 0.2f;
    fCatchUpIntegral = 5e-05f;
    fCatchUpDerivative = 0.001f;
    nCatchUpSkillEntries = 2;
    nCatchUpSpreadEntries = 2;
    aCatchUpSpreadData[1] = 1000.0f;
    aCatchUpSpreadData[0] = 1000.0f;
}

// PC: 0x5FFCC0 (called from Game_StartRace).
void GRaceStatus::MakeCatchUpData() {
    if (mRaceParms == nullptr) {
        MakeDefaultCatchUpData();
        return;
    }
    fCatchUpIntegral = mRaceParms->GetCatchUpIntegral();
    fCatchUpDerivative = mRaceParms->GetCatchUpDerivative();
    ParseCatchUpData(mRaceParms->GetCatchUpSkill(), mRaceParms->GetCatchUpSpread());
}

// PC: 0x5DC180.
float GRaceStatus::GetAdaptiveDifficutly() const {
    if (mRaceContext == GRace::kRaceContext_Career) {
        return fCatchUpAdaptiveBonus;
    }
    return 0.0f;
}

// PC: 0x5DC1A0.
void GRaceStatus::SyncronizeAdaptiveBonus() {
    fCatchUpAdaptiveBonus = FEDatabase->GetCareerSettings()->GetAdaptiveDifficulty();
}

// Rubber band tables by percent complete (PC: 0x8F5B58.., data at 0x8A3774..).
static const float kGlueSpreadEasy[5] = {1000.0f, 900.0f, 750.0f, 600.0f, 800.0f};
static const float kGlueSpreadHard[5] = {300.0f, 275.0f, 250.0f, 150.0f, 250.0f};
static const float kGlueSkillEasy[5] = {0.75f, 0.75f, 0.75f, 0.5f, 0.25f};
static const float kGlueSkillHard[5] = {0.75f, 0.75f, 0.75f, 0.75f, 0.25f};
// Quick race glue level per difficulty (PC: 0x8A37D8).
static const float kQuickRaceGlueLevel[GRace::kRaceDifficulty_NumDifficulties] = {0.0f, 0.5f, 1.0f};

// PC: 0x5FF990. The AI rubber band: a PID on how far the AI is from the
// player, tuned by difficulty (quick race), adaptive difficulty (career) or the
// race's own catch-up tables.
bool GRaceStatus::ComputeCatchUpSkill(GRacerInfo *racer_info, PidError *pid, float *output, float *skill, bool off_world) {
    bool use_race_override = false;
    bool boss_race = false;
    float glue_level;
    if (off_world) {
        glue_level = 1.0f;
    } else if (mRaceContext == GRace::kRaceContext_QuickRace) {
        if (mRaceParms == nullptr || !mRaceParms->GetCatchUp()) {
            return false;
        }
        glue_level = kQuickRaceGlueLevel[mRaceParms->GetDifficulty()];
    } else if (mRaceContext == GRace::kRaceContext_Career) {
        glue_level = bClamp((fCatchUpAdaptiveBonus + 1.0f) * 0.5f, 0.0f, 1.0f);
        if (mRaceParms != nullptr) {
            use_race_override = mRaceParms->GetCatchUpOverride();
            if (mRaceParms->GetIsBossRace()) {
                boss_race = true;
                glue_level = (1.0f - glue_level) * 0.5f + glue_level;
            }
        }
    } else {
        return false;
    }

    float percent_complete = racer_info->GetPctRaceComplete();
    float glue_integral_error = pid->GetErrorIntegral();
    float glue_derivative_error = pid->GetErrorDerivative();
    float glue_skill;
    float glue_spread;
    float glue_integral;
    float glue_derivative;
    if (use_race_override) {
        glue_skill = Table(aCatchUpSkillData, nCatchUpSkillEntries, 0.0f, 100.0f).GetValue(percent_complete);
        glue_spread = Table(aCatchUpSpreadData, nCatchUpSpreadEntries, 0.0f, 100.0f).GetValue(percent_complete);
        glue_integral = fCatchUpIntegral;
        glue_derivative = fCatchUpDerivative;
    } else {
        float spreadHard = Table(kGlueSpreadHard, 5, 0.0f, 100.0f).GetValue(percent_complete);
        float spreadEasy = Table(kGlueSpreadEasy, 5, 0.0f, 100.0f).GetValue(percent_complete);
        glue_spread = (spreadHard - spreadEasy) * glue_level + spreadEasy;
        float skillHard = Table(kGlueSkillHard, 5, 0.0f, 100.0f).GetValue(percent_complete);
        float skillEasy = Table(kGlueSkillEasy, 5, 0.0f, 100.0f).GetValue(percent_complete);
        glue_skill = (skillHard - skillEasy) * glue_level + skillEasy;
        glue_integral = glue_level * 0.0f + 5e-05f;
        glue_derivative = glue_level * 0.0f + 0.01f;
    }
    if (glue_spread < 100.0f) {
        glue_spread = 100.0f;
    }

    float f = (2.0f / glue_spread) * pid->GetError() + glue_derivative * glue_derivative_error + glue_integral * glue_integral_error;
    f = bClamp(f, -1.0f, 1.0f);
    *skill = f * glue_skill;
    *output = boss_race ? 1.0f : f;
    return true;
}

// PC: 0x5FF2E0 (named GRaceStatus::__unk in the PC symbols). Nudges the
// career adaptive difficulty once per race by how the player did.
void GRaceStatus::UpdateAdaptiveDifficulty(eAdaptiveGainReason reason, ISimable *who) {
#ifndef EA_BUILD_A124
    if (mCaluclatedAdaptiveGain) {
        return;
    }
#endif
    if (mRaceContext != GRace::kRaceContext_Career || mRacerCount < 2 || Sim::GetUserMode() != Sim::USER_SINGLE ||
        mPlayMode != kPlayMode_Racing) {
        return;
    }
    if (mRaceParms != nullptr && mRaceParms->GetNoPostRaceScreen()) {
        return;
    }
    if (fRaceLength <= 0.0f) {
        return;
    }

    float difficulty = fCatchUpAdaptiveBonus;
    GRacerInfo *aiWinner = nullptr;
    GRacerInfo *humanWinner = nullptr;
    GRacerInfo *humanKnockedOut = nullptr;
    for (int i = 0; i < mRacerCount; i++) {
        GRacerInfo &info = mRacerInfo[i];
        if (info.GetGameCharacter() == nullptr) {
            if (info.mBusted) {
                return;
            }
            if (info.mFinishedRacing && info.mRanking == 1) {
                humanWinner = &info;
            } else if (GetRaceType() == GRace::kRaceType_Knockout && info.mKnockedOut) {
                humanKnockedOut = &info;
            }
        } else if (info.mFinishedRacing && info.mRanking == 1) {
            aiWinner = &info;
        }
    }

    float f;
    float humanPct;
    if (reason == kAdaptiveGain_FromEngineBlown || reason == kAdaptiveGain_FromVehicleDestroyed) {
        if (who == nullptr || !who->IsPlayer()) {
            return;
        }
        float aiPct = 0.0f;
        humanPct = 0.0f;
        for (int i = 0; i < mRacerCount; i++) {
            GRacerInfo &info = mRacerInfo[i];
            if (!info.mKnockedOut) {
                if (info.GetGameCharacter() == nullptr) {
                    humanPct = UMath::Max(humanPct, info.mPctRaceComplete);
                } else {
                    aiPct = UMath::Max(aiPct, info.mPctRaceComplete);
                }
            }
        }
        if (aiPct <= humanPct || humanPct <= 0.0f) {
            goto done;
        }
        f = (aiPct - humanPct) * fRaceLength;
    } else if (reason == kAdaptiveGain_FromRestart) {
        float aiPct = 0.0f;
        humanPct = 0.0f;
        for (int i = 0; i < mRacerCount; i++) {
            GRacerInfo &info = mRacerInfo[i];
            if (!info.mKnockedOut && !info.mTotalled && !info.mEngineBlown) {
                if (info.GetGameCharacter() == nullptr) {
                    humanPct = UMath::Max(humanPct, info.mPctRaceComplete);
                } else {
                    aiPct = UMath::Max(aiPct, info.mPctRaceComplete);
                }
            }
        }
        if (aiPct <= humanPct || humanPct <= 0.0f) {
            goto done;
        }
        f = (aiPct - humanPct) * fRaceLength;
    } else {
        if (GetRaceType() == GRace::kRaceType_SpeedTrap) {
            float humanPoints = 0.0f;
            float aiPoints = 0.0f;
            for (int i = 0; i < mRacerCount; i++) {
                GRacerInfo &info = mRacerInfo[i];
                if (!info.mFinishedRacing) {
                    return;
                }
                if (info.GetGameCharacter() == nullptr) {
                    humanPoints = info.mPointTotal;
                } else {
                    aiPoints = UMath::Max(aiPoints, info.mPointTotal);
                }
            }
            float most = UMath::Max(humanPoints, aiPoints);
            if (most <= 0.0f || aiPoints <= 0.0f || humanPoints <= 0.0f) {
                return;
            }
            float margin = (humanPoints - aiPoints) / most;
            if (margin <= 0.0f) {
                difficulty = bClamp(UMath::Ramp(-margin, 0.0f, 0.2f) * -0.2f + difficulty, -1.0f, 1.0f);
            } else {
                difficulty = bClamp(UMath::Ramp(margin, 0.05f, 0.2f) * 0.2f + difficulty, -1.0f, 1.0f);
            }
        } else if (humanWinner != nullptr) {
            // Won: harder by how far back the best AI was.
            float best = 0.0f;
            for (int i = 0; i < mRacerCount; i++) {
                GRacerInfo &info = mRacerInfo[i];
                if (&info != humanWinner && !info.mFinishedRacing && !info.mTotalled && !info.mEngineBlown) {
                    best = bMax(best, info.mPctRaceComplete);
                }
            }
            float win_margin = (100.0f - best) * fRaceLength * 0.01f;
            if (win_margin > 0.0f && best > 0.0f) {
                difficulty = bClamp(UMath::Ramp(win_margin, 200.0f, 750.0f) * 0.4f + difficulty, -1.0f, 1.0f);
            }
        } else if (aiWinner != nullptr) {
            // Lost: easier by how far from the finish the player was.
            bool updated = false;
            for (int i = 0; i < mRacerCount; i++) {
                GRacerInfo &info = mRacerInfo[i];
                if (info.GetGameCharacter() == nullptr && !info.mFinishedRacing) {
                    float pct = info.mPctRaceComplete;
                    float lose_margin = (100.0f - pct) * fRaceLength * 0.01f;
                    if (lose_margin > 0.0f) {
                        difficulty = bClamp((UMath::Ramp(lose_margin, 0.0f, 300.0f) * -0.3f - 0.1f) * pct * 0.01f + difficulty, -1.0f, 1.0f);
                        updated = true;
                    }
                }
            }
            if (!updated) {
                return;
            }
        } else {
            if (humanKnockedOut == nullptr) {
                return;
            }
            int laps = mRaceParms->GetNumLaps();
            if (laps < 2) {
                laps = 1;
            }
            float lose_margin = UMath::Mod((100.0f - humanKnockedOut->mPctRaceComplete) * fRaceLength * 0.01f, fRaceLength / static_cast<float>(laps));
            if (lose_margin <= 0.0f) {
                return;
            }
            difficulty = bClamp((UMath::Ramp(lose_margin, 0.0f, 300.0f) * -0.3f - 0.1f) * humanKnockedOut->mPctRaceComplete * 0.01f + difficulty,
                                -1.0f, 1.0f);
        }
        goto done;
    }
    difficulty = bClamp(UMath::Ramp(f * 0.01f, 0.0f, 300.0f) * -0.4f * humanPct * 0.01f + difficulty, -1.0f, 1.0f);

done:
#ifndef EA_BUILD_A124
    mCaluclatedAdaptiveGain = true;
#endif
    FEDatabase->GetCareerSettings()->SetAdaptiveDifficulty(difficulty);
    fCatchUpAdaptiveBonus = difficulty;
}

// PC: 0x5E3100.
int GRaceStatus::GetCheckpointCount() {
    return static_cast<int>(mCheckpoints.size());
}

// PC: 0x5ED860.
GTrigger *GRaceStatus::GetCheckpoint(int index) {
    return mCheckpoints[index];
}

// PC: 0x5DC250.
GTrigger *GRaceStatus::GetNextCheckpoint() {
    return mNextCheckpoint;
}

// PC: 0x5DC3A0.
float GRaceStatus::GetRaceSpeedTrapSpeed(int trapIndex, int racerIndex) {
    return mRacerInfo[racerIndex].mSpeedTrapSpeed[trapIndex];
}

// PC: 0x5DC3C0.
int GRaceStatus::GetRaceSpeedTrapPosition(int trapIndex, int racerIndex) {
    return mRacerInfo[racerIndex].mSpeedTrapPosition[trapIndex];
}

// PC: 0x5DC3E0. As in the PC, "best" is the lowest recorded speed.
float GRaceStatus::GetBestSpeedTrapSpeed(int racerIndex) {
    float best = mRacerInfo[racerIndex].mSpeedTrapSpeed[0];
    for (int i = 1; i < nSpeedTraps; i++) {
        float speed = mRacerInfo[racerIndex].mSpeedTrapSpeed[i];
        if (speed > 0.0f && speed < best) {
            best = speed;
        }
    }
    return best;
}

// PC: 0x5DC530. As in the PC, "worst" is the highest recorded speed.
float GRaceStatus::GetWorstSpeedTrapSpeed(int racerIndex) {
    float worst = mRacerInfo[racerIndex].mSpeedTrapSpeed[0];
    for (int i = 1; i < nSpeedTraps; i++) {
        float speed = mRacerInfo[racerIndex].mSpeedTrapSpeed[i];
        if (speed > 0.0f && speed > worst) {
            worst = speed;
        }
    }
    return worst;
}

// PC: 0x5DC680.
float GRaceStatus::GetRaceTollboothTime(int boothIndex, int racerIndex) {
    return mRacerInfo[racerIndex].mTimeRemainingToBooth[boothIndex];
}

// PC: 0x603220. Abandoning a career boss race resets the bin's boss races.
void GRaceStatus::RaceAbandoned() {
    if (mRaceContext != GRace::kRaceContext_Career || mRaceBin == nullptr || mRaceParms == nullptr) {
        return;
    }
    if (!mRaceParms->GetIsBossRace() || mRaceParms->GetIsEpicPursuitRace()) {
        return;
    }
    unsigned int count = mRaceBin->GetBossRaceCount();
    for (unsigned int i = 0; i < count; i++) {
        GRaceDatabase::Get().ResetCareerCompleteFlag(mRaceBin->GetBossRaceHash(i));
    }
    // TODO(port): GManager::RefreshEngageTriggerIcons (icons aren't ported).
}

// PC: 0x5EB560.
void GRaceStatus::EndStopAll() {
    for (int i = 0; i < mRacerCount; i++) {
        mRacerInfo[i].EndForceStop();
    }
}

// PC: 0x6021B0.
void GRaceStatus::FinalizeRaceStats() {
    if (mRaceContext == GRace::kRaceContext_Online) {
        return;
    }
    for (int i = 0; i < mRacerCount; i++) {
        mRacerInfo[i].FinalizeRaceStats();
    }
    UpdateAdaptiveDifficulty(kAdaptiveGain_FromFinalizeRace, nullptr);
}
