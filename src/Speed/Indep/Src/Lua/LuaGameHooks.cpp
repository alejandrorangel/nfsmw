// The game functions the race/activity scripts call (bound by
// LuaBindery::BindToGameCode, PC 0x61E750). Each is PC: <address>.
#include "Speed/Indep/Src/Lua/LuaGameHooks.h"

#include "Speed/Indep/Src/AI/AITarget.h"
#include "Speed/Indep/Src/EAXSound/EAXSOund.hpp"
#include "Speed/Indep/Src/World/VisualTreatment.h"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Gameplay/GActivity.h"
#include "Speed/Indep/Src/Gameplay/GCharacter.h"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GMarker.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Gameplay/GTrigger.h"
#include "Speed/Indep/Src/Generated/Events/EAwardUpgrade.hpp"
#include "Speed/Indep/Src/Generated/Events/ECommitAudioAssets.hpp"
#include "Speed/Indep/Src/Generated/Events/ECommitRenderAssets.hpp"
#include "Speed/Indep/Src/Generated/Events/EEnterEngagableTrigger.hpp"
#include "Speed/Indep/Src/Generated/Events/EExitEngagableTrigger.hpp"
#include "Speed/Indep/Src/Generated/Events/EFadeScreenOn.hpp"
#include "Speed/Indep/Src/Generated/Events/EQuitToFE.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/FEAnyMovieScreen.hpp"
#include "Speed/Indep/Src/Generated/Events/EPause.hpp"
#include "Speed/Indep/Src/Generated/Events/EShowMilestones.hpp"
#include "Speed/Indep/Src/Generated/Events/EShowRaceOverMessage.hpp"
#include "Speed/Indep/Src/Generated/Events/EShowResults.hpp"
#include "Speed/Indep/Src/Generated/Messages/MControlPathfinder.h"
#include "Speed/Indep/Src/Generated/Messages/MFlowReadyForOutro.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyChallengePassed.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifySpeedTrap.h"
#include "Speed/Indep/Src/Generated/Messages/MNotifyPlayerRep.h"
#include "Speed/Indep/Src/Frontend/Database/VehicleDB.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/HUD/FEPkg_Hud.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/PhotoFinish.hpp"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/gameplay.h"
#include "Speed/Indep/Src/Interfaces/IFengHud.h"
#include "Speed/Indep/Src/Speech/SoundAI.h"
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
// Defined in Frontend/Database/VehicleDB.cpp.
void AdjustStableHeat_EventWin(int playerNum);
void AdjustStableImpound_EventWin(int playerNum);

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
#if defined(EA_PORT_NATIVE)
    // A race's intro NIS ends with the player's car at the start marker
    // (the opponents are already on the grid, Game_InitRacers): without the
    // NIS, put it there as the scripts do when a race has no intro NIS.
    if (nisName != nullptr && *nisName != '\0' && type != nullptr && bStrICmp(type, "Intro") == 0 && location != nullptr) {
        Game_WarpPlayerToTrigger(location);
    }
#endif
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
    // A career race engaged from free roam loads its opponents' cars now (a
    // quick race's loading flow does it in QuickGame).
    if (raceStatus.GetRaceContext() == GRace::kRaceContext_Career) {
        new ECommitAudioAssets();
        new ECommitRenderAssets();
    }
    // Every player's HUD goes; the race's comes back with the race.
    const IPlayer::List &allPlayers = IPlayer::GetList(PLAYER_ALL);
    for (IPlayer::List::const_iterator iter = allPlayers.begin(); iter != allPlayers.end(); ++iter) {
        (*iter)->SetHud(PHT_NONE);
    }
    if (g_pEAXSound != nullptr) {
        g_pEAXSound->StartNewGamePlay();
    }
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

// PC: 0x60A7B0. A racer went through a speed trap at speedKmh: tells the
// game (MNotifySpeedTrap), records it on the world's speed trap, and either
// shows the free roam speed trap screen or, in a speed trap race, stores the
// racer's speed and flashes it on the racer's HUD.
void Game_NotifySpeedTrapTriggered(GRuntimeInstance *activity, GRuntimeInstance *trap, ISimable *racer, float speedKmh) {
    GCollectionKey key;
    key.mCollectionKey = activity != nullptr ? activity->GetCollection() : 0;
    MNotifySpeedTrap(key, racer != nullptr ? racer->GetInstanceHandle() : 0, speedKmh).Post(UCrc32(UCRC32_Gameplay));
    GManager::Get().TrackValue("speedtrap_speed", speedKmh);
    float mps = speedKmh * 0.27778f;
    unsigned int trapKey = trap != nullptr ? trap->GetCollection() : 0;
    // The world's (active) speed trap with this key records the pass.
    GSpeedTrap *speedTrap = GManager::Get().GetFirstSpeedTrap(true, 0);
    for (; speedTrap != nullptr; speedTrap = GManager::Get().GetNextSpeedTrap(speedTrap, true, 0)) {
        if (speedTrap->GetSpeedTrapKey() == trapKey) {
            speedTrap->NotifyTriggered(mps);
            break;
        }
    }
    if (!GRaceStatus::Exists()) {
        return;
    }
    GRaceStatus &raceStatus = RaceStatus();
    if (raceStatus.GetPlayMode() == GRaceStatus::kPlayMode_Roaming) {
        // The PC reads the bounty off the trap it found (a null trap would
        // crash there; no bounty here).
        FE_ShowSpeedTrapScreen(speedKmh, speedTrap != nullptr ? speedTrap->GetBounty() : 0.0f);
        Game_ChallengeCompleted();
        return;
    }
    GRaceParameters *parms = raceStatus.GetRaceParameters();
    if (parms != nullptr && parms->GetRaceType() == GRace::kRaceType_SpeedTrap && racer != nullptr) {
        const Attrib::Gen::gameplay *gameplay = parms->GetGameplayObj();
        for (unsigned int i = 0; i < gameplay->Num_SpeedTrapList(); i++) {
            GRacerInfo *info;
            if (gameplay->SpeedTrapList(i).mCollectionKey == trapKey && (info = raceStatus.GetRacerInfo(racer)) != nullptr) {
                info->NotifySpeedTrapTriggered(static_cast<int>(i), mps);
            }
        }
    }
    IPlayer *player = racer != nullptr ? racer->GetPlayer() : nullptr;
    IHud *hud = player != nullptr ? player->GetHud() : nullptr;
    IGenericMessage *message;
    if (hud != nullptr && hud->QueryInterface(&message)) {
        bool metric = FEDatabase->GetGameplaySettings()->SpeedoUnits != 0;
        char text[64];
        bSPrintf(text, "+%0.0f %s", mps * (metric ? 3.6f : 2.23699f), GetLocalizedString(metric ? 0x8569A25F : 0x8569AB44));
        message->RequestGenericMessage(text, false, 0x8AB83EDB, bStringHash("SPEEDTRAP_FLASHER_ICON"), 0x609F6B15,
                                       static_cast<GenericMessage_Priority>(3));
    }
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
        IPlayer *player = racer->GetPlayer();
        if (Sim::GetUserMode() == Sim::USER_SINGLE && info.GetGameCharacter() == nullptr && !info.GetIsTotalled() &&
            !info.GetIsEngineBlown() && player != nullptr && player->IsLocal()) {
            new EShowRaceOverMessage(player);
        }
        // TODO(port): online races report the driver (OnlineManager::FinishDriver).
        // A player's win: the race's cash (and, the first time, its rewards
        // and the races it unlocks), the stable's heat and impound
        // bookkeeping, then the race's records and the event icons.
        if (info.GetRanking() == 1 && player != nullptr) {
            int playerIndex = player->GetSettingsIndex();
            if (playerIndex == 0 || playerIndex == 1) {
                GRaceParameters *parms = raceStatus.GetRaceParameters();
                const Attrib::Gen::gameplay *raceObj = parms->GetGameplayObj();
                if (!raceStatus.GetHasBeenWon()) {
                    raceStatus.SetHasBeenWon(true);
                    bool wonBefore = GRaceDatabase::Get().CheckRaceScoreFlags(parms->GetEventHash(), GRaceDatabase::kCompleted_ContextCareer);
                    if (raceStatus.GetRaceContext() == GRace::kRaceContext_Career) {
                        FEDatabase->GetUserProfile(playerIndex)->GetCareer()->AwardCash(static_cast<int>(parms->GetCashValue()));
                        if (!wonBefore) {
                            for (unsigned int i = 0; i < raceObj->Num_RewardsForWinner(); i++) {
                                new EAwardUpgrade(raceObj->RewardsForWinner(i).mCollectionKey);
                            }
                        }
                        AdjustStableHeat_EventWin(playerIndex);
                        AdjustStableImpound_EventWin(playerIndex);
                    }
                    if (!wonBefore) {
                        for (unsigned int i = 0; i < raceObj->Num_UnlockRaces(); i++) {
                            unsigned int unlockKey = raceObj->UnlockRaces(i).mCollectionKey;
                            GRaceParameters *unlock = unlockKey != 0 ? GRaceDatabase::Get().GetRaceFromKey(unlockKey) : nullptr;
                            if (unlock == nullptr) {
                                continue;
                            }
                            GRaceSaveInfo *unlockInfo = GRaceDatabase::Get().GetScoreInfo(unlock->GetEventHash());
                            // As on PC (0x611C83): the reverse of Game_UnlockRace.
                            unlockInfo->mFlags |= GRaceDatabase::kUnlocked_QuickRace;
                            if (!unlock->GetNeverInQuickRace()) {
                                unlockInfo->mFlags |= GRaceDatabase::kUnlocked_Career;
                            }
                        }
                    }
                }
                GRaceDatabase::Get().UpdateRaceScore(true);
                GManager::Get().RefreshEngageTriggerIcons();
            }
            if (Sim::GetUserMode() == Sim::USER_SINGLE) {
                info.ForceStop();
            }
        } else {
            info.ForceStop();
        }
    }
    IVehicleAI *ai;
    if (racer->QueryInterface(&ai) && ai->GetTarget() != nullptr) {
        ai->GetTarget()->Clear();
    }
    raceStatus.UpdateAdaptiveDifficulty(static_cast<GRaceStatus::eAdaptiveGainReason>(4), racer);
    // The challenge series: its special event, then every one of its races.
    GRaceParameters *parms = raceStatus.GetRaceParameters();
    if (parms != nullptr && parms->GetIsPursuitRace() && bStrICmp(parms->GetEventID(), "19.8.69") == 0) {
        FEDatabase->GetCareerSettings()->SetHasBeatenSpecialChallengeEvent();
    }
    GRaceBin *challengeBin = GRaceDatabase::Get().GetBinNumber(19);
    if (challengeBin == nullptr) {
        return;
    }
    for (unsigned int i = 0; i < challengeBin->GetWorldRaceCount(); i++) {
        unsigned int raceHash = challengeBin->GetWorldRaceHash(i);
        GRaceParameters *race = GRaceDatabase::Get().GetRaceFromHash(raceHash);
        if (race != nullptr && !race->GetIsCollectorsEditionRace() && bStrCmp(race->GetEventID(), "19.8.31") != 0 &&
            !GRaceDatabase::Get().CheckRaceScoreFlags(raceHash, GRaceDatabase::kCompleted_ContextQuickRace)) {
            return;
        }
    }
    FEDatabase->GetCareerSettings()->SetHasBeatenChallengeSeries();
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
    FEngHud::bIsRestartingRace = false;
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

// PC: 0x60AC20. Pursuit races get the milestone screen (winning) or the
// pause menu (losing); otherwise a winner (not in split screen) gets the
// photo finish screen, which goes on to the results, and a loser goes
// straight to the results.
void FE_ShowPostRaceScreen(bool winner) {
    if (GRaceStatus::Exists()) {
        RaceStatus().FinalizeRaceStats();
    }
    if (FE_ShowOnlinePostRaceScreen() || !GRaceStatus::Exists()) {
        return;
    }
    GRaceStatus &raceStatus = RaceStatus();
    if (raceStatus.GetRaceParameters()->GetIsPursuitRace()) {
        SoundAI *soundAI = SoundAI::Get();
        bool musicActive = soundAI != nullptr && soundAI->IsMusicActive();
        if (musicActive) {
            MControlPathfinder msg(false, winner ? 0xE : 0xF, 0, 0);
            msg.Send(UCrc32("Event"));
        }
        if (winner) {
            new EShowMilestones(0);
        } else {
            new EPause(0, 1, 0);
        }
        return;
    }
    if (raceStatus.GetPlayMode() != GRaceStatus::kPlayMode_Racing) {
        return;
    }
    if (winner && Sim::GetUserMode() != Sim::USER_SPLIT_SCREEN) {
        PhotoFinishScreen::mActive = true;
        cFEng::Get()->QueuePackagePush("InGamePhotoMaster.fng", 0, FEngMapJoyportToJoyParam(FEDatabase->GetPlayersJoystickPort(0)), true);
        return;
    }
    new EShowResults(FERESULTTYPE_RACE, false);
}

// PC: 0x605730. The free roam speed trap result (the photo finish screen in
// speed trap mode).
void FE_ShowSpeedTrapScreen(float speedKmh, float bounty) {
    // In m/s (0x8A0294: 0.27778); the screen converts to the player's units.
    PhotoFinishScreen::mSpeedtrapSpeed = speedKmh * 0.27778f;
    PhotoFinishScreen::mSpeedtrapBounty = bounty;
    PhotoFinishScreen::mActive = true;
    cFEng::Get()->QueuePackagePush("InGamePhotoMaster.fng", 2, FEngMapJoyportToJoyParam(FEDatabase->GetPlayersJoystickPort(0)), true);
}

// PC: 0x611F20. Career races flagged with no post race screen go straight to
// their outro; everything else shows the post race screens.
void Game_EnterPostRaceFlow() {
    GRaceStatus &raceStatus = RaceStatus();
    raceStatus.FinalizeRaceStats();
    Game_AbandonRace();
    if (raceStatus.GetRaceContext() == GRace::kRaceContext_Career) {
        GRaceParameters *parms = raceStatus.GetRaceParameters();
        raceStatus.GetWinningPlayerInfo();
        if (parms != nullptr && parms->GetNoPostRaceScreen()) {
            MFlowReadyForOutro().Post(UCrc32(UCRC32_Gameplay));
            GRaceDatabase::Get().GetScoreInfo(parms->GetEventHash())->mFlags |= GRaceDatabase::kCompleted_ContextCareer;
            return;
        }
    }
    FE_ShowPostRaceScreen(true);
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

// PC: 0x604F80 / 0x604FD0. The engage prompt on the HUD while the player is
// inside an event's trigger (the argument is the race activity).
void Game_EnterEngagableTrigger(GRuntimeInstance *activity) {
    new EEnterEngagableTrigger(activity);
}

void Game_ExitEngagableTrigger(GRuntimeInstance *activity) {
    new EExitEngagableTrigger(activity);
}

namespace {

IMenuZoneTrigger *FirstPlayerMenuZoneTrigger() {
    IPlayer *player = FirstPlayer();
    IHud *hud = player != nullptr ? player->GetHud() : nullptr;
    IMenuZoneTrigger *izone;
    return hud != nullptr && hud->QueryInterface(&izone) ? izone : nullptr;
}

} // namespace

// PC: 0x611FB0 / 0x611FF0. A menu gate (safehouse, car lot, customization
// shop): the HUD's engage prompt for the zone.
void Game_EnterGateZone(GRuntimeInstance *trigger, const char *zone) {
    if (IMenuZoneTrigger *izone = FirstPlayerMenuZoneTrigger()) {
        izone->EnterTrigger(zone);
    }
}

void Game_ExitGateZone(GRuntimeInstance *trigger, const char *zone) {
    if (IMenuZoneTrigger *izone = FirstPlayerMenuZoneTrigger()) {
        izone->ExitTrigger();
    }
}

// PC: 0x6120C0 / 0x605020. The "race over" HUD message for the first player
// (not in split screen); hiding it does nothing on PC.
void Game_ShowRaceOverSummary() {
    IPlayer *player = FirstPlayer();
    if (Sim::GetUserMode() != Sim::USER_SPLIT_SCREEN && player != nullptr) {
        new EShowRaceOverMessage(player);
    }
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

// PC: 0x60E030. Points (cash bags, bounty) add to the racer's total; a
// player with a HUD gets a screen pulse and the sound.
void Game_AwardPoints(ISimable *racer, float points) {
    GRacerInfo *info = RaceStatus().GetRacerInfo(racer);
    if (info != nullptr) {
        info->AddToPointTotal(points);
    }
    IPlayer *player = racer != nullptr ? racer->GetPlayer() : nullptr;
    IHud *hud = player != nullptr ? player->GetHud() : nullptr;
    IGenericMessage *imessage;
    if (hud != nullptr && hud->QueryInterface(&imessage)) {
        IVisualTreatment *treatment = IVisualTreatment::Get();
        if (treatment != nullptr) {
            treatment->TriggerPulse(0.2f);
        }
        g_pEAXSound->PlayUISoundFX(static_cast<eMenuSoundTriggers>(4));
    }
}

// PC: 0x612130 (a jump to Game_AwardPoints).
void Game_AwardCash(ISimable *racer, float cash) {
    Game_AwardPoints(racer, cash);
}

// PC: 0x605140.
void Game_ChallengeCompleted() {
    MNotifyChallengePassed("").Post(UCrc32(UCRC32_Gameplay));
}

// PC: 0x612140. The activity's race unlocks in the career (and in quick
// race unless it never shows there); the minimap and the event icons follow.
void Game_UnlockRace(GRuntimeInstance *activity) {
    if (activity == nullptr) {
        return;
    }
    GRaceParameters *race = GRaceDatabase::Get().GetRaceFromActivity(static_cast<GActivity *>(activity));
    if (race == nullptr) {
        return;
    }
    GRaceSaveInfo *info = GRaceDatabase::Get().GetScoreInfo(race->GetEventHash());
    if (!race->GetNeverInQuickRace()) {
        info->mFlags |= GRaceDatabase::kUnlocked_QuickRace;
    }
    info->mFlags |= GRaceDatabase::kUnlocked_Career;
    IPlayer *player = IPlayer::First(PLAYER_LOCAL);
    IHud *hud = player != nullptr ? player->GetHud() : nullptr;
    if (hud != nullptr) {
        hud->RefreshMiniMapItems();
    }
    GManager::Get().RefreshEngageTriggerIcons();
}

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
// The next free roam starts at the marker.
void Game_WarpToMarkerWhenRoaming(GRuntimeInstance *marker) {
    if (marker != nullptr) {
        RaceStatus().SetWarpWhenInFreeRoam(marker->GetCollection());
    }
}

// PC: 0x605890. Nothing left on PC.
void Game_DoSpecialFinalization(GRuntimeInstance *activity) {}

// PC: 0x611E80. A trigger is an active speed trap unless the player is
// racing or in a pursuit.
bool Game_IsActiveSpeedTrap(GRuntimeInstance *trigger) {
    if (RaceStatus().GetPlayMode() == GRaceStatus::kPlayMode_Racing) {
        return false;
    }
    IPlayer *player = IPlayer::First(PLAYER_ALL);
    ISimable *simable = player != nullptr ? player->GetSimable() : nullptr;
    IVehicleAI *ai;
    if (simable != nullptr && simable->QueryInterface(&ai) && ai->GetPursuit() != nullptr) {
        return false;
    }
    if (trigger == nullptr) {
        return false;
    }
    unsigned int key = trigger->GetCollection();
    for (GSpeedTrap *trap = GManager::Get().GetFirstSpeedTrap(true, 0); trap != nullptr; trap = GManager::Get().GetNextSpeedTrap(trap, true, 0)) {
        if (trap->GetSpeedTrapKey() == key) {
            return true;
        }
    }
    return false;
}

// PC: 0x604E40. TODO(port): menu gates (career).

bool Game_IsActiveMenuGate(GRuntimeInstance *trigger) {
    GTrigger *gtrigger = AsTrigger(trigger);
    if (gtrigger == nullptr ||
        !gtrigger->GetFlag(GRuntimeInstance::kFlag_TriggerSafeHouse | GRuntimeInstance::kFlag_TriggerCarLot | GRuntimeInstance::kFlag_TriggerCustomShop)) {
        return false;
    }
    return gtrigger->GetIcon() != nullptr && GManager::Get().GetIsIconVisible(gtrigger->GetIcon());
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

// PC: 0x6051B0. The activity's race won in the career.
bool Game_IsRaceCompleted(GRuntimeInstance *activity) {
    GRaceParameters *race = activity != nullptr ? GRaceDatabase::Get().GetRaceFromActivity(static_cast<GActivity *>(activity)) : nullptr;
    return race != nullptr && GRaceDatabase::Get().CheckRaceScoreFlags(race->GetEventHash(), GRaceDatabase::kCompleted_ContextCareer);
}

// PC: 0x605170. The activity's race unlocked in the career.
bool Game_IsRaceUnlocked(GRuntimeInstance *activity) {
    GRaceParameters *race = activity != nullptr ? GRaceDatabase::Get().GetRaceFromActivity(static_cast<GActivity *>(activity)) : nullptr;
    return race != nullptr && GRaceDatabase::Get().CheckRaceScoreFlags(race->GetEventHash(), GRaceDatabase::kUnlocked_Career);
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

namespace {

// The first player's car stable, when that player is local and has one.
FEPlayerCarDB *FirstPlayerStable(IPlayer *&player) {
    player = FirstPlayer();
    ISimable *simable = player != nullptr ? player->GetSimable() : nullptr;
    if (simable == nullptr || !simable->IsPlayer() || simable->GetPlayer() == nullptr) {
        return nullptr;
    }
    return FEDatabase->GetPlayerCarStable(simable->GetPlayer()->GetSettingsIndex());
}

} // namespace

// PC: 0x612300. The career's bounty: every car's, and the sold ones'.
int Game_GetPlayerBounty() {
    IPlayer *player;
    FEPlayerCarDB *stable = FirstPlayerStable(player);
    return stable != nullptr ? static_cast<int>(stable->GetTotalBounty()) : 0;
}

// PC: 0x612220. Bounty (a milestone's) goes on the current car's career
// record, then MNotifyPlayerRep with the new total.
void Game_AwardPlayerBounty(int bounty) {
    IPlayer *player;
    FEPlayerCarDB *stable = FirstPlayerStable(player);
    if (stable == nullptr) {
        return;
    }
    int playerIndex = player->GetSimable()->GetPlayer()->GetSettingsIndex();
    FECarRecord *car = stable->GetCarRecordByHandle(FEDatabase->GetUserProfile(playerIndex)->GetCareer()->GetCurrentCar());
    FECareerRecord *career = car != nullptr ? stable->GetCareerRecordByHandle(car->CareerHandle) : nullptr;
    if (career == nullptr) {
        return;
    }
    career->AwardBounty(bounty);
    MNotifyPlayerRep(player->GetSimable()->GetInstanceHandle(), static_cast<int>(stable->GetTotalBounty())).Post(UCrc32(UCRC32_Gameplay));
}

// PC: 0x605250. Out to the car lot (the D-Day's end: pick a car).
// TODO(port): PC sets the time of day to 0 first (SetTimeOfDay).
void Game_JumpToCarLot() {
    new EQuitToFE(GARAGETYPE_CAR_LOT, "Car_Select.fng");
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

// PC: 0x6052B0. Out to the safehouse's career menu.
void Game_JumpToSafeHouse() {
    new EQuitToFE(GARAGETYPE_CAREER_SAFEHOUSE, "MainMenu_Sub.fng");
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

// PC: 0x612490. True with SkipFE, the skip-intro switch, no local player,
// or once the player's career has done its intro (the D-Day flow's drive to
// the safehouse, which keeps the cops off).
bool Game_SkipCareerIntro() {
    if (SkipFE != 0 || static_cast<unsigned char>(SkipCareerIntro) == 1) {
        return true;
    }
    IPlayer *player = IPlayer::First(PLAYER_LOCAL);
    if (player == nullptr) {
        return true;
    }
    return FEDatabase->GetMultiplayerProfile(player->GetSettingsIndex())->GetCareer()->HasDoneCareerIntro();
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

// PC: 0x605560. The career's first arrival at the safehouse (the D-Day's
// end): free roam will start from the activity's marker, the career is in
// bin 16, and the safehouse intro movie plays on the way into the safehouse.
// TODO(port): PC sets the time of day to 0 first (SetTimeOfDay).
void Game_DoSafeHouseIntro(GRuntimeInstance *activity) {
    if (activity != nullptr) {
        GManager::Get().SetFreeRoamStartMarker(activity->GetCollection());
        GManager::Get().SetFreeRoamFromSafeHouseStartMarker(activity->GetCollection());
        GManager::Get().SetStartingFreeRoamFromSafeHouse();
    }
    FEDatabase->GetCareerSettings()->SetCurrentBin(16);
    FEAnyMovieScreen::PlaySafehouseIntroMovie();
    new EQuitToFE(GARAGETYPE_CAREER_SAFEHOUSE, FEAnyMovieScreen::GetFEngPackageName());
}

// PC: 0x612030. A menu gate's action (the HUD's: into the safehouse, the car
// lot, the customization shop): the gate's respawn marker is where free roam
// starts again (from the safehouse too, for the safehouse).
void Game_DoZoneMenuAction(GRuntimeInstance *trigger) {
    IPlayer *player = FirstPlayer();
    if (player == nullptr || trigger == nullptr) {
        return;
    }
    Attrib::Key marker = trigger->RespawnMarker().mCollectionKey;
    GManager::Get().SetFreeRoamStartMarker(marker);
    if (IMenuZoneTrigger *izone = FirstPlayerMenuZoneTrigger()) {
        if (izone->IsType("safehouse")) {
            GManager::Get().SetFreeRoamFromSafeHouseStartMarker(marker);
        }
        izone->RequestDoAction();
    }
}

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

// PC: 0x605670.
void Game_SetHasRapSheet() {
    FEDatabase->GetCareerSettings()->SetHasRapSheet();
}

// PC: 0x612660. The first player's heat.
void Game_SetWorldHeat(float heat) {
    IPlayer *player = FirstPlayer();
    ISimable *simable = player != nullptr ? player->GetSimable() : nullptr;
    IPerpetrator *perp;
    if (simable != nullptr && simable->QueryInterface(&perp)) {
        perp->SetHeat(heat);
    }
}

// PC: 0x60E240 / 0x60E230.
void FE_ShowWinningPostRaceScreen() {
    FE_ShowPostRaceScreen(true);
}

void FE_ShowLosingPostRaceScreen() {
    FE_ShowPostRaceScreen(false);
}

// PC: 0x60AE00.
void Game_PreventPlayerBeingBusted() {
    if (ICopMgr::Exists()) {
        ICopMgr::Get()->SetAllBustedTimersToZero();
    }
}

// PC: 0x6058B0. Returns the racer's rank: its rank argument, except that a
// speed trap race recomputes it for the player. The race scripts pass the
// result on to Game.NotifyRacePlacement (and from there pick the winning or
// losing post race screen).
int Game_CalculateRanking(ISimable *racer, int rank) {
    if (Sim::GetUserMode() == Sim::USER_SPLIT_SCREEN || !GRaceStatus::Exists() || RaceStatus().GetRaceContext() == GRace::kRaceContext_Online) {
        return rank;
    }
    if (racer != nullptr && racer->IsPlayer() && RaceStatus().GetRaceType() == GRace::kRaceType_SpeedTrap) {
        RaceStatus().FinalizeRaceStats();
        RaceStatus().CalculateRankings();
        GRacerInfo *info = RaceStatus().GetRacerInfo(racer);
        if (info != nullptr) {
            return info->GetRanking();
        }
    }
    return rank;
}
