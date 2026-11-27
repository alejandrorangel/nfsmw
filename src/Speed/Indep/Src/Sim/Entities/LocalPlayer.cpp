#include "LocalPlayer.hpp"
#include "Speed/Indep/Libs/Support/Utility/UCOM.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/HUD/FEPkg_Hud.hpp"
#include "Speed/Indep/Src/Frontend/Localization/Localize.hpp"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/simsurface.h"
#include "Speed/Indep/Src/Generated/Events/EPursuitBreaker.hpp"
#include "Speed/Indep/Src/Input/IFeedBack.h"
#include "Speed/Indep/Src/Input/IOModule.h"
#include "Speed/Indep/Src/Input/ISteeringWheel.h"
#include "Speed/Indep/Src/Interfaces/IAttachable.h"
#include "Speed/Indep/Src/Interfaces/IFengHud.h"
#include "Speed/Indep/Src/Interfaces/ITaskable.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/IGameState.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/INIS.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IEntity.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/IAI.h"
#include "Speed/Indep/Src/Interfaces/Simables/IEngine.h"
#include "Speed/Indep/Src/Interfaces/Simables/IEngineDamage.h"
#include "Speed/Indep/Src/Interfaces/Simables/IINput.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISpikeable.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISuspension.h"
#include "Speed/Indep/Src/Interfaces/Simables/ITransmission.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Misc/Profiler.hpp"
#include "Speed/Indep/Src/Physics/PhysicsTypes.h"
#include "Speed/Indep/Src/Sim/SimEntity.h"
#include "Speed/Indep/Src/Sim/SimSurface.h"
#include "Speed/Indep/Src/Sim/SimTypes.h"
#include "Speed/Indep/Src/Sim/Simulation.h"
#include "Speed/Indep/Src/World/TrackPath.hpp"
#include "Speed/Indep/Tools/Inc/ConversionUtil.hpp"
#include "Speed/Indep/bWare/Inc/bMath.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#if defined(EA_PORT_NATIVE)
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/FEPkg_PostRace.hpp"
#include "Speed/Indep/Src/Gameplay/GInfractionManager.h"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Gameplay/GTrigger.h"
#include "Speed/Indep/Src/Interfaces/Simables/ICarAudio.h"
#include "Speed/Indep/Src/Interfaces/Simables/IInductable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRigidBody.h"
#include "Speed/Indep/Src/Physics/PVehicle.h"
#include <math.h>
#endif

#if defined(EA_PORT_NATIVE)
// PC 0x6FD280. QuickGame::CreatePlayers creates "LocalPlayer" through the
// entity factory; the decomp has neither Construct nor the registration.
Sim::IEntity *LocalPlayer::Construct(Sim::Param params) {
    return new LocalPlayer(params);
}

Sim::IEntity::Prototype _LocalPlayer(UCrc32("LocalPlayer"), LocalPlayer::Construct);
#endif

LocalPlayer::LocalPlayer(Sim::Param params)
    : IPlayer(this),            //
      mSettingIndex(-1),        //
      mName(""),                //
      mGameBreakerCharge(1.0f), //
      mFFB(nullptr),            //
      mWheelDevice(nullptr),    //
      mRenderPort(-1),          //
      mControllerPort(-1),      //
      mNeighbourhoodHash(0),    //
      mHud(nullptr),            //
      mHudTask(nullptr),        //
      mSpeech(nullptr),         //
      mInGameBreaker(false)
#ifndef EA_BUILD_A124
      ,
      mLastPursuit(nullptr)
#endif
{
    IEntity::AddToList(ENTITY_PLAYERS);
    IPlayer::AddToList(PLAYER_LOCAL);
    IPlayer::AddToList(PLAYER_ALL);
    mSpeech = Sim::IActivity::CreateInstance("SoundAI", Sim::Param());
    if (mSpeech) {
        Attach(mSpeech);
    }
    mHudTask = AddTask("WorldUpdate", 1.0f, 0.0f, Sim::TASK_FRAME_VARIABLE);
    Sim::ProfileTask(mHudTask, "Hud");
}

void LocalPlayer::ReleaseHud() {
    if (mHud) {
        mHud->Release();
        mHud = nullptr;
    }
}

PlayerSettings *LocalPlayer::GetSettings() const {
    if (mSettingIndex >= 0) {
        return FEDatabase->GetPlayerSettings(mSettingIndex);
    }
    return nullptr;
}

void LocalPlayer::SetHud(ePlayerHudType ht) {
    if (ht == PHT_NONE) {
        ReleaseHud();
        return;
    } else if (mSettingIndex < 0) {
        return;
    }
    const char *hud_name = HudResourceManager::GetHudFengName(ht);
    if (!hud_name) {
        ReleaseHud();
    } else {
        ReleaseHud();
        mHud = ::new (__FILE__, __LINE__) FEngHud(ht, hud_name, this, mSettingIndex);
    }
}

void LocalPlayer::OnAttached(IAttachable *pOther) {
    IVehicle *ivehicle;
    if (pOther->QueryInterface(&ivehicle)) {
        if (mSpeech) {
            mSpeech->Attach(ivehicle);
        }
        Sim::Collision::AddListener(this, ivehicle, "LocalPlayer");
    }
    Sim::Entity::OnAttached(pOther);
}

void LocalPlayer::OnDetached(IAttachable *pOther) {
    IVehicle *ivehicle;
    if (pOther->QueryInterface(&ivehicle)) {
        if (mSpeech) {
            mSpeech->Detach(ivehicle);
        }
        Sim::Collision::RemoveListener(this, ivehicle);
        SetGameBreaker(false);
    }
    if (UTL::COM::ComparePtr(mSpeech, pOther)) {
        mSpeech = nullptr;
    }
    Sim::Entity::OnDetached(pOther);
}

LocalPlayer::~LocalPlayer() {
    if (mFFB) {
        mFFB->ResetEffects();
        mFFB = nullptr;
    }
    SetGameBreaker(false);
    if (mHudTask) {
        Sim::Object::RemoveTask(mHudTask);
    }
    ReleaseHud();
    if (mSpeech) {
        mSpeech->Release();
    }
    Sim::Collision::RemoveListener(this);
}

void LocalPlayer::SetGameBreaker(bool on) {
    if (on != mInGameBreaker) {
        new EPursuitBreaker(on ? 1 : 0);
        mInGameBreaker = on;
    }
}

// UNSOLVED
bool LocalPlayer::CanDoGameBreaker() {
    if (Sim::GetUserMode() != Sim::USER_SINGLE) {
        return false;
    }
    ISimable *isimable = static_cast<IEntity *>(this)->GetSimable();
    IVehicle *ivehicle;
    if (!isimable || !INIS::Exists() || !isimable->QueryInterface(&ivehicle)) {
        return false;
    }
    float speed_mph = MPS2MPH(ivehicle->GetSpeedometer());
    if (speed_mph < 30.0f) {
        return false;
    }
    if (!ivehicle->IsAnimating() && !ivehicle->IsStaging() && !ivehicle->IsLoading()) {
        return true;
    }
    return false;
}

bool LocalPlayer::ToggleGameBreaker() {
    if (!CanDoGameBreaker() && !mInGameBreaker) {
        return false;
    }
    if (mInGameBreaker) {
        SetGameBreaker(false);
    } else if (mGameBreakerCharge > 0.0f) {
        SetGameBreaker(true);
    } else {
        return false;
    }
    return true;
}

bool LocalPlayer::CanRechargeNOS() const {
    return mInGameBreaker == 0;
}

void LocalPlayer::ResetGameBreaker(bool full) {
    mGameBreakerCharge = full ? 1.0f : 0.0f;
    SetGameBreaker(false);
}

float Tweak_GameBreakerRechargeTime = 25.0f;
float Tweak_GameBreakerRechargeSpeed = 100.0f;
bool Tweak_InfiniteRaceBreaker = false;
// static const float Tweak_GameBreakerMinimumSpeedMPH; // TODO
float Tweak_GameBreakerCollisionMass = 2.0f;

// TODO move
extern int bRumbleEnabled;

void LocalPlayer::DoGameBreaker(float dT, float dT_real) {
    if (!CanDoGameBreaker()) {
        SetGameBreaker(false);
        return;
    }
    ISimable *isimable = static_cast<Entity *>(this)->GetSimable();
    IVehicle *ivehicle;
    if (!isimable || !isimable->QueryInterface(&ivehicle)) {
        SetGameBreaker(false);
        return;
    }

    float speed_mph = MPS2MPH(ivehicle->GetSpeedometer());
    if (mInGameBreaker) {
        if (!Tweak_InfiniteRaceBreaker) {
            mGameBreakerCharge = mGameBreakerCharge - dT_real * 0.1f;
            mGameBreakerCharge = UMath::Max(mGameBreakerCharge, 0.0f);
        }
    } else {
        if (speed_mph > Tweak_GameBreakerRechargeSpeed) {
            mGameBreakerCharge = mGameBreakerCharge + dT / Tweak_GameBreakerRechargeTime;
            mGameBreakerCharge = UMath::Min(mGameBreakerCharge, 1.0f);
        }
    }
    if (mGameBreakerCharge <= 0.0f) {
        SetGameBreaker(false);
    }
}

// UNSOLVED
void LocalPlayer::UpdateNeighbourhood() {
    bVector3 v;
    bConvertFromBond(v, static_cast<IEntity *>(this)->GetPosition());
    TrackPathZone *zone = TheTrackPathManager.FindZone(reinterpret_cast<bVector2 *>(&v), TRACK_PATH_ZONE_NEIGHBOURHOOD, nullptr);
    unsigned int neighbourhood_hash = 0;
    if (zone) {
        neighbourhood_hash = zone->GetData(0);
    }
    if (neighbourhood_hash != mNeighbourhoodHash) {
        if (mHud) {
            IGenericMessage *igenericmessage;
            if (mHud->QueryInterface(&igenericmessage) && neighbourhood_hash != 0) {
                char *stringToUse = GetTranslatedString(neighbourhood_hash);
                // TODO hash
                igenericmessage->RequestGenericMessage(stringToUse, false, 0xa19bb14c, 0, 0, GenericMessage_Priority_5);
            }
        }
        mNeighbourhoodHash = neighbourhood_hash;
    }
}

bool LocalPlayer::CanDoFFB() const {
    PlayerSettings *settings = GetSettings();
    if (!settings || !settings->Rumble || !bRumbleEnabled) {
        return false;
    }
    Sim::IStateManager *state_manager = UTL::COM::QueryInterface<Sim::IStateManager>(IGameState::Get());
    if (!state_manager || state_manager->ShouldPauseInput()) {
        return false;
    }
    ISimable *myobject = static_cast<const IEntity *>(this)->GetSimable();
    IHumanAI *ai;
    IVehicle *vehicle;
    if (myobject && myobject->QueryInterface(&ai)) {
        if (ai->GetAiControl()) {
            return false;
        }
        if (myobject->QueryInterface(&vehicle)) {
            return vehicle->IsAnimating() == 0;
        }
    }
    return false;
}

void LocalPlayer::DoFFB() {
    if (!mFFB) {
        return;
    }
    if (!CanDoFFB()) {
        mFFB->PauseEffects();
        return;
    }
    if (mWheelDevice && mWheelDevice->IsConnected()) {
        mWheelDevice->UpdateForces(this);
        return;
    }

    ISimable *isimable = static_cast<IEntity *>(this)->GetSimable();

    IInput *iinput;
    IVehicle *ivehicle;
    ISuspension *isuspension;
    IEngine *iengine;
    ITransmission *itransmission;

    if (!isimable || !isimable->QueryInterface(&iinput) || !isimable->QueryInterface(&ivehicle) || !isimable->QueryInterface(&isuspension) ||
        !isimable->QueryInterface(&iengine) || !isimable->QueryInterface(&itransmission)) {
        return;
    }
    mFFB->ResumeEffects();
    mFFB->BeginUpdate();
    for (unsigned int i = 0; i < isuspension->GetNumWheels(); i++) {
        ISpikeable *ispikeable;
        bool blown = isimable->QueryInterface(&ispikeable) && ispikeable->GetTireDamage(i);

        // TODO hash
        const SimSurface &surface = isuspension->IsWheelOnGround(i) ? (blown ? SimSurface(0xd929e923) : isuspension->GetWheelRoadSurface(i))
                                                                    : (SimSurface(SimSurface::kNull));

        float slip = UMath::Abs(isuspension->GetWheelSlip(i));
        float skid = UMath::Abs(isuspension->GetWheelSkid(i));
        bool front = Physics::Wheels::IsFront(i);
        mFFB->UpdateTireSlip(front, surface, slip);
        mFFB->UpdateTireSkid(front, surface, skid);
        mFFB->UpdateRoadNoise(front, surface, ivehicle->GetAbsoluteSpeed());
    }

    float throttle = iinput->GetControls().fGas;
    float minrpm = iengine->GetMinRPM();
    float rpm = iengine->GetRPM();
    float range = iengine->GetRedline() - minrpm;
    float powerband = 0.0f;
    float overrev = 0.0f;
    if (range > 0.0f) {
        powerband = UMath::Clamp((rpm - minrpm) / range, 0.0f, 1.0f);
    }
    mFFB->UpdateRPM(powerband, overrev, throttle);
    mFFB->UpdateShiftPotential(itransmission->GetShiftPotential());

    if (iengine->HasNOS()) {
        mFFB->UpdateNOS(iengine->IsNOSEngaged(), iengine->GetNOSCapacity());
    }

    IEngineDamage *ienginedamage;
    isimable->QueryInterface(&ienginedamage);
    if (ienginedamage) {
        mFFB->UpdateEngineBlown(ienginedamage->IsBlown() || ienginedamage->IsSabotaged());
    } else {
        mFFB->UpdateShifting(itransmission->IsGearChanging() && (ivehicle->GetSpeed() > 0.1f));
    }

    mFFB->EndUpdate();
}

bool LocalPlayer::OnTask(HSIMTASK htask, float dT) {
    ProfileNode profile_node("TODO", 0);

    if (htask == mHudTask) {
        float dT_real = 0.0f;
        float sim_speed = Sim::GetSpeed();
        if (sim_speed > UMath::Epsilon) {
            dT_real = dT / sim_speed;
        }
        UpdateHud(dT);
        DoGameBreaker(dT, dT_real);
        DoFFB();
        return true;
    } else {
        Sim::Object::OnTask(htask, dT);
        return false;
    }
}

IFeedback *LocalPlayer::GetFFB() {
    return mFFB;
}

ISteeringWheel *LocalPlayer::GetSteeringDevice() {
    return mWheelDevice;
}

void LocalPlayer::SetControllerPort(int port) {
    if (port != mControllerPort) {
        mControllerPort = port;
        if (mFFB) {
            mFFB->ResetEffects();
            mFFB = nullptr;
        }
        mWheelDevice = nullptr;
        if (mControllerPort >= 0) {
            InputDevice *device = IOModule::GetIOModule().GetDevice(mControllerPort);
            if (device) {
                mFFB = UTL::COM::QueryInterface<IFeedback>(device->GetInterfaces());
                mWheelDevice = UTL::COM::QueryInterface<ISteeringWheel>(device->GetSecondaryDevice());
            }
        }
        if (mFFB) {
            mFFB->ResetEffects();
        }
    }
}

void LocalPlayer::OnCollision(const COLLISION_INFO &cinfo) {
    ISimable *bodyA = ISimable::FindInstance(cinfo.objA);
    if (!bodyA) {
        return;
    }
    ISimable *bodyB;
    if (cinfo.type == COLLISION_INFO::OBJECT) {
        bodyB = ISimable::FindInstance(cinfo.objB);
    }
    PlayerSettings *settings = GetSettings();
    if (mFFB && settings && settings->Rumble && bRumbleEnabled) {
        mFFB->ReportCollision(cinfo, UTL::COM::ComparePtr(bodyA, static_cast<IEntity *>(this)->GetSimable()));
    }
}

#if defined(EA_PORT_NATIVE)
// PC: 0x6F13F0. Feeds the HUD widgets (speedometer, tachometer, shift light,
// turbo/temp gauges, NOS, speedbreaker, pursuit and milestone boards, busted
// meter, race information, leaderboard, cost to state, wrong way, heat, radar)
// from the car, the pursuit and the race, then updates the HUD itself.
void LocalPlayer::UpdateHud(float dT) {
    if (mHud == nullptr) {
        return;
    }
    ISimable *isimable = static_cast<IEntity *>(this)->GetSimable();
    if (isimable == nullptr) {
        return;
    }
    IVehicle *ivehicle;
    IHumanAI *ihumanai;
    ISuspension *isuspension;
    IEngine *iengine;
    ITransmission *itransmission;
    if (!isimable->QueryInterface(&ivehicle) || !isimable->QueryInterface(&ihumanai) || !isimable->QueryInterface(&isuspension) ||
        !isimable->QueryInterface(&iengine)) {
        return;
    }
    IDragEngine *idragengine = nullptr;
    isimable->QueryInterface(&idragengine);
    IEngineDamage *ienginedamage = nullptr;
    isimable->QueryInterface(&ienginedamage);
    IInductable *iinductable = nullptr;
    isimable->QueryInterface(&iinductable);
    if (!isimable->QueryInterface(&itransmission)) {
        return;
    }

    IVehicleAI *ivehicleai = ivehicle->GetAIVehiclePtr();
    IPursuit *ipursuit = ivehicleai ? ivehicleai->GetPursuit() : nullptr;
    IPerpetrator *iperp = nullptr;
    ivehicle->QueryInterface(&iperp);
    bool inPursuit = ipursuit != nullptr && !ipursuit->IsPursuitBailed();

    ISpeedometer *ispeedometer;
    if (mHud->QueryInterface(&ispeedometer)) {
        ispeedometer->SetSpeed(ivehicle->GetSpeed());
    }

    ITachometer *itachometer;
    if (mHud->QueryInterface(&itachometer)) {
        bool is_staging = ivehicle->IsStaging();
        float rpm = iengine->GetRPM();
        ICarAudio *iaudible;
        if (!is_staging && ivehicle->GetDriverStyle() != STYLE_DRAG && isimable->QueryInterface(&iaudible)) {
            rpm = iaudible->GetRPM();
        }
        itachometer->SetRpm(rpm);
        itachometer->SetRevLimiter(iengine->GetRedline(), iengine->GetMaxRPM());
        itachometer->SetGear(itransmission->GetGear(), itransmission->GetShiftPotential(), true);
        itachometer->SetShifting(itransmission->IsGearChanging());
        itachometer->SetInPerfectLaunchRange(false);
        IRaceEngine *iraceengine;
        if (ivehicle->IsStaging() && iengine->QueryInterface(&iraceengine)) {
            float range = 0.0f;
            float peak_rpm = iraceengine->GetPerfectLaunchRange(range);
            if (range > 0.0f && peak_rpm > 0.0f && peak_rpm <= rpm && rpm <= peak_rpm + range) {
                itachometer->SetInPerfectLaunchRange(true);
            }
        }
    }

    IShiftUpdater *ishiftupdater;
    if (mHud->QueryInterface(&ishiftupdater)) {
        bool hasGoodEnoughTraction = !ivehicle->IsStaging();
        ishiftupdater->SetGear(itransmission->GetGear(), itransmission->GetShiftStatus(), itransmission->GetShiftPotential(),
                               hasGoodEnoughTraction);
        if (idragengine) {
            ishiftupdater->SetEngineTemp(idragengine->GetHeat());
        }
        if (ienginedamage) {
            ishiftupdater->SetEngineBlown(ienginedamage->IsBlown());
        }
    }

    if (iinductable) {
        mHud->SetHasTurbo(iinductable->InductionType() != 0);
        ITurbometer *iturbometer;
        if (mHud->QueryInterface(&iturbometer)) {
            iturbometer->SetInductionPsi(iinductable->GetInductionPSI());
        }
    }

    IEngineTempGauge *itempgauge;
    if (idragengine && mHud->QueryInterface(&itempgauge)) {
        itempgauge->SetEngineTemp(idragengine->GetHeat());
    }

    INos *inos;
    if (mHud->QueryInterface(&inos)) {
        float nos = iengine->GetNOSCapacity();
        // TODO(port): PC also zeroes it in online/LAN races unless the online
        // settings allow NOS (FEDatabase+0x142); online isn't supported.
        inos->SetNos(nos);
    }

    ISpeedBreakerMeter *ispeedbreaker;
    if (mHud->QueryInterface(&ispeedbreaker)) {
        ispeedbreaker->SetPursuitLevel(mGameBreakerCharge);
    }

    GRaceStatus &raceStatus = GRaceStatus::Get();
    IPursuitBoard *ipursuitboard;
    if ((raceStatus.GetPlayMode() == GRaceStatus::kPlayMode_Roaming || raceStatus.GetRaceParameters()->GetIsPursuitRace()) &&
        mHud->QueryInterface(&ipursuitboard)) {
        mHud->SetInPursuit(inPursuit);
        ipursuitboard->SetInPursuit(inPursuit);
        if (ipursuit) {
            ipursuitboard->SetIsInView(ipursuit->IsPerpInSight());
            ipursuitboard->SetPursuitDuration(ipursuit->GetPursuitDuration());
            ipursuitboard->SetCooldownTimeRemaining(ipursuit->GetCoolDownTimeRemaining());
            ipursuitboard->SetCooldownTimeRequired(ipursuit->GetCoolDownTimeRequired());
            ipursuitboard->SetNumCopsInPursuit(ipursuit->GetNumCopsRemainingInWave());
            ipursuitboard->SetNumCopsDestroyed(ipursuit->GetNumCopsDestroyed(), ipursuit->GetMostRecentCopDestroyedType(),
                                               ipursuit->GetCopDestroyedBonusMultiplier(), ipursuit->GetMostRecentCopDestroyedRepPoints());
            ipursuitboard->SetNumCopsDamaged(ipursuit->GetNumCopsDamaged());
            ipursuitboard->SetTotalNumCopsInvolved(ipursuit->GetTotalNumCopsInvolved());
            ipursuitboard->SetHeliInvolvedInPursuit(ipursuit->IsHeliInPursuit());
            ipursuitboard->SetTimeUntilBusted(ipursuit->TimeUntilBusted(), ipursuit->IsPerpBusted());
            ipursuitboard->SetTimeUntilBackup(ipursuit->GetTimeToBackupSpawned());
            if (iperp) {
                float timeUntilHidden = 0.0f;
                ipursuitboard->SetIsHiding(iperp->IsPartiallyHidden(timeUntilHidden));
                ipursuitboard->SetTimeUntilHidden(timeUntilHidden);
                ipursuitboard->SetPursuitRep(iperp->GetPendingRepPointsFromCopDestruction() + iperp->GetPendingRepPointsNormal());
            }
        }
    }

    IMilestoneBoard *imilestoneboard;
    if (mHud->QueryInterface(&imilestoneboard)) {
        GRaceParameters *raceParams = GRaceStatus::Exists() ? GRaceStatus::Get().GetRaceParameters() : nullptr;
        if (raceParams && raceParams->GetIsPursuitRace() && !FEDatabase->IsFinalEpicChase()) {
            // challenge series pursuit: a single milestone, the challenge goal
            imilestoneboard->SetNumberOfMilestones(1);
            imilestoneboard->SetChallengeSeries(true);
            imilestoneboard->SetMilestoneIconHash(0, FEDatabase->GetMilestoneIconHash(raceParams->GetChallengeType(), true));
            imilestoneboard->SetMilestoneType(0, raceParams->GetChallengeType());
            imilestoneboard->SetMilestoneGoal(0, raceParams->GetChallengeGoal());
            imilestoneboard->SetMilestoneCurrValue(0, GManager::Get().GetValue(raceParams->GetChallengeType()));
            imilestoneboard->SetMilestoneHeaderHash(0, raceParams->GetLocalizationTag());
            float currVal = GManager::Get().GetValue(raceParams->GetChallengeType());
            imilestoneboard->SetMilestoneComplete(0, raceParams->GetChallengeGoal() <= currVal);
        } else if (GRaceStatus::Exists() && inPursuit) {
            unsigned int currBin = FEDatabase->GetCareerSettings()->GetCurrentBin();
            uint8 index = 0;
            for (GMilestone *currMilestone = GManager::Get().GetFirstMilestone(false, currBin); currMilestone;
                 currMilestone = GManager::Get().GetNextMilestone(currMilestone, false, currBin), index++) {
                imilestoneboard->SetMilestoneIconHash(index, FEDatabase->GetMilestoneIconHash(currMilestone->GetTypeKey(), true));
                imilestoneboard->SetMilestoneType(index, currMilestone->GetTypeKey());
                imilestoneboard->SetMilestoneGoal(index, currMilestone->GetRequiredValue());
                imilestoneboard->SetMilestoneCurrValue(index, currMilestone->GetCurrentValue());
                imilestoneboard->SetMilestoneHeaderHash(index, currMilestone->GetLocalizationTag());
                bool isDone = currMilestone->GetIsDonePendingEscape() || currMilestone->GetIsAwarded();
                imilestoneboard->SetMilestoneComplete(index, isDone);
            }
            imilestoneboard->SetNumberOfMilestones(index);
            imilestoneboard->SetChallengeSeries(false);
        }
        imilestoneboard->SetInPursuit(inPursuit);
    }

    IBustedMeter *ibustedmeter;
    if (mHud->QueryInterface(&ibustedmeter)) {
        ibustedmeter->SetInPursuit(inPursuit);
        if (ipursuit) {
            ibustedmeter->SetTimeUntilBusted(ipursuit->TimeUntilBusted());
            ibustedmeter->SetIsBusted(ipursuit->IsPerpBusted());
            if (iperp) {
                float timeUntilHidden = 0.0f;
                ibustedmeter->SetIsHiding(iperp->IsPartiallyHidden(timeUntilHidden));
            }
        }
    }

    ITimeExtension *itimeextension;
    if (GRaceStatus::IsTollboothRace() && mHud->QueryInterface(&itimeextension)) {
        itimeextension->SetPlayerLapTime(GRaceStatus::Get().GetRaceTimeRemaining());
    }

    IRaceInformation *iraceinfo;
    if (raceStatus.GetPlayMode() == GRaceStatus::kPlayMode_Racing && mHud->QueryInterface(&iraceinfo)) {
        iraceinfo->SetNumRacers(raceStatus.GetRacerCount());
        iraceinfo->SetNumLaps(raceStatus.GetRaceParameters()->GetNumLaps());
        GRacerInfo *playerInfo = raceStatus.GetRacerInfo(static_cast<IEntity *>(this)->GetSimable());
        if (playerInfo) {
            iraceinfo->SetPlayerPosition(playerInfo->GetRanking());
            iraceinfo->SetPlayerLapNumber(playerInfo->GetLapsCompleted() + 1);
            GRace::Type raceType = raceStatus.GetRaceType();
            if (raceType >= GRace::kRaceType_Checkpoint && raceType <= GRace::kRaceType_CashGrab) {
                float remaining = raceStatus.GetRaceTimeRemaining();
                iraceinfo->SetPlayerPercentComplete((1.0f - remaining / raceStatus.GetRaceParameters()->GetTimeLimit()) * 100.0f);
            } else {
                iraceinfo->SetPlayerPercentComplete(playerInfo->GetHudPctRaceComplete());
            }
            if (raceStatus.GetIsTimeLimited()) {
                iraceinfo->SetPlayerLapTime(raceStatus.GetRaceTimeRemaining());
                iraceinfo->SetSuddenDeathMode(raceStatus.GetIsSuddenDeath());
            } else {
                iraceinfo->SetPlayerLapTime(playerInfo->GetRaceTime());
                iraceinfo->SetSuddenDeathMode(false);
            }
            if (GRaceStatus::IsTollboothRace()) {
                iraceinfo->SetPlayerTollboothsCrossed(playerInfo->GetTollboothsCrossed() + (playerInfo->IsFinishedRacing() ? 1 : 0));
                iraceinfo->SetNumTollbooths(raceStatus.GetNumRaceTollbooths() + 1);
            }
        }
    }

    ILeaderBoard *ileaderboard;
    if (raceStatus.GetPlayMode() == GRaceStatus::kPlayMode_Racing && mHud->QueryInterface(&ileaderboard)) {
        ileaderboard->SetNumRacers(raceStatus.GetRacerCount());
        ileaderboard->SetNumLaps(raceStatus.GetRaceParameters()->GetNumLaps());
        GRacerInfo *playerInfo = raceStatus.GetRacerInfo(static_cast<IEntity *>(this)->GetSimable());
        for (int i = 0; i < 4; i++) {
            int ranking = i + 1;
            int racerIndex = -1;
            for (int onRacer = 0; onRacer < raceStatus.GetRacerCount(); onRacer++) {
                if (raceStatus.GetRacerInfo(onRacer).GetRanking() == ranking) {
                    racerIndex = onRacer;
                    break;
                }
            }
            ileaderboard->SetRacerHasHeadset(i, false);
            if (racerIndex < 0) {
                continue;
            }
            GRacerInfo &racer_info = raceStatus.GetRacerInfo(racerIndex);
            // (online races also show the racer's headset here -- online isn't supported)
            if (&racer_info == playerInfo) {
                ileaderboard->SetPlayerIndex(i);
            }
            ileaderboard->SetRacerName(i, racer_info.GetName());
            ileaderboard->SetRacerNum(i, racer_info.GetRanking());
            ileaderboard->SetRacerTotalPoints(i, racer_info.GetPointTotal());
            ileaderboard->SetRacerPercentComplete(i, racer_info.GetHudPctRaceComplete(), racer_info.GetRaceTime(), static_cast<IPlayer *>(this));
            ileaderboard->SetRacerNumLapsCompleted(i, racer_info.GetLapsCompleted(), racer_info.GetLapTime(), static_cast<IPlayer *>(this));
            ileaderboard->SetRacerIsBusted(i, racer_info.GetIsBusted());
            ileaderboard->SetRacerIsKoed(i, racer_info.GetIsKnockedOut());
        }
    }

    ICostToState *icosttostate;
    if (mHud->QueryInterface(&icosttostate)) {
        if (inPursuit) {
            icosttostate->SetInPursuit(true);
            Sim::IActivity *pursuitActivity = UTL::COM::QueryInterface<Sim::IActivity>(ipursuit);
            if (pursuitActivity && pursuitActivity->GetInstanceHandle() != mLastPursuit) {
                icosttostate->SetCostToState(0);
                mLastPursuit = pursuitActivity->GetInstanceHandle();
            }
            icosttostate->SetCostToState(ipursuit->CalcTotalCostToState());
        } else {
            icosttostate->SetInPursuit(false);
        }
    }

    IWrongWay *iwrongway;
    if (mHud->QueryInterface(&iwrongway)) {
        iwrongway->SetWrongWay(ihumanai->IsFacingWrongWay());
    }

    IHeatMeter *iheatmeter;
    if (mHud->QueryInterface(&iheatmeter) && iperp) {
        float vehicleHeat = iperp->GetHeat();
        iheatmeter->SetVehicleHeat(vehicleHeat);
        GManager::Get().TrackValue("heat_meter", vehicleHeat);
    }

    bool isCoolingDown = ipursuit && ipursuit->GetPursuitStatus() == PS_COOL_DOWN;
    DoRadar(inPursuit, isCoolingDown);

    if (mHud) {
        mHud->Update(this, dT);
    }

    if (inPursuit && ipursuit->GetMinDistanceToTarget() < 25.0f) {
        float speed = ivehicle->GetSpeed();
        // PC pairs the limits this way: TWEAK_SpeedingLimit (67) feeds racing,
        // TWEAK_RacingLimit (80.5) feeds speeding.
        bool reckless = speed > 89.4f;
        bool speeding = speed > 80.5f;
        bool racing = speed > 67.0f;
        GInfractionManager::Get().ReportRacing(racing);
        GInfractionManager::Get().ReportRecklessDriving(reckless);
        GInfractionManager::Get().ReportSpeeding(speeding);
    }

    if (raceStatus.GetPlayMode() == GRaceStatus::kPlayMode_Roaming) {
        UpdateNeighbourhood();
    }

    PostRacePursuitScreen::GetPursuitData().PopulateData(ipursuit, iperp, -1);
}
#endif

#if defined(EA_PORT_NATIVE)
static const float kRadarAngle = 0.7853982f; // PC: 0x988E74 (pi/4, set by a static initialiser)
static const float kRadarRange = 300.0f;     // PC: 0x8B01C4

// PC: 0x6EDE30. Points the radar detector at the nearest cop or speed trap
// camera within 300 m in front of the car.
void LocalPlayer::DoRadar(bool inPursuit, bool isCoolingDown) {
    if (!GRaceStatus::Exists()) {
        return;
    }
    IRadarDetector *iradar;
    if (!mHud->QueryInterface(&iradar)) {
        return;
    }
    iradar->SetInPursuit(inPursuit);
    iradar->SetIsCoolingDown(isCoolingDown);

    ISimable *isimable = static_cast<IEntity *>(this)->GetSimable();
    IVehicle *ivehicle;
    IRigidBody *irigidbody;
    if (isimable == nullptr || !isimable->QueryInterface(&ivehicle) || !ivehicle->QueryInterface(&irigidbody)) {
        iradar->SetTarget(IRadarDetector::RADAR_TARGET_NONE, 0.0f, 0.0f);
        return;
    }
    IPerpetrator *iperp;
    if (!ivehicle->QueryInterface(&iperp)) {
        return;
    }

    float angleRange = cosf(kRadarAngle);
    UMath::Vector3 playerDir;
    irigidbody->GetForwardVector(playerDir);
    UMath::Normalize(playerDir);
    UMath::Vector3 playerPos = irigidbody->GetPosition();

    IRadarDetector::RadarTarget targetType = IRadarDetector::RADAR_TARGET_NONE;
    float targetRange = 310.0f;
    float targetDot = 1.0f;
    UMath::Vector3 toCurrentTarget = UMath::Vector3::kZero;

    // a speed trap camera (free roam: the active ones; speed trap races: the race's traps)
    GRaceStatus &raceStatus = GRaceStatus::Get();
    if (raceStatus.GetPlayMode() == GRaceStatus::kPlayMode_Roaming) {
        unsigned int count = GManager::Get().GetNumSpeedTraps();
        for (unsigned int i = 0; i < count; i++) {
            GSpeedTrap *speedTrap = GManager::Get().GetSpeedTrap(i);
            if (speedTrap->GetIsActive()) {
                GTrigger *trap = speedTrap->GetTrapTrigger();
                if (trap && trap->IsEnabled()) {
                    UMath::Vector3 targetPos;
                    trap->GetPosition(targetPos);
                    UMath::Vector3 toTarget = targetPos - playerPos;
                    float m = UMath::Normalize(toTarget);
                    float dot = UMath::Dot(toTarget, playerDir);
                    if (m <= kRadarRange && dot >= angleRange && m < targetRange) {
                        targetType = IRadarDetector::RADAR_TARGET_CAMERA;
                        targetRange = m;
                        targetDot = dot;
                        toCurrentTarget = toTarget;
                    }
                }
            }
        }
    } else if (raceStatus.GetRaceType() == GRace::kRaceType_SpeedTrap && iperp->GetRacerInfo() != nullptr) {
        for (int i = 0; i < raceStatus.GetNumRaceSpeedTraps(); i++) {
            GTrigger *trap = raceStatus.GetRaceSpeedTrap(i);
            if (trap && trap->IsEnabled()) {
                UMath::Vector3 targetPos;
                trap->GetPosition(targetPos);
                UMath::Vector3 toTarget = targetPos - playerPos;
                float m = UMath::Normalize(toTarget);
                float dot = UMath::Dot(toTarget, playerDir);
                if (m <= kRadarRange && dot >= angleRange && m < targetRange) {
                    targetType = IRadarDetector::RADAR_TARGET_CAMERA;
                    targetRange = m;
                    targetDot = dot;
                    toCurrentTarget = toTarget;
                }
            }
        }
    }

    // a cop (not the helicopter)
    const IVehicle::List &cops = IVehicle::GetList(VEHICLE_AICOPS);
    for (IVehicle::List::const_iterator iter = cops.begin(); iter != cops.end(); ++iter) {
        IVehicle *iv = *iter;
        if (iv->GetVehicleClass() == VehicleClass::CHOPPER || !iv->IsActive() || iv->IsAnimating() || iv->IsDestroyed()) {
            continue;
        }
        UMath::Vector3 toTarget;
        UMath::Sub(iv->GetPosition(), playerPos, toTarget);
        float m = UMath::Normalize(toTarget);
        if (m > kRadarRange) {
            continue;
        }
        float dot = UMath::Dot(toTarget, playerDir);
        if (dot >= angleRange && m < targetRange) {
            targetType = IRadarDetector::RADAR_TARGET_COP;
            targetRange = m;
            targetDot = dot;
            toCurrentTarget = toTarget;
        }
    }

    if (targetType == IRadarDetector::RADAR_TARGET_NONE) {
        iradar->SetTarget(IRadarDetector::RADAR_TARGET_NONE, 0.0f, 0.0f);
        return;
    }
    float targetAngle = acosf(UMath::Clamp(targetDot, -1.0f, 1.0f));
    UMath::Vector3 right;
    irigidbody->GetRightVector(right);
    if (UMath::Dot(right, toCurrentTarget) < 0.0f) {
        targetAngle = -targetAngle;
    }
    iradar->SetTarget(targetType, UMath::Clamp(targetRange * (1.0f / kRadarRange), 0.0f, 1.0f), targetAngle);
}
#endif
