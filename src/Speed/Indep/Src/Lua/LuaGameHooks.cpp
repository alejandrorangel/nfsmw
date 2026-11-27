// The game functions the race/activity scripts call (bound by
// LuaBindery::BindToGameCode, PC 0x61E750). Each is PC: <address>.
#include "Speed/Indep/Src/Lua/LuaGameHooks.h"

#include "Speed/Indep/Src/AI/AITarget.h"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Gameplay/GActivity.h"
#include "Speed/Indep/Src/Gameplay/GCharacter.h"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GMarker.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Gameplay/GTrigger.h"
#include "Speed/Indep/Src/Generated/Events/EFadeScreenOn.hpp"
#include "Speed/Indep/Src/Generated/Events/EResetProps.hpp"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Generated/Messages/MNISComplete.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyKnockedOut.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyMovieFinished.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyRacePlacement.h"
#include "Speed/Indep/Src/Generated/Messages/MSetCopsEnabled.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/ICopMgr.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/ITrafficMgr.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/IAI.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRigidBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Misc/Config.h"
#include "Speed/Indep/Src/Physics/PhysicsInfo.hpp"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/pvehicle.h"
#include "Speed/Indep/Src/Sim/Simulation.h"
#include "Speed/Indep/Src/World/WRoadNetwork.h"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include "Speed/Indep/Src/Lua/source/lauxlib.h"

#include <cstdio>
#include <cstdlib>
#include <list>
#include <vector>

// Defined in World/rain.cpp.
void SetOverRideRainIntensity(float rov);

namespace {

#if defined(EA_PORT_NATIVE)
// Reports the first call of a script function that isn't ported yet.
#define LUA_HOOK_TODO(name)                                                                                                                          \
    do {                                                                                                                                             \
        static bool sReported = false;                                                                                                               \
        if (!sReported) {                                                                                                                            \
            sReported = true;                                                                                                                        \
            std::fprintf(stderr, "[lua] TODO(port): %s\n", name);                                                                                    \
        }                                                                                                                                            \
    } while (0)
#else
#define LUA_HOOK_TODO(name)
#endif

IPlayer *FirstPlayer() {
    const IPlayer::List &players = IPlayer::GetList(PLAYER_ALL);
    return players.empty() ? nullptr : *players.begin();
}

IPlayer *LastPlayer() {
    const IPlayer::List &players = IPlayer::GetList(PLAYER_ALL);
    return players.empty() ? nullptr : *(players.end() - 1);
}

GRaceStatus &RaceStatus() {
    return GRaceStatus::Get();
}

// The 1-based racer index scripts use.
GRacerInfo &RacerInfo(int racerIndex) {
    return RaceStatus().GetRacerInfo(racerIndex - 1);
}

GActivity *AsActivity(GRuntimeInstance *instance) {
    return instance != nullptr && instance->GetType() == kGameplayObjType_Activity ? static_cast<GActivity *>(instance) : nullptr;
}

GTrigger *AsTrigger(GRuntimeInstance *instance) {
    return instance != nullptr && instance->GetType() == kGameplayObjType_Trigger ? static_cast<GTrigger *>(instance) : nullptr;
}

IHumanAI *PlayerHumanAI(int playerIndex) {
    IPlayer *player = playerIndex == 0 ? FirstPlayer() : LastPlayer();
    if (player == nullptr) {
        return nullptr;
    }
    ISimable *isim = player->GetSimable();
    IHumanAI *humanAI;
    if (isim == nullptr || !isim->QueryInterface(&humanAI)) {
        return nullptr;
    }
    return humanAI;
}

} // namespace

// ---------------------------------------------------------------------------
// Table / Activity / Audio / Bin / Math / Camera / NIS / Movie / Debug / Demo
// ---------------------------------------------------------------------------

// PC: 0x618AC0. Shuffles the array part of a table.
int Table_Randomize(lua_State *luaState) {
    if (!lua_istable(luaState, 1)) {
        return 0;
    }
    int count = luaL_getn(luaState, 1);
    for (int pass = count; pass > 0; pass--) {
        for (int i = 1; i < count; i++) {
            int j = static_cast<int>(bRandom(count)) + 1;
            if (j == i) {
                continue;
            }
            lua_rawgeti(luaState, 1, i);
            lua_rawgeti(luaState, 1, j);
            lua_rawseti(luaState, 1, i);
            lua_rawseti(luaState, 1, j);
        }
    }
    return 0;
}

// PC: 0x6048A0.
void Activity_Run(GRuntimeInstance *instance) {
    GActivity *activity = AsActivity(instance);
    if (activity == nullptr) {
        return;
    }
    GRaceParameters *parms = GRaceDatabase::Get().GetRaceFromActivity(activity);
    if (parms != nullptr) {
        parms->BlockUntilLoaded();
    }
    activity->Reset();
    activity->Run();
}

// PC: 0x6048D0.
void Activity_Suspend(GRuntimeInstance *instance) {
    GActivity *activity = AsActivity(instance);
    if (activity != nullptr) {
        activity->Suspend();
    }
}

// PC: 0x6048E0. Sets an audio game state (and the sound control state for
// some of them).
// TODO(port): the flag table (0x8F5D78) and ESndGameState aren't ported.
void Audio_SetFlag(const char *flag, bool set) {
    LUA_HOOK_TODO("Audio.SetFlag");
}

// PC: 0x604A00.
bool Audio_IsCopSpeechPlaying() {
    return false;
}

// PC: 0x604B50. TODO(port): the minimap engage marker isn't ported.
void MiniMap_AddEngagedRace(GRuntimeInstance *activity) {
    LUA_HOOK_TODO("MiniMap.AddEngagedRace");
}

// PC: 0x604A10.
int Bin_GetNumChallengesPassed(int bin) {
    GRaceBin *raceBin = GRaceDatabase::Get().GetBinNumber(bin);
    return raceBin != nullptr ? raceBin->GetStats().mChallengesCompleted : 0;
}

// PC: 0x604A30.
int Bin_GetNumRacesWon(int bin) {
    GRaceBin *raceBin = GRaceDatabase::Get().GetBinNumber(bin);
    return raceBin != nullptr ? raceBin->GetStats().mRacesWon : 0;
}

// PC: 0x604CB0.
int Math_RandomInt(int range) {
    return static_cast<int>(bRandom(range)) + 1;
}

// PC: 0x604A50. TODO(port): ECinematicMoment (and the MGamePlayMoment sent for
// Cinematics/Challenge) aren't ported.
void Camera_SetGenericCamera(const char *name, const char *sub) {
    LUA_HOOK_TODO("Camera.SetGenericCamera");
}

// PC: 0x604B80. With nothing to play (or NISes disabled) the script is told
// it's done straight away.
// TODO(port): playing an NIS (the EPlayRaceNIS-like event 0x640F00) isn't
// ported, so every NIS completes immediately.
void NIS_Play(GRuntimeInstance *location, const char *nisName, const char *type, int flags, const char *cameraTrack, const char *shell) {
    MNISComplete(nisName).Post(UCrc32(UCRC32_Gameplay));
}

// PC: 0x604C60.
void Movie_PlayHackE3FMV() {}

// PC: 0x604C70. Compiled out on PC.
void Debug_Print(const char *text) {
#if defined(EA_PORT_NATIVE)
    static int sTrace = -1;
    if (sTrace < 0) {
        const char *env = std::getenv("NFSMW_TRACE_LUA");
        sTrace = env != nullptr && env[0] != '\0' && env[0] != '0';
    }
    if (sTrace && text != nullptr) {
        std::fprintf(stderr, "[lua] Debug.Print: %s\n", text);
    }
#endif
}

// PC: 0x604C80.
void Debug_PrintInstance(GRuntimeInstance *instance) {}

// PC: 0x6121C0. Debug only.
void Debug_ShowScreenMessage(const char *text, float time) {}

// PC: 0x605410.
void Debug_Assert(bool condition, const char *text) {}

// PC: 0x604C90 / 0x604CA0 (demo builds only).
void Demo_SetRaceCompleteForFE(GRuntimeInstance *activity) {}

void Demo_StorePursuitRepForFE(GRuntimeInstance *activity) {}

// PC: 0x611320. TODO(port): IGenericMessage on the player's HUD.
void HUD_ShowMessage(const char *text) {
    LUA_HOOK_TODO("HUD.ShowMessage");
}

// PC: 0x6113E0. TODO(port): EShowTimeExtension.
void HUD_ShowTimeExtension(float seconds) {
    LUA_HOOK_TODO("HUD.ShowTimeExtension");
}

// PC: 0x604CC0.
bool Platform_IsNextGen() {
    return true;
}

// ---------------------------------------------------------------------------
// Game: racers
// ---------------------------------------------------------------------------

// PC: 0x60AB60. TODO(port): engine sabotage (IEngineDamage) isn't ported.
void Game_SabotageEngine(ISimable *racer, float time) {
    LUA_HOOK_TODO("Game.SabotageEngine");
}

// PC: 0x60AB30. TODO(port): IEngineDamage::Sabotage.
void Game_BlowEngine(ISimable *racer) {
    LUA_HOOK_TODO("Game.BlowEngine");
}

// PC: 0x611540.
void Game_DetachCameraFromRacer(ISimable *racer) {
    if (racer == nullptr) {
        return;
    }
    RacerInfo(Game_GetRacerIndex(racer)).DetachCamera();
}

// PC: 0x60E200.
void Game_ChallengeComplete(ISimable *racer) {
    GRacerInfo *info = RaceStatus().GetRacerInfo(racer);
    if (info != nullptr) {
        info->ChallengeComplete();
    }
}

// PC: 0x612420.
void Game_ClearAIControl(int playerIndex) {
    IHumanAI *humanAI = PlayerHumanAI(playerIndex);
    if (humanAI != nullptr && humanAI->GetAiControl()) {
        humanAI->SetAiControl(false);
    }
}

// PC: 0x6123B0.
void Game_ForceAIControl(int playerIndex) {
    IHumanAI *humanAI = PlayerHumanAI(playerIndex);
    if (humanAI != nullptr && !humanAI->GetAiControl()) {
        humanAI->SetAiControl(true);
    }
}

// PC: 0x60A5B0. A player vehicle becomes a racer.
static void Game_AddPlayer(IVehicle *ivehicle) {
    ISimable *isim = ivehicle->GetSimable();
    if (isim == nullptr || isim->GetPlayer() == nullptr) {
        return;
    }
    GRacerInfo &info = RaceStatus().AddSimablePlayer(isim);
    IPerpetrator *perp;
    if (isim->QueryInterface(&perp)) {
        perp->SetRacerInfo(&info);
    }
    // TODO(port): online players take their name from IOnlinePlayer.
}

typedef std::list<unsigned int> CandidateList;

// PC: 0x61D8E0. Up to maxCount random RandomOpponent cars whose stock
// performance is at most 0.2 better than the target (PC: the stock car table's
// range query, 0x684610). Returns whether any were found.
static bool Game_FindPerformanceCandidates(CandidateList &candidates, unsigned int maxCount, const Physics::Info::Performance &target) {
    Physics::Info::Performance high = target;
    high.TopSpeed = UMath::Clamp(high.TopSpeed + 0.2f, 0.0f, 1.0f);
    high.Handling = UMath::Clamp(high.Handling + 0.2f, 0.0f, 1.0f);
    high.Acceleration = UMath::Clamp(high.Acceleration + 0.2f, 0.0f, 1.0f);
    std::vector<unsigned int> shuffle;
    const Attrib::Class *aclass = Attrib::Database::Get().GetClass(Attrib::Gen::pvehicle::ClassKey());
    for (Attrib::Key key = aclass->GetFirstCollection(); key != 0; key = aclass->GetNextCollection(key)) {
        Attrib::Gen::pvehicle pvehicle(key, 0, nullptr);
        Physics::Info::Performance stock;
        if (pvehicle.IsDynamic() || !Physics::Info::GetStockPerformance(pvehicle, stock)) {
            continue;
        }
        if (stock.TopSpeed <= high.TopSpeed && stock.Acceleration <= high.Acceleration && stock.Handling <= high.Handling && pvehicle.RandomOpponent()) {
            shuffle.push_back(key);
        }
    }
    unsigned int count = static_cast<unsigned int>(shuffle.size());
    for (unsigned int i = 0; i < count; i++) {
        unsigned int j = bRandom(static_cast<int>(count));
        std::swap(shuffle[i], shuffle[j]);
    }
    for (unsigned int i = 0; i < count && i < maxCount; i++) {
        candidates.push_back(shuffle[i]);
    }
    return !candidates.empty();
}

// PC: 0x61DBE0. Adds the players and the race's opponents as racers, then
// lines the opponents up on the road behind the start marker (unless they
// have a forced start position) and creates their cars.
void Game_InitRacers(GRuntimeInstance *startMarker) {
    GRaceStatus &raceStatus = RaceStatus();
    raceStatus.ClearRacers();
    raceStatus.SyncronizeAdaptiveBonus();
    GManager::Get().ClearStockCars();
    WRoadNetwork::Get().ResetShortcuts();
    if (ICopMgr::Exists()) {
        ICopMgr::Get()->ResetCopsForRestart(true);
    }
    if (ITrafficMgr::Exists()) {
        ITrafficMgr::Get()->FlushAllTraffic(true);
    }
    const IVehicle::List &players = IVehicle::GetList(VEHICLE_PLAYERS);
    for (IVehicle::List::const_iterator iter = players.begin(); iter != players.end(); ++iter) {
        Game_AddPlayer(*iter);
    }

    GRaceParameters *parms = raceStatus.GetRaceParameters();
    if (parms != nullptr && Sim::GetUserMode() == Sim::USER_SINGLE && parms->GetRaceType() != GRace::kRaceType_Drag) {
        unsigned int numOpponents = static_cast<unsigned int>(1 - raceStatus.GetRacerCount() + parms->GetNumOpponents());
        for (unsigned int i = 0; i < numOpponents; i++) {
            raceStatus.AddRacer(parms->GetOpponentChar(i));
        }
    }

    GMarker *marker = startMarker != nullptr && startMarker->GetType() == kGameplayObjType_Marker ? static_cast<GMarker *>(startMarker) : nullptr;
    UMath::Vector3 startPos = UMath::Vector3::kZero;
    UMath::Vector3 backDir = UMath::Vector3::kZero;
    if (marker != nullptr) {
        startPos = marker->GetPosition();
        backDir.x = -marker->GetDirection().x;
        backDir.y = -marker->GetDirection().y;
        backDir.z = -marker->GetDirection().z;
    } else if (startMarker != nullptr) {
        startMarker->GetPosition(startPos);
        startMarker->GetDirection(backDir);
        backDir.x = -backDir.x;
        backDir.y = -backDir.y;
        backDir.z = -backDir.z;
    }
    WRoadNav nav;
    nav.SetNavType(WRoadNav::kTypeDirection);
    nav.InitAtPoint(startPos, backDir, false, 1.0f);

    raceStatus.LockVehicleCache(true);
    // Opponents without a car of their own get one of a few random cars that
    // can match the players (PC: the best player performance, starting from
    // the career's MinimumAIPerformance).
    float minimumPerformance = 0.0f;
    if (raceStatus.GetRaceContext() == GRace::kRaceContext_Career) {
        for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
            GCharacter *character = raceStatus.GetRacerInfo(i).GetGameCharacter();
            if (character != nullptr && character->MinimumAIPerformance() > minimumPerformance) {
                minimumPerformance = character->MinimumAIPerformance();
            }
        }
    }
    Physics::Info::Performance playerPerformance(minimumPerformance, minimumPerformance, minimumPerformance);
    for (IVehicle::List::const_iterator iter = players.begin(); iter != players.end(); ++iter) {
        Physics::Info::Performance performance;
        if ((*iter)->GetPerformance(performance)) {
            playerPerformance.Maximize(performance);
        }
    }
    CandidateList candidates;
    Game_FindPerformanceCandidates(candidates, 3, playerPerformance);
    CandidateList::iterator nextCandidate = candidates.begin();
    float sideOffset = 1.5f;
#if defined(EA_PORT_NATIVE)
    int gridSlot = 0;
#endif
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        GRacerInfo &info = raceStatus.GetRacerInfo(i);
        GCharacter *character = info.GetGameCharacter();
        if (character == nullptr) {
            continue;
        }
        UMath::Vector3 pos;
        UMath::Vector3 dir;
        Attrib::Key forcedStart = character->ForceStartPosition().mCollectionKey;
        if (forcedStart == 0) {
            nav.IncNavPosition(7.0f, backDir, 0.0f);
#if defined(EA_PORT_NATIVE)
            if (std::getenv("NFSMW_TRACE_LUA") != nullptr) {
                std::fprintf(stderr, "[lua]   grid: start (%.1f %.1f %.1f) back (%.2f %.2f %.2f) nav %s (%.1f %.1f %.1f)\n", startPos.x, startPos.y,
                             startPos.z, backDir.x, backDir.y, backDir.z, nav.IsValid() ? "valid" : "INVALID", nav.GetPosition().x,
                             nav.GetPosition().y, nav.GetPosition().z);
            }
#endif
            UMath::Vector3 navPos = nav.GetPosition();
            UMath::Vector3 navFwd = nav.GetForwardVector();
#if defined(EA_PORT_NATIVE)
            // TODO(port): WRoadNav::IncNavPosition sometimes walks off into
            // garbage natively (a valid nav at ~1e28); line the car up
            // straight behind the start instead of dropping it there.
            gridSlot++;
            UMath::Vector3 fromStart = {navPos.x - startPos.x, navPos.y - startPos.y, navPos.z - startPos.z};
            if (!nav.IsValid() || !(UMath::Length(fromStart) < 500.0f)) {
                navPos.x = startPos.x + backDir.x * 7.0f * gridSlot;
                navPos.y = startPos.y + backDir.y * 7.0f * gridSlot;
                navPos.z = startPos.z + backDir.z * 7.0f * gridSlot;
                navFwd = backDir;
            }
#endif
            dir.x = -navFwd.x;
            dir.y = -navFwd.y;
            dir.z = -navFwd.z;
            UMath::Vector3 up = {0.0f, 1.0f, 0.0f};
            UMath::Vector3 right;
            UMath::Cross(up, dir, right);
            pos.x = navPos.x + right.x * sideOffset;
            pos.y = navPos.y + right.y * sideOffset;
            pos.z = navPos.z + right.z * sideOffset;
        } else {
            Attrib::Gen::gameplay forced(forcedStart, 0, nullptr);
            const UMath::Vector3 &p = forced.Position();
            pos.x = -p.y;
            pos.y = p.z;
            pos.z = p.x;
            UMath::Matrix4 rotMat = UMath::Matrix4::kIdentity;
            UMath::MultYRot(rotMat, forced.Rotation() * (-1.0f / 360.0f), rotMat);
            UMath::Vector3 fwd = {0.0f, 0.0f, 1.0f};
            UMath::Rotate(fwd, rotMat, dir);
        }
        unsigned int defaultKey = 0;
        if (!candidates.empty()) {
            if (nextCandidate == candidates.end()) {
                nextCandidate = candidates.begin();
            }
            defaultKey = *nextCandidate;
            ++nextCandidate;
        }
        IVehicle *ivehicle = info.CreateVehicle(defaultKey);
        if (ivehicle != nullptr) {
            ivehicle->SetVehicleOnGround(pos, dir);
            IPerpetrator *perp;
            if (ivehicle->QueryInterface(&perp)) {
                perp->SetRacerInfo(&info);
            }
        }
        sideOffset = -sideOffset;
    }
    raceStatus.LockVehicleCache(false);
#if defined(EA_PORT_NATIVE)
    if (std::getenv("NFSMW_TRACE_LUA") != nullptr) {
        std::fprintf(stderr, "[lua] InitRacers: %d racers (race %s, %d opponents)\n", raceStatus.GetRacerCount(),
                     parms != nullptr ? parms->GetEventID() : "-", parms != nullptr ? parms->GetNumOpponents() : 0);
        for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
            GRacerInfo &info = raceStatus.GetRacerInfo(i);
            ISimable *isim = info.GetSimable();
            UMath::Vector3 pos = isim != nullptr ? isim->GetPosition() : UMath::Vector3::kZero;
            std::fprintf(stderr, "[lua]   racer %d %s char=%s simable=%p pos=(%.1f %.1f %.1f)\n", i, info.GetName() != nullptr ? info.GetName() : "?",
                         info.GetGameCharacter() != nullptr ? static_cast<const char *>(info.GetGameCharacter()->CollectionName()) : "-",
                         static_cast<void *>(isim), pos.x, pos.y, pos.z);
        }
    }
#endif

    if (parms != nullptr) {
        GManager::Get().PreloadStockCarsForActivity(parms->GetActivity());
        const char *copSpawnType = parms->GetGameplayObj()->CopSpawnType();
        if (copSpawnType != nullptr && *copSpawnType != '\0') {
            GManager::Get().ReserveStockCar(copSpawnType);
        }
    }
    // TODO(port): career races commit the audio/render assets here
    // (ECommitAudioAssets/ECommitRenderAssets), every player's HUD is reset
    // (IPlayer vtable +0x24) and EAXSound::StartNewGamePlay runs.
}

// PC: 0x611570. Moves the (first) player to the trigger, streaming the world
// in around it first.
void Game_WarpPlayerToTrigger(GRuntimeInstance *instance) {
    GTrigger *trigger = AsTrigger(instance);
    UMath::Vector3 pos;
    UMath::Vector3 dir;
    if (instance == nullptr || !instance->GetPosition(pos) || !instance->GetDirection(dir)) {
        return;
    }
    // TODO(port): the PC streams the world in at the new position
    // (Sim::SetStream, waiting in MiniMainLoop with the screen faded) and
    // split screen warps both players (0x60D820).
    IPlayer *player = FirstPlayer();
    if (player == nullptr) {
        return;
    }
    ISimable *isim = player->GetSimable();
    IVehicle *ivehicle;
    if (isim != nullptr && isim->QueryInterface(&ivehicle)) {
        ivehicle->SetVehicleOnGround(pos, dir);
    }
    (void)trigger;
}

// PC: 0x604D10.
void Game_ResetTrigger(GRuntimeInstance *instance) {
    GTrigger *trigger = AsTrigger(instance);
    if (trigger != nullptr) {
        trigger->Reset();
    }
}

// PC: 0x611670.
void Game_SetPlayerStartPosition(GRuntimeInstance *marker) {
    if (marker == nullptr) {
        return;
    }
    UMath::Vector3 pos = UMath::Vector3::kZero;
    UMath::Vector3 dir = UMath::Vector3::kZero;
    marker->GetPosition(pos);
    marker->GetDirection(dir);
    IPlayer *player = FirstPlayer();
    if (player == nullptr) {
        return;
    }
    GRacerInfo *info = RaceStatus().GetRacerInfo(player->GetSimable());
    if (info != nullptr) {
        info->ForceStartPosition(pos, dir);
    }
}

// PC: 0x611700. Points the racer's AI at its next goal (and the checkpoint
// marker at it, for the first player).
void Game_SetRacerGoal(int racerIndex, GRuntimeInstance *goal) {
    GTrigger *trigger = AsTrigger(goal);
    if (trigger == nullptr) {
        return;
    }
    GRacerInfo &info = RacerInfo(racerIndex);
    ISimable *isim = info.GetSimable();
    if (isim == nullptr) {
        return;
    }
    IVehicleAI *ai;
    if (isim->QueryInterface(&ai)) {
        AITarget *target = ai->GetTarget();
        if (target != nullptr) {
            UMath::Vector3 pos;
            trigger->GetPosition(pos);
            target->Aquire(pos, trigger->GetDirection());
        }
        WRoadNav *nav = ai->GetDriveToNav();
        if (nav != nullptr) {
            nav->CancelPathFinding();
        }
    }
    IPlayer *player = FirstPlayer();
    if (isim->GetPlayer() != nullptr && isim->GetPlayer() == player) {
        RaceStatus().SetNextCheckpointPos(trigger);
    }
}

// PC: 0x604DB0. Does nothing on PC.
void Game_SetRacerLapsLeft(int racerIndex, int lapsLeft) {}

// PC: 0x611440.
void Game_KnockoutRacer(ISimable *racer) {
    if (racer == nullptr) {
        return;
    }
    GRacerInfo &info = RacerInfo(Game_GetRacerIndex(racer));
    info.KnockOut();
    ISimable *isim = info.GetSimable();
    if (isim != nullptr) {
        MNotifyKnockedOut(isim->GetInstanceHandle()).Post(UCrc32(UCRC32_Gameplay));
        IVehicle *ivehicle;
        if (!isim->IsPlayer() && isim->QueryInterface(&ivehicle)) {
            ivehicle->Deactivate();
        }
    }
    RaceStatus().UpdateAdaptiveDifficulty(static_cast<GRaceStatus::eAdaptiveGainReason>(3), racer);
}

// PC: 0x60AA00.
void Game_NotifyRacePlacement(GRuntimeInstance *activity, ISimable *racer, int place) {
    GCollectionKey key;
    key.mCollectionKey = activity != nullptr ? activity->GetCollection() : 0;
    MNotifyRacePlacement(key, racer != nullptr ? racer->GetInstanceHandle() : 0, place).Post(UCrc32(UCRC32_Gameplay));
}

// PC: 0x60A7B0. TODO(port): speed trap results (GManager speed traps).
void Game_NotifySpeedTrapTriggered(GRuntimeInstance *activity, GRuntimeInstance *trap, ISimable *racer, float speed) {
    LUA_HOOK_TODO("Game.NotifySpeedTrapTriggered");
}

// PC: 0x6117F0.
void Game_NotifyCheckpointReached(ISimable *racer, int checkpoint) {
    int racerIndex = Game_GetRacerIndex(racer);
    if (racerIndex < 1) {
        return;
    }
    GRacerInfo &info = RacerInfo(racerIndex);
    RaceStatus().SetCheckpointTime(info.GetLapsCompleted(), checkpoint, racerIndex - 1, info.GetCheckTime());
    info.StartCheckpoint(checkpoint);
}

// PC: 0x611850. Knockout races also knock out whoever is last.
void Game_NotifyLapFinished(ISimable *racer, int lap) {
    int racerIndex = Game_GetRacerIndex(racer);
    if (racerIndex < 1) {
        return;
    }
    GRaceStatus &raceStatus = RaceStatus();
    GRacerInfo &info = RacerInfo(racerIndex);
    raceStatus.SetLapTime(info.GetLapsCompleted(), racerIndex - 1, info.GetLapTime());
    info.StartLap(lap + 1);
    raceStatus.CalculateRankings();
    GRaceParameters *parms = raceStatus.GetRaceParameters();
    if (parms == nullptr || parms->GetRaceType() != GRace::kRaceType_Knockout) {
        return;
    }
    int knockoutsPerLap = parms->GetKnockoutsPerLap();
    int numOpponents = parms->GetNumOpponents();
    int finishedLap = 0;
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        if (raceStatus.GetRacerInfo(i).GetLapsCompleted() >= lap) {
            finishedLap++;
        }
    }
    if (finishedLap != numOpponents + 1 - knockoutsPerLap * lap) {
        return;
    }
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        GRacerInfo &other = raceStatus.GetRacerInfo(i);
        if (!other.GetIsKnockedOut() && other.GetLapsCompleted() < lap) {
            Game_KnockoutRacer(other.GetSimable());
            // TODO(port): EKnockoutRacer (the HUD message) isn't ported.
        }
    }
}

// PC: 0x6119F0. The racer is done; a human winner of a career race is scored.
// TODO(port): the career bookkeeping (cash, upgrades, unlocks, race score
// flags, split screen/online messages) isn't ported.
void Game_NotifyRaceFinished(ISimable *racer) {
    if (racer == nullptr) {
        IPlayer *player = FirstPlayer();
        racer = player != nullptr ? player->GetSimable() : nullptr;
    }
    int racerIndex = Game_GetRacerIndex(racer);
    if (racerIndex < 1) {
        return;
    }
    GRaceStatus &raceStatus = RaceStatus();
    GRacerInfo &info = RacerInfo(racerIndex);
    info.FinishRace();
    info.SetPctRaceComplete(100.0f);
    raceStatus.CalculateRankings();
    if (racer->IsPlayer()) {
        info.ForceStop();
    }
    IVehicleAI *ai;
    if (racer->QueryInterface(&ai) && ai->GetTarget() != nullptr) {
        ai->GetTarget()->Clear();
    }
    raceStatus.UpdateAdaptiveDifficulty(static_cast<GRaceStatus::eAdaptiveGainReason>(4), racer);
}

// PC: 0x604ED0.
void Game_SetRaceActivity(GRuntimeInstance *activity) {
    GActivity *raceActivity = AsActivity(activity);
    if (raceActivity != nullptr) {
        RaceStatus().SetRaceActivity(raceActivity);
    }
}

// PC: 0x60DBD0. Racing begins: traffic, barriers, the race's length and
// catch-up, each racer's IRacer and heat.
void Game_StartRace(GRuntimeInstance *activity) {
    GRaceStatus &raceStatus = RaceStatus();
    raceStatus.SetRacing();
    GManager::Get().UnspawnAllCharacters();
    // TODO(port): FESoundControl(false, "Game_StartRace") and GPS_Disengage.
    GRaceParameters *parms = raceStatus.GetRaceParameters();
    int trafficDensity = parms != nullptr ? parms->GetTrafficDensity() : 0;
    if (activity != nullptr && activity->ForceTrafficDensity() > 0) {
        trafficDensity = activity->ForceTrafficDensity();
    }
    if (parms != nullptr && parms->GetRaceType() == GRace::kRaceType_Drag) {
        raceStatus.SetTrafficDensity(0);
    } else {
        raceStatus.SetTrafficDensity(trafficDensity);
    }
    const char *trafficPattern = parms != nullptr ? parms->GetTrafficPattern() : nullptr;
    raceStatus.SetTrafficPattern(trafficPattern != nullptr && *trafficPattern != '\0' ? Attrib::StringToLowerCaseKey(trafficPattern) : 0);
    // TODO(port): the "e3demo" race redoes the topology/scenery groups.
    raceStatus.EnableBarriers();
    new EResetProps();
    raceStatus.DetermineRaceLength();
    raceStatus.MakeCatchUpData();
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        ISimable *isim = raceStatus.GetRacerInfo(i).GetSimable();
        if (isim == nullptr) {
            continue;
        }
        IRacer *iracer;
        if (isim->QueryInterface(&iracer)) {
            iracer->StartRace(parms != nullptr && parms->GetRaceType() == GRace::kRaceType_Drag ? static_cast<DriverStyle>(1) : static_cast<DriverStyle>(0));
        }
        IPerpetrator *perp;
        if (isim->QueryInterface(&perp) && activity != nullptr) {
            float heat = static_cast<float>(activity->ForceHeatLevel());
            float current = perp->GetHeat();
            if (current > heat) {
                heat = current;
            }
            perp->SetHeat(heat);
        }
    }
    // TODO(port): EFadeScreenOff when the screen is faded (FadeScreen isn't
    // reachable from here natively).
}

// PC: 0x604EF0.
void Game_StartRaceTimers() {
    GRaceStatus &raceStatus = RaceStatus();
    raceStatus.StartMasterTimer();
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        raceStatus.GetRacerInfo(i).StartRace();
    }
}

// PC: 0x604E90.
void Game_SaveStartPositions() {
    GRaceStatus &raceStatus = RaceStatus();
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        raceStatus.GetRacerInfo(i).SaveStartPosition();
    }
}

// PC: 0x60AA50.
void Game_RestoreStartPositions() {
    if (ITrafficMgr::Exists()) {
        ITrafficMgr::Get()->FlushAllTraffic(false);
    }
    GRaceStatus &raceStatus = RaceStatus();
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        GRacerInfo &info = raceStatus.GetRacerInfo(i);
        info.RestoreStartPosition();
        info.ClearRaceStats();
    }
}

// PC: 0x611F20. TODO(port): the post race screens (FE) aren't driven yet.
void Game_EnterPostRaceFlow() {
    RaceStatus().FinalizeRaceStats();
    Game_AbandonRace();
    LUA_HOOK_TODO("Game.EnterPostRaceFlow (post race screen)");
}

// PC: 0x605690.
bool FE_ShowOnlinePostRaceScreen() {
    return false;
}

// PC: 0x604F40.
void Game_SetCopsEnabled(bool enabled) {
    MSetCopsEnabled(enabled).Post(UCrc32(stringhash32("AICopManager")));
}

// PC: 0x60AAB0.
void Game_NoNewPursuitsOrCops() {
    if (ICopMgr::Exists()) {
        ICopMgr::Get()->NoNewPursuitsOrCops();
    }
}

// PC: 0x60AAC0.
void Game_ForcePursuitStart(int minHeat) {
    GManager::Get().QueueFreeRoamPursuit(static_cast<float>(minHeat));
}

// PC: 0x604F80 / 0x604FD0. TODO(port): EEnterEngagableTrigger (career world
// race engage) isn't ported.
void Game_EnterEngagableTrigger(GRuntimeInstance *trigger) {
    LUA_HOOK_TODO("Game.EnterEngagableTrigger");
}

void Game_ExitEngagableTrigger(GRuntimeInstance *trigger) {}

// PC: 0x611FB0 / 0x611FF0. TODO(port): menu gate zones (career).
void Game_EnterGateZone(GRuntimeInstance *trigger, const char *zone) {}

void Game_ExitGateZone(GRuntimeInstance *trigger, const char *zone) {}

// PC: 0x6120C0 / 0x605020. TODO(port): the race over summary HUD.
void Game_ShowRaceOverSummary() {
    LUA_HOOK_TODO("Game.ShowRaceOverSummary");
}

void Game_HideRaceOverSummary() {}

// PC: 0x605090. TODO(port): characters aren't spawned (GCharacter::Spawn).
void Game_SetTrafficSpeed(GRuntimeInstance *character, float speedDefault, float speedHighway) {}

// PC: 0x60A670.
void Game_SpawnCop(GRuntimeInstance *marker, const char *carName, bool inPursuit, bool roadBlock) {
    UMath::Vector3 pos;
    UMath::Vector3 dir;
    if (marker == nullptr || carName == nullptr || !marker->GetPosition(pos) || !marker->GetDirection(dir) || !ICopMgr::Exists()) {
        return;
    }
    ICopMgr::Get()->LockoutCops(false);
    ICopMgr::Get()->SpawnCop(pos, dir, carName, inPursuit, roadBlock);
}

// PC: 0x604D40. TODO(port): GCharacter::Spawn (0x601320) and the pending
// character service aren't ported.
void Game_SpawnCharacter(GRuntimeInstance *character, GRuntimeInstance *spawn, GRuntimeInstance *target, float speed) {
    LUA_HOOK_TODO("Game.SpawnCharacter");
}

// PC: 0x604DA0.
void Game_UnspawnCharacter(GRuntimeInstance *character) {
    if (character != nullptr && character->GetType() == kGameplayObjType_Character) {
        static_cast<GCharacter *>(character)->Unspawn();
    }
}

// PC: 0x60A6E0. TODO(port): character stimuli.
void Game_SendCharacterStimulus(GRuntimeInstance *character, const char *stimulus) {}

// PC: 0x605030. TODO(port): jack-knifing a spawned truck.
void Game_JackKnife(GRuntimeInstance *character) {}

// PC: 0x60DF70. Staging holds the cars at the line (and revs the AI).
void Game_SetAllStaging(bool staging) {
    GRaceStatus &raceStatus = RaceStatus();
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        ISimable *isim = raceStatus.GetRacerInfo(i).GetSimable();
        IVehicle *ivehicle;
        if (isim != nullptr && isim->QueryInterface(&ivehicle)) {
            ivehicle->SetStaging(staging);
        }
    }
    // TODO(port): MAIEngineRev on "AIRev" when staging.
}

// PC: 0x6050F0.
void Game_ShowPauseMenu() {}

// PC: 0x612130 / 0x60E030.
void Game_AwardCash(ISimable *racer, float cash) {}

void Game_AwardPoints(ISimable *racer, float points) {
    GRacerInfo *info = RaceStatus().GetRacerInfo(racer);
    if (info != nullptr) {
        info->AddToPointTotal(points);
    }
}

// PC: 0x605140.
void Game_ChallengeCompleted() {}

// PC: 0x612140. TODO(port): career unlocks.
void Game_UnlockRace(GRuntimeInstance *activity) {}

// PC: 0x60E160. Go: every racer launches.
void Game_NotifyCountdownDone() {
    GRaceStatus &raceStatus = RaceStatus();
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        ISimable *isim = raceStatus.GetRacerInfo(i).GetSimable();
        IVehicle *ivehicle;
        if (isim != nullptr && isim->QueryInterface(&ivehicle)) {
            ivehicle->Launch();
        }
    }
    // TODO(port): online races note the start time.
}

// PC: 0x60AB10.
void Game_ResetCopsForRestart() {
    if (ICopMgr::Exists()) {
        ICopMgr::Get()->ResetCopsForRestart(true);
    }
}

// PC: 0x604D20 / 0x604D30.
void Game_ShowTriggerIcon(GRuntimeInstance *trigger) {
    GTrigger *gtrigger = AsTrigger(trigger);
    if (gtrigger != nullptr) {
        gtrigger->ShowIcon();
    }
}

void Game_HideTriggerIcon(GRuntimeInstance *trigger) {
    GTrigger *gtrigger = AsTrigger(trigger);
    if (gtrigger != nullptr) {
        gtrigger->HideIcon();
    }
}

// PC: 0x605930.
void Game_WarpToMarkerWhenRoaming(GRuntimeInstance *marker) {
    LUA_HOOK_TODO("Game.WarpToMarkerWhenRoaming");
}

// PC: 0x605890. Nothing left on PC.
void Game_DoSpecialFinalization(GRuntimeInstance *activity) {}

// PC: 0x611E80 / 0x604E40. TODO(port): speed traps and menu gates (career).
bool Game_IsActiveSpeedTrap(GRuntimeInstance *trigger) {
    return false;
}

bool Game_IsActiveMenuGate(GRuntimeInstance *trigger) {
    return false;
}

// PC: 0x604CD0.
float Game_GetSimTime() {
    return Sim::GetTime();
}

// PC: 0x60DAE0. 1-based; -1 when it isn't racing.
int Game_GetRacerIndex(ISimable *racer) {
    GRaceStatus &raceStatus = RaceStatus();
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        if (raceStatus.GetRacerInfo(i).GetSimable() == racer) {
            return i + 1;
        }
    }
    return -1;
}

// PC: 0x60DB60.
ISimable *Game_GetRacerElement(int racerIndex) {
    return RacerInfo(racerIndex).GetSimable();
}

// PC: 0x612370.
ISimable *Game_GetPlayerElement(int playerIndex) {
    IPlayer *player = playerIndex == 0 ? FirstPlayer() : LastPlayer();
    return player != nullptr ? player->GetSimable() : nullptr;
}

// PC: 0x604DE0.
GRuntimeInstance *Game_GetRacerCharacter(int racerIndex) {
    return RacerInfo(racerIndex).GetGameCharacter();
}

// PC: 0x604E00.
int Game_GetNumRacers() {
    return RaceStatus().GetRacerCount();
}

// PC: 0x604E10.
float Game_GetSimableSpeedKmh(ISimable *simable) {
    if (simable != nullptr) {
        IRigidBody *body = simable->GetRigidBody();
        if (body != nullptr) {
            return body->GetSpeed() * 3.6f;
        }
    }
    return 0.0f;
}

// PC: 0x611E20.
bool Game_RacerIsHuman(int racerIndex) {
    ISimable *isim = RacerInfo(racerIndex).GetSimable();
    IVehicle *ivehicle;
    if (isim != nullptr && isim->QueryInterface(&ivehicle)) {
        return ivehicle->GetDriverClass() == DRIVER_HUMAN;
    }
    return false;
}

// PC: 0x60A760.
bool Game_PlayerIsLocal(ISimable *simable) {
    const IPlayer::List &players = IPlayer::GetList(PLAYER_ALL);
    for (IPlayer::List::const_iterator iter = players.begin(); iter != players.end(); ++iter) {
        if ((*iter)->GetSimable() == simable) {
            return true;
        }
    }
    return false;
}

// PC: 0x6051B0 / 0x605170. TODO(port): career race score flags.
bool Game_IsRaceCompleted(GRuntimeInstance *activity) {
    return false;
}

bool Game_IsRaceUnlocked(GRuntimeInstance *activity) {
    return true;
}

// PC: 0x605460 / 0x605480. TODO(port): FEDatabase's online/LAN game flags
// (bits 3 and 6 at +0x12C); there's no online play natively.
bool Game_IsOnlineGame() {
    return false;
}

bool Game_IsLANGame() {
    return false;
}

// PC: 0x6051F0.
bool Game_AllRacersDone() {
    GRaceStatus &raceStatus = RaceStatus();
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        GRacerInfo &info = raceStatus.GetRacerInfo(i);
        if (!info.IsFinishedRacing() && !info.GetIsKnockedOut() && !info.GetIsTotalled() && !info.GetIsEngineBlown() && !info.GetIsBusted()) {
            return false;
        }
    }
    return true;
}

// PC: 0x60E0A0.
bool Game_AllHumanPlayersDone() {
    GRaceStatus &raceStatus = RaceStatus();
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        GRacerInfo &info = raceStatus.GetRacerInfo(i);
        ISimable *isim = info.GetSimable();
        IVehicle *ivehicle;
        if (isim != nullptr && isim->QueryInterface(&ivehicle) && ivehicle->GetDriverClass() == DRIVER_HUMAN && !info.IsFinishedRacing() &&
            !info.GetIsKnockedOut() && !info.GetIsTotalled() && !info.GetIsEngineBlown() && !info.GetIsBusted()) {
            return false;
        }
    }
    return true;
}

// PC: 0x60AB90.
float Game_SimableDistance(ISimable *simable, GRuntimeInstance *target) {
    UMath::Vector3 targetPos;
    if (simable == nullptr || target == nullptr || !target->GetPosition(targetPos)) {
        return 0.0f;
    }
    const UMath::Vector3 &pos = simable->GetPosition();
    UMath::Vector3 delta = {pos.x - targetPos.x, pos.y - targetPos.y, pos.z - targetPos.z};
    return UMath::Length(delta);
}

// PC: 0x6053B0. TODO(port): the angle between the simable's heading and the
// target's direction.
float Game_SimableAngle(ISimable *simable, GRuntimeInstance *target) {
    LUA_HOOK_TODO("Game.SimableAngle");
    return 0.0f;
}

// PC: 0x605640 .. 0x605660.
bool Game_AllowEngageEvents() {
    return GManager::Get().GetAllowEngageEvents();
}

bool Game_AllowMenuGates() {
    return GManager::Get().GetAllowMenuGates();
}

bool Game_AllowEngageSafehouse() {
    return GManager::Get().GetAllowEngageSafehouse();
}

// PC: 0x605830. Awards the rival's car before the epic pursuit.
// TODO(port): FEPlayerCarDB::AwardRivalCar.
bool Game_DoSpecialSetup(GRuntimeInstance *activity) {
    return false;
}

// PC: 0x612300. TODO(port): the career bounty.
int Game_GetPlayerBounty() {
    return 0;
}

void Game_AwardPlayerBounty(int bounty) {}

// PC: 0x605250 / 0x6052B0. TODO(port): career car lot / safe house jumps.
void Game_JumpToCarLot() {
    LUA_HOOK_TODO("Game.JumpToCarLot");
}

// PC: 0x60DEB0. Stops the race clock and takes every racer out of the race.
void Game_AbandonRace() {
    GRaceStatus &raceStatus = RaceStatus();
    raceStatus.StopMasterTimer();
    raceStatus.SetNextCheckpointPos(nullptr);
    raceStatus.ClearCheckpoints();
    if (raceStatus.GetRaceContext() == GRace::kRaceContext_Career) {
        GManager::Get().UnspawnAllCharacters();
        GManager::Get().ClearStockCars();
    }
    for (int i = 0; i < raceStatus.GetRacerCount(); i++) {
        ISimable *isim = raceStatus.GetRacerInfo(i).GetSimable();
        IRacer *iracer;
        if (isim != nullptr && isim->QueryInterface(&iracer)) {
            iracer->QuitRace();
        }
    }
}

void Game_JumpToSafeHouse() {
    LUA_HOOK_TODO("Game.JumpToSafeHouse");
}

// PC: 0x605300 / 0x605320.
void Game_SetTimer(const char *name, float interval) {
    GManager::Get().SetTimer(name, interval);
}

void Game_KillTimer(const char *name) {
    GManager::Get().KillTimer(name);
}

// PC: 0x605340. TODO(port): GPS_Disengage / WorldMap::ClearGPSing.
void Game_ShowGPS(bool show) {}

// PC: 0x605360. TODO(port): GPS navigation.
void Game_NavigatePlayerTo(GRuntimeInstance *from, GRuntimeInstance *to, float distance) {}

// PC: 0x605420. TODO(port): MNotifyFinished for career activities.
void Game_NotifyFinished(GRuntimeInstance *activity) {}

// PC: 0x612490.
bool Game_SkipCareerIntro() {
    return false;
}

// PC: 0x6054A0. Any real chance of rain makes it rain; otherwise the
// override is cleared.
void Game_SetChanceOfRain(float chance) {
    if (chance > 1.0f) {
        chance = 1.0f;
    } else if (chance < 0.0f) {
        chance = 0.0f;
    }
    SetOverRideRainIntensity(chance >= 0.01f ? 0.0f : -1.0f);
}

// PC: 0x605500.
void Game_DoFade() {
    new EFadeScreenOn(false);
}

// PC: 0x605550. TODO(port): rival introduction (career).
void Game_IntroduceRival() {}

// PC: 0x6124E0. Career races play the race type's tutorial movie the first
// time; otherwise the script moves on at once.
// TODO(port): the tutorial movies (EPlayRaceMovie and the profile's seen
// flags) aren't ported.
void Game_PlayTutorial() {
    MNotifyMovieFinished().Post(UCrc32(UCRC32_Gameplay));
}

// PC: 0x605560 / 0x612030. TODO(port): safe house / zone menus (career).
void Game_DoSafeHouseIntro(GRuntimeInstance *activity) {}

void Game_DoZoneMenuAction(GRuntimeInstance *trigger) {}

// PC: 0x605780. Returns false (the world doesn't need a reload) on PC.
bool Game_SetCurrentTimeOfDay(GRuntimeInstance *activity) {
    GActivity *raceActivity = AsActivity(activity);
    if (raceActivity == nullptr) {
        return false;
    }
    GRaceParameters *parms = GRaceDatabase::Get().GetRaceFromActivity(raceActivity);
    if (parms != nullptr && parms->GetTimeOfDay() >= 0.0f) {
        // TODO(port): SetTimeOfDay(parms->GetTimeOfDay()).
    }
    return false;
}

// PC: 0x6057E0. TODO(port): EReloadGame (career only).
void Game_ReloadWorld(GRuntimeInstance *activity) {
    LUA_HOOK_TODO("Game.ReloadWorld");
}

// PC: 0x605610.
bool Game_IsCareerMode() {
    return GRaceStatus::Exists() && RaceStatus().GetRaceContext() == GRace::kRaceContext_Career;
}

// PC: 0x605630.
bool Game_IsSplitScreen() {
    return Sim::GetUserMode() == Sim::USER_SPLIT_SCREEN;
}

// PC: 0x605670 / 0x612660. TODO(port): career rap sheet / world heat.
void Game_SetHasRapSheet() {}

void Game_SetWorldHeat(float heat) {}

// PC: 0x60E240 / 0x60E230. TODO(port): the post race screens.
void FE_ShowWinningPostRaceScreen() {
    LUA_HOOK_TODO("Game.ShowWinningPostRaceScreen");
}

void FE_ShowLosingPostRaceScreen() {
    LUA_HOOK_TODO("Game.ShowLosingPostRaceScreen");
}

// PC: 0x60AE00.
void Game_PreventPlayerBeingBusted() {
    if (ICopMgr::Exists()) {
        ICopMgr::Get()->SetAllBustedTimersToZero();
    }
}

// PC: 0x6058B0. Returns its rank argument (speed trap races recompute it for
// the player). The PC returns the rank; the binder discards it.
void Game_CalculateRanking(ISimable *racer, int rank) {
    if (Sim::GetUserMode() == Sim::USER_SPLIT_SCREEN || !GRaceStatus::Exists() || RaceStatus().GetRaceContext() == GRace::kRaceContext_Online) {
        return;
    }
    if (racer != nullptr && racer->IsPlayer() && RaceStatus().GetRaceType() == GRace::kRaceType_SpeedTrap) {
        RaceStatus().FinalizeRaceStats();
        RaceStatus().CalculateRankings();
    }
}
