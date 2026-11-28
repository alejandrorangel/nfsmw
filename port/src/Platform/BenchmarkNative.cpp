// Native benchmark mode: NFSMW_BENCHMARK=<scenario> (or --benchmark <scenario>).
//
// PC Most Wanted has no benchmark, so this builds one out of the game's own
// dev switches. A race scenario boots straight into a Quick Race with the
// SkipFE options (Config.cpp; QuickGame::HandleSkipFEOptions picks the race by
// its event ID, QuickGame::CreateCars the player car) and hands the player car
// to the race AI (AIVehicleHuman::SetAiControl, the debug "AI drives me"
// toggle), so it drives itself. The frontend scenario boots to the main menu
// and looks at the garage car. The free roam scenario starts a new career
// from the main menu with the game's skip-D-Day switches (SkipDDayRaces and
// SkipCareerIntro, the EASTER_EGG_SKIP_DDAY cheat: CareerSettings::
// StartNewCareer simulates the prologue and starts at blacklist bin 15),
// then RaceStarter::StartCareerFreeRoam, as the career menu's "resume free
// roam" does, and the AI drives the player's car around the city.
//
// Every frame is exactly one 1/60 s sim tick of game time whatever the real
// time (Main.cpp's loop passes PortBenchmarkFixedFrameMs to MainLoop and
// Scheduler::Run takes a fixed step), the random seed is fixed, the real
// keyboard/mouse/pad are ignored, there's no frame pacing and no vsync, and
// every frame the game builds is drawn, in order (BenchmarkMac.mm: encoded on
// the main thread while the game builds the next one, as in normal play), so
// a run is always the same frames, drawn as fast as the machine can.
//
// Counting starts when the race is on (game flow RACING) or the main menu is
// up; after the scenario's frames it writes <scenario>.csv (per frame: wall
// time, the game loop, the render build, the present (the wait to hand the
// frame over), the main thread's encode, the GPU) and the
// screenshots <scenario>_<frame>.png to $NFSMW_BENCHMARK_OUT (default: the
// current directory at launch), prints a summary and exits.
//
// Overrides: NFSMW_BENCHMARK_FRAMES=<n>, NFSMW_BENCHMARK_SHOTS=<f>,<f>,...,
// NFSMW_BENCHMARK_RACE=<event id>, NFSMW_BENCHMARK_CAR=<vehicle>,
// NFSMW_BENCHMARK_OPPONENTS=<n>, NFSMW_BENCHMARK_TRAFFIC=<0..100>,
// NFSMW_BENCHMARK_TIMEOUT=<s> (real seconds to reach the start, 180),
// NFSMW_BENCHMARK_VSYNC=1, NFSMW_BENCHMARK_SERIAL=1 (the game thread waits
// for each frame's encode: the frame is game + build + encode).

#include "Benchmark.h"

#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/Careers/UnlockSystem.hpp"
#include "Speed/Indep/Src/Frontend/Database/VehicleDB.hpp"
#include "Speed/Indep/Src/Frontend/MemoryCard/MemoryCard.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feKeyboardInput.hpp"
#include "Speed/Indep/Src/Frontend/RaceStarter.hpp"
#include "Speed/Indep/Src/Frontend/FEPackageManager.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Safehouse/career/uiCareerMain.hpp"
#include "Speed/Indep/Src/Gameplay/GActivity.h"
#include "Speed/Indep/Src/Generated/Events/EEnterBin.hpp"
#include "Speed/Indep/Src/Interfaces/SimActivities/INIS.h"
#include "Speed/Indep/Src/Animation/AnimDirectory.hpp"
#include "Speed/Indep/Src/Animation/AnimScene.hpp"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GMilestone.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/milestonetypes.h"
#include "Speed/Indep/Src/Lua/LuaGameHooks.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Gameplay/GTrigger.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/ICopMgr.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/IAI.h"
#include "Speed/Indep/Src/Interfaces/Simables/IInput.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Misc/Config.h"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"

#include "../Mac/InputBridge.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern int IsSoundEnabled;
extern int IsMemcardEnabled;
extern const char *SkipFEPlayerCar;
extern int SkipDDayRaces;
extern int SkipCareerIntro;

namespace {

struct Scenario {
    const char *name;
    const char *description;
    bool frontend;      // the main menu instead of a race
    bool free_roam;     // a new career's free roam (from the main menu)
    const char *race;   // event ID (GRaceDatabase::GetRaceFromName)
    const char *car;    // vehicle (CarBuilder key), the SkipFE default otherwise
    int opponents;      // AI racers
    int laps;           // circuits
    int traffic;        // density 0..100
    bool cops;
    float heat;         // player heat at the start (cops chase from 2 up)
    int frames;         // frames recorded once the race is on
    int shots[4];       // screenshot frames (1-based), 0 ends
    bool career_event;  // free roam: warp into an event's trigger and engage it
    bool milestone;     // free roam: a pursuit for the bin's milestones, escaped to the safehouse
    bool career_save;   // profiles on: a career saved to disk, then loaded back (NFSMW_BENCHMARK_PHASE)
    bool rival;         // free roam: the bin's requirements met, the rival challenged and beaten, the next bin
    bool busted;        // free roam: a pursuit, the car stopped until the cops bust it: the arrest NIS
};

// Race IDs are the race database's event IDs (NFSMW_BENCHMARK=list-races
// prints them all).
const Scenario kScenarios[] = {
    {"race", "downtown Rockport circuit (21.1.1), 3 opponents, light traffic, no cops: the renderer and the GPU", false, false, "21.1.1", "bmwm3gtre46", 3,
     3, 10, false, 0.0f, 3600, {600, 1800, 3600}},
    {"forest", "the Rosewood forest circuit (15.1.1), 3 opponents: trees, foliage and long views", false, false, "15.1.1", "bmwm3gtre46", 3, 2, 10, false,
     0.0f, 3600, {600, 1800, 3600}},
    {"rain", "the downtown circuit (21.1.1) in the rain (NFSMW_RAIN): rain particles, wet road", false, false, "21.1.1", "bmwm3gtre46", 3, 3, 10, false,
     0.0f, 3600, {600, 1800, 3600}},
    {"pursuit", "Rosewood sprint (15.2.1), 3 opponents, a heat 4 pursuit, traffic: the CPU (AI, physics) and many cars", false, false, "15.2.1", "bmwm3gtre46", 3, 1, 60,
     true, 4.0f, 3600, {600, 1800, 3600}},
    {"freeroam", "a new career's free roam (skip D-Day: bin 15), the AI driving: traffic, cops, the world's events", false, true, nullptr,
     nullptr, 0, 0, 0, true, 0.0f, 3600, {600, 1800, 3600}},
    {"career-event", "a new career's free roam, the player warped into an event's trigger: engage it, race it (the AI driving), back to free roam", false,
     true, nullptr, nullptr, 0, 0, 0, true, 0.0f, 30000, {0}, true},
    {"career-milestone", "a new career's free roam, a heat 2 pursuit for the bin's milestones, escaped into the safehouse: rewards, safehouse", false,
     true, nullptr, nullptr, 0, 0, 0, true, 0.0f, 30000, {0}, false, true},
    {"career-save", "a new career on a profile: an event won, saved, the game quit; then (NFSMW_BENCHMARK_PHASE=load) the profile loaded and the career checked",
     false, true, nullptr, nullptr, 0, 0, 0, true, 0.0f, 60000, {0}, false, false, true},
    {"career-rival", "a new career's free roam with the bin's requirements met (debug): the rival challenge, the boss race, the rival flow, the next bin",
     false, true, nullptr, nullptr, 0, 0, 0, true, 0.0f, 60000, {0}, false, false, false, true},
    {"career-busted", "a new career's free roam, a heat 2 pursuit, the car stopped until the cops bust it: the arrest NIS, the rap sheet",
     false, true, nullptr, nullptr, 0, 0, 0, true, 0.0f, 30000, {0}, false, false, false, false, true},
    {"frontend", "the main menu over the garage car: car shading and the frontend", true, false, nullptr, nullptr, 0, 0, 0, false, 0.0f, 3600,
     {120, 1800, 3600}},
    {"list-races", "prints the race database's events and exits", true, false, nullptr, nullptr, 0, 0, 0, false, 0.0f, 1, {0}},
    {"write-options", "writes the default video options to $NFSMW_VIDEO_CONFIG and exits", true, false, nullptr, nullptr, 0, 0, 0, false, 0.0f, 0,
     {0}},
};

const Scenario *gScenario = nullptr;
std::string gRace, gCar, gOutDir;
int gFrames = 0;
std::vector<int> gShots;
int gTimeout = 180;
bool gVSync = false;

uint64_t Now() {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

uint64_t gLaunch = 0;

int EnvInt(const char *name, int fallback) {
    const char *v = std::getenv(name);
    return v && *v ? std::atoi(v) : fallback;
}

struct FrameRecord {
    double frame_ms, game_ms, render_ms, present_ms;
    int frame; // 0 before the start
    int cars;  // active vehicles (racers, cops, traffic)
    bool shot;
};

std::vector<FrameRecord> gRecords;
int gBenchFrame = 0; // frames since the start (1-based once started)
bool gStarted = false;
uint64_t gLastEnd = 0;
uint64_t gStartTime = 0;
int gEnterPresses = 0;

void PrintScenarios(FILE *f) {
    for (const Scenario &s : kScenarios) {
        std::fprintf(f, "  %-14s %s\n", s.name, s.description);
    }
}

void ListRaces() {
    GRaceDatabase &db = GRaceDatabase::Get();
    std::printf("[benchmark] %u races (id, type, laps, length m, cops, quick race, time of day)\n", db.GetRaceCount());
    for (unsigned int i = 0; i < db.GetRaceCount(); i++) {
        GRaceParameters *p = db.GetRaceParameters(i);
        if (p == nullptr) {
            continue;
        }
        p->EnsureLoaded();
        std::printf("  %-10s type %2d laps %d len %6.0f cops %d qr %d boss %d tod %.2f\n", p->GetEventID() ? p->GetEventID() : "?",
                    static_cast<int>(p->GetRaceType()), p->GetNumLaps(), p->GetRaceLengthMeters(), p->GetCopsEnabled(),
                    p->GetInitiallyUnlockedQuickRace(), p->GetIsBossRace(), p->GetTimeOfDay());
    }
}

IPlayer *FirstPlayer() {
    const IPlayer::List &players = IPlayer::GetList(PLAYER_LOCAL);
    return players.empty() ? nullptr : *players.begin();
}

[[noreturn]] void Finish();

const int kPursuitFrame = 240; // the countdown is over by then

// The race AI drives the player's car.
void DriveWithAI() {
    IPlayer *player = FirstPlayer();
    ISimable *simable = player ? player->GetSimable() : nullptr;
    if (simable == nullptr) {
        return;
    }
    IHumanAI *human_ai;
    if (simable->QueryInterface(&human_ai) && !human_ai->GetAiControl()) {
        human_ai->SetAiControl(true);
        std::fprintf(stderr, "[benchmark] AI drives the player car (frame %d)\n", gBenchFrame);
    }
    // Cops: a race locks them out for its first minute (kCopLockoutTime);
    // lift that once the race is on and start a pursuit at the scenario's
    // heat (the debug "pursue at heat level", AICopManager::PursueAtHeatLevel).
    if (gBenchFrame == kPursuitFrame && gScenario->heat > 0.0f && ICopMgr::Exists()) {
        ICopMgr::Get()->LockoutCops(false);
        ICopMgr::Get()->PursueAtHeatLevel(static_cast<int>(gScenario->heat));
        std::fprintf(stderr, "[benchmark] pursuit at heat %d (frame %d)\n", static_cast<int>(gScenario->heat), gBenchFrame);
    }
    // Screen effects on cue: NFSMW_BENCHMARK_NOS=<from>-<to> holds the NOS
    // control (IInput::SetControlNOS) over those frames (the radial blur);
    // NFSMW_BENCHMARK_BREAKER=<frame> charges and engages the pursuit
    // breaker then (IPlayer::ChargeGameBreaker, ToggleGameBreaker).
    static int nos_from = -1, nos_to = -1;
    static const int breaker = EnvInt("NFSMW_BENCHMARK_BREAKER", -1);
    if (nos_from < 0) {
        nos_from = 0;
        if (const char *nos = std::getenv("NFSMW_BENCHMARK_NOS")) {
            std::sscanf(nos, "%d-%d", &nos_from, &nos_to);
        }
    }
    if (gBenchFrame >= nos_from && gBenchFrame <= nos_to) {
        IInput *input;
        if (simable->QueryInterface(&input)) {
            input->SetControlNOS(true);
        }
        if (gBenchFrame == nos_from) {
            std::fprintf(stderr, "[benchmark] NOS on (frames %d-%d)\n", nos_from, nos_to);
        }
    }
    if (gBenchFrame == breaker) {
        player->ChargeGameBreaker(1.0f);
        bool on = player->ToggleGameBreaker();
        std::fprintf(stderr, "[benchmark] pursuit breaker %s (frame %d)\n", on ? "on" : "refused", gBenchFrame);
    }
}

// The career's gameplay data (GManager::SaveGameplayData: the persistent
// activity vars, milestones, speed traps, bins, SMS...) saved, loaded back
// and saved again: the two saves must match.
void CheckSaveRoundTrip() {
    static uint8 first[0x4000];
    static uint8 second[0x4000];
    const bool saved = GManager::Get().SaveGameplayData(first, sizeof(first));
    const bool loaded = saved && GManager::Get().LoadGameplayData(first, sizeof(first));
    const bool resaved = loaded && GManager::Get().SaveGameplayData(second, sizeof(second));
    const GManager::SavedGameplayDataHeader *header = reinterpret_cast<const GManager::SavedGameplayDataHeader *>(first);
    std::fprintf(stderr, "[benchmark] gameplay data: save %d load %d save %d, %u persistent blocks, %u milestones, identical %d\n", saved, loaded, resaved,
                 header->mNumPersistent, header->mNumMilestoneRecords, resaved && std::memcmp(first, second, sizeof(first)) == 0);
}

void RivalPostRace(const char *control, bool hud, bool racing, int since);

// The career event scenario: a career race's trigger in the current bin
// (NFSMW_BENCHMARK_RACE picks one by event ID), the player warped into it;
// the engage key (enter) brings up its info dialog, enter accepts it, the AI
// races it, enter gets through the post race screens back to free roam.
enum CareerEventPhase {
    kEvent_FreeRoam,  // waiting to warp
    kEvent_Engaging,  // in the trigger: the prompt, the dialog
    kEvent_Racing,    // the race activity runs
    kEvent_PostRace,  // the race is over: the results screens
    kEvent_BackRoaming,
};

CareerEventPhase gEventPhase = kEvent_FreeRoam;
int gEventPhaseFrame = 0;
bool gEventPress = false;
bool gEventLeft = false; // focus the dialog's Accept (Decline has the focus)
bool gEventHoldCar = false; // in the trigger: the AI is off until the dialog
std::string gLastControlPackage;
GRaceParameters *gEventRace = nullptr;
int gEventStartCash = 0;

const char *ControlPackageName() {
    FEPackage *pkg = cFEng::Get() ? cFEng::Get()->FindPackageWithControl() : nullptr;
    return pkg != nullptr && pkg->GetName() != nullptr ? pkg->GetName() : "";
}

void SetEventPhase(CareerEventPhase phase) {
    static const char *kNames[] = {"free roam", "engaging", "racing", "post race", "back in free roam"};
    gEventPhase = phase;
    gEventPhaseFrame = gBenchFrame;
    std::fprintf(stderr, "[benchmark] career event: %s (frame %d)\n", kNames[phase], gBenchFrame);
}

GTrigger *FindEventTrigger() {
    const int bin = FEDatabase->GetCareerSettings()->GetCurrentBin();
    char prefix[8];
    std::snprintf(prefix, sizeof(prefix), "%d.", bin);
    for (GObjectIterator<GTrigger> it(0xFFFFFFFF); it.IsValid(); it.Advance()) {
        GTrigger *trigger = it.GetInstance();
        GActivity *activity = trigger->GetTargetActivity();
        GRaceParameters *parms = activity ? GRaceDatabase::Get().GetRaceFromActivity(activity) : nullptr;
        if (parms == nullptr || parms->GetEventID() == nullptr || parms->GetIsBossRace() != gScenario->rival || !trigger->IsEnabled()) {
            continue;
        }
        if (!gRace.empty() ? gRace != parms->GetEventID()
                           : (std::strncmp(parms->GetEventID(), prefix, std::strlen(prefix)) != 0 ||
                              !GRaceDatabase::Get().CheckRaceScoreFlags(parms->GetEventHash(), GRaceDatabase::kUnlocked_Career) ||
                              GRaceDatabase::Get().CheckRaceScoreFlags(parms->GetEventHash(), GRaceDatabase::kCompleted_ContextCareer))) {
            continue;
        }
        gEventRace = parms;
        return trigger;
    }
    return nullptr;
}

void SetHumanAI(bool on) {
    IPlayer *player = FirstPlayer();
    ISimable *simable = player ? player->GetSimable() : nullptr;
    IHumanAI *human_ai;
    if (simable != nullptr && simable->QueryInterface(&human_ai) && human_ai->GetAiControl() != on) {
        human_ai->SetAiControl(on);
    }
}

// Free roam: the SMS inbox and messages (GManager::UpdatePendingSMS opens
// them when a text arrives) close with escape a second after they open.
bool gEventEscape = false;
int gEventKey = -1; // a PortKey to hold this frame
int gControlFrame = 0;

void FreeRoamScreens() {
    const char *control = ControlPackageName();
    if (gLastControlPackage != control) {
        gLastControlPackage = control;
        gControlFrame = gBenchFrame;
        std::fprintf(stderr, "[benchmark] control: %s (frame %d)\n", control, gBenchFrame);
    }
    const int in_control = gBenchFrame - gControlFrame;
    gEventEscape = std::strncmp(control, "SMS_", 4) == 0 && in_control >= 60 && in_control % 60 < 6;
}

void CareerEventUpdate() {
    const char *control = ControlPackageName();
    const bool racing = GRaceStatus::Exists() && GRaceStatus::Get().GetPlayMode() == GRaceStatus::kPlayMode_Racing;
    const bool hud = std::strstr(control, "HUD") != nullptr || *control == '\0';
    const bool sms = std::strncmp(control, "SMS_", 4) == 0;
    const int since = gBenchFrame - gEventPhaseFrame;
    gEventPress = false;
    gEventLeft = false;
    switch (gEventPhase) {
        case kEvent_FreeRoam:
            // The rival scenario: once the bin's script unlocked the boss race.
            if (gBenchFrame == (gScenario->rival ? 720 : 120)) {
                GTrigger *trigger = FindEventTrigger();
                UMath::Vector3 pos;
                if (trigger == nullptr) {
                    std::fprintf(stderr, "[benchmark] no engageable career event in bin %d\n", FEDatabase->GetCareerSettings()->GetCurrentBin());
                    std::fflush(nullptr);
                    _exit(4);
                }
                trigger->GetPosition(pos);
                UMath::Vector3 dir = trigger->GetDirection();
                std::fprintf(stderr, "[benchmark] career event %s (type %d) at %.0f %.0f %.0f\n", gEventRace->GetEventID(),
                             static_cast<int>(gEventRace->GetRaceType()), pos.x, pos.y, pos.z);
                SetHumanAI(false);
                gEventHoldCar = true;
                IPlayer *player = FirstPlayer();
                IVehicle *ivehicle;
                if (player && player->GetSimable() && player->GetSimable()->QueryInterface(&ivehicle)) {
                    if (UMath::LengthSquare(dir) < 0.01f) {
                        dir = UMath::Vector3Make(1.0f, 0.0f, 0.0f);
                    }
                    ivehicle->SetVehicleOnGround(pos, dir);
                    ivehicle->SetSpeed(0.0f);
                }
                gEventStartCash = static_cast<int>(FEDatabase->GetCareerSettings()->GetCash());
                SetEventPhase(kEvent_Engaging);
            }
            break;
        case kEvent_Engaging:
            // Enter for 6 frames of every 60 in the trigger: the HUD's engage
            // button. Once the dialog is in and done animating: left (to
            // Accept, Decline has the focus), then enter.
            static int dialog_frame = -1;
            if (std::strcmp(control, "EngageEventDialog.fng") == 0) {
                // The race's goals are set as the accept is delivered (with the
                // sim paused): the AI takes the car back before that.
                gEventHoldCar = false;
                if (dialog_frame < 0) {
                    dialog_frame = gBenchFrame;
                }
                const int in_dialog = gBenchFrame - dialog_frame;
                gEventLeft = in_dialog >= 60 && in_dialog < 66;
                gEventPress = in_dialog >= 90 && in_dialog < 96;
            } else {
                dialog_frame = -1;
                // Not while the race's intro NIS runs: enter skips it.
                gEventPress = since >= 60 && since % 60 < 6 && !INIS::Exists();
            }
            if (racing) {
                SetEventPhase(kEvent_Racing);
            } else if (since > 1800) {
                std::fprintf(stderr, "[benchmark] career event: not engaged after 30 s\n");
                Finish();
            }
            break;
        case kEvent_Racing:
            // The race AI drives (and enter on any screen that isn't the HUD:
            // the post race results)
            gEventPress = !hud && !sms && since % 60 < 6;
            gEventHoldCar = false;
            if (!racing) {
                SetEventPhase(kEvent_PostRace);
            }
            break;
        case kEvent_PostRace:
            gEventPress = !hud && !sms && since % 60 < 6;
            if (gScenario->rival) {
                RivalPostRace(control, hud, racing, since);
                break;
            }
            if (hud && !racing && TheGameFlowManager.IsInGame() && since > 120) {
                std::fprintf(stderr, "[benchmark] career event %s: cash %d -> %d, completed %d\n", gEventRace->GetEventID(), gEventStartCash,
                             static_cast<int>(FEDatabase->GetCareerSettings()->GetCash()),
                             GRaceDatabase::Get().CheckRaceScoreFlags(gEventRace->GetEventHash(), GRaceDatabase::kCompleted_ContextCareer) ? 1 : 0);
                SetEventPhase(kEvent_BackRoaming);
            }
            break;
        case kEvent_BackRoaming:
            if (since >= 600 && !gScenario->career_save) {
                CheckSaveRoundTrip();
                Finish();
            }
            break;
    }
}

// The busted scenario: a heat 2 pursuit from frame 120 (as the milestone
// scenario), at NFSMW_BENCHMARK_BUST (frame 600) the AI lets go and the car
// stops; the cops bust it (MPerpBusted): the arrest NIS (NISListenerActivity,
// a random ArrestM/F scene at an arrest marker near the car), then, in free
// roam, the rap sheet in the safehouse. Logs the NIS's scene and frames, and
// ends 300 frames into the frontend.
void BustedUpdate() {
    static const int bust = EnvInt("NFSMW_BENCHMARK_BUST", 600);
    static int nis_frame = -1;
    static int fe_frame = -1;
    gEventPress = false;
    gEventLeft = false;
    if (gBenchFrame == 120 && ICopMgr::Exists()) {
        ICopMgr::Get()->LockoutCops(false);
        ICopMgr::Get()->PursueAtHeatLevel(2);
        std::fprintf(stderr, "[benchmark] pursuit at heat 2 (frame %d)\n", gBenchFrame);
    }
    if (gBenchFrame == bust) {
        SetHumanAI(false);
        gEventHoldCar = true;
        std::fprintf(stderr, "[benchmark] the car stops (frame %d)\n", gBenchFrame);
    }
    if (gBenchFrame >= bust && nis_frame < 0) {
        IPlayer *player = FirstPlayer();
        IVehicle *ivehicle;
        if (player && player->GetSimable() && player->GetSimable()->QueryInterface(&ivehicle)) {
            ivehicle->SetSpeed(0.0f);
        }
    }
    INIS *nis = INIS::Get();
    if (nis != nullptr && nis_frame < 0) {
        nis_frame = gBenchFrame;
        std::fprintf(stderr, "[benchmark] busted: arrest NIS (frame %d)\n", gBenchFrame);
    }
    static bool playing = false;
    if (nis != nullptr && nis->IsPlaying() && !playing) {
        playing = true;
        char name[32] = "";
        if (TheAnimDirectory != nullptr && nis->GetAnimScene() != nullptr) {
            TheAnimDirectory->GetNameOfSceneHash(nis->GetAnimScene()->GetSceneHash(), name);
        }
        std::fprintf(stderr, "[benchmark] arrest NIS %s playing (frame %d)\n", name, gBenchFrame);
    }
    if (nis == nullptr && playing) {
        playing = false;
        std::fprintf(stderr, "[benchmark] arrest NIS done (frame %d)\n", gBenchFrame);
    }
    if (fe_frame < 0 && TheGameFlowManager.IsInFrontend()) {
        fe_frame = gBenchFrame;
        std::fprintf(stderr, "[benchmark] in the frontend (frame %d)\n", gBenchFrame);
    }
    if (fe_frame >= 0 && gBenchFrame - fe_frame == 300) {
        std::fprintf(stderr, "[benchmark] frontend screen: %s\n", ControlPackageName());
        Finish();
    }
    if (gBenchFrame == bust + 60 * 60 && nis_frame < 0) {
        std::fprintf(stderr, "[benchmark] not busted a minute after stopping\n");
        Finish();
    }
}

// The milestone scenario: a pursuit from frame 120 (the debug "pursue at
// heat level"), the bin's milestones logged as they go; at
// NFSMW_BENCHMARK_EVADE (frame 3600) the pursuit ends the way driving into
// the safehouse while cooling down ends it (IPursuit::EndPursuitEnteringSafehouse):
// evaded, the milestone rewards screen (enter), then the safehouse.
void LogMilestones(const char *when) {
    const unsigned int bin = FEDatabase->GetCareerSettings()->GetCurrentBin();
    std::fprintf(stderr, "[benchmark] milestones (bin %u, %s), bounty %d:\n", bin, when, Game_GetPlayerBounty());
    for (GMilestone *m = GManager::Get().GetFirstMilestone(false, bin); m != nullptr; m = GManager::Get().GetNextMilestone(m, false, bin)) {
        Attrib::Gen::milestonetypes type(m->GetTypeKey(), 0, nullptr);
        const char *name = type.IsValid() ? static_cast<const char *>(type.CollectionName()) : "?";
        std::fprintf(stderr, "[benchmark]   %-26s state %d goal %8.1f value %8.1f best %8.1f recorded %8.1f bounty %.0f\n", name ? name : "?",
                     m->GetIsLocked() ? 1 : m->GetIsAvailable() ? 2 : m->GetIsDonePendingEscape() ? 3 : 4, m->GetRequiredValue(), m->GetCurrentValue(),
                     GManager::Get().GetBestValue(m->GetTypeKey()), m->GetRecordedPassValue(), m->GetBounty());
    }
}

void MilestoneUpdate() {
    static const int evade = EnvInt("NFSMW_BENCHMARK_EVADE", 3600);
    static int fe_frame = -1;
    const char *control = ControlPackageName();
    const bool hud = std::strstr(control, "HUD") != nullptr || *control == '\0';
    const bool sms = std::strncmp(control, "SMS_", 4) == 0;
    gEventPress = false;
    gEventLeft = false;
    if (gBenchFrame == 120) {
        LogMilestones("free roam");
        if (ICopMgr::Exists()) {
            ICopMgr::Get()->LockoutCops(false);
            ICopMgr::Get()->PursueAtHeatLevel(2);
            std::fprintf(stderr, "[benchmark] pursuit at heat 2 (frame %d)\n", gBenchFrame);
        }
    }
    if (gBenchFrame > 120 && gBenchFrame <= evade && gBenchFrame % 600 == 0) {
        LogMilestones("in pursuit");
    }
    if (gBenchFrame == evade) {
        IPlayer *player = FirstPlayer();
        IVehicleAI *ai = nullptr;
        if (player != nullptr && player->GetSimable() != nullptr) {
            player->GetSimable()->QueryInterface(&ai);
        }
        IPursuit *pursuit = ai != nullptr ? ai->GetPursuit() : nullptr;
        std::fprintf(stderr, "[benchmark] %s (frame %d)\n", pursuit ? "pursuit escaped into the safehouse" : "no pursuit to escape", gBenchFrame);
        if (pursuit != nullptr) {
            pursuit->EndPursuitEnteringSafehouse();
        } else {
            Finish();
        }
    }
    if (gBenchFrame > evade) {
        // The milestone rewards and pursuit screens: enter. Then, back in
        // free roam with the pursuit over, into the safehouse: the player is
        // put in its gate, enter engages it (the HUD prompt, MEnterSafeHouse,
        // the menu gate script, Game_DoZoneMenuAction).
        static int roaming_frame = -1;
        static bool at_safehouse = false;
        const bool roaming = hud && !TheGameFlowManager.IsInFrontend() && GRaceStatus::Exists() &&
                             GRaceStatus::Get().GetPlayMode() == GRaceStatus::kPlayMode_Roaming;
        if (roaming && roaming_frame < 0 && gBenchFrame > evade + 120) {
            roaming_frame = gBenchFrame;
        }
        gEventPress = !hud && !sms && gBenchFrame % 60 < 6 && !TheGameFlowManager.IsInFrontend();
        if (roaming_frame >= 0 && !at_safehouse && gBenchFrame - roaming_frame == 120) {
            at_safehouse = true;
            for (GObjectIterator<GTrigger> it(GRuntimeInstance::kFlag_TriggerSafeHouse); it.IsValid(); it.Advance()) {
                UMath::Vector3 pos;
                it.GetInstance()->GetPosition(pos);
                UMath::Vector3 dir = it.GetInstance()->GetDirection();
                if (UMath::LengthSquare(dir) < 0.01f) {
                    dir = UMath::Vector3Make(1.0f, 0.0f, 0.0f);
                }
                SetHumanAI(false);
                gEventHoldCar = true;
                IPlayer *player = FirstPlayer();
                IVehicle *ivehicle;
                if (player && player->GetSimable() && player->GetSimable()->QueryInterface(&ivehicle)) {
                    ivehicle->SetVehicleOnGround(pos, dir);
                    ivehicle->SetSpeed(0.0f);
                }
                std::fprintf(stderr, "[benchmark] into the safehouse gate at %.0f %.0f %.0f (frame %d)\n", pos.x, pos.y, pos.z, gBenchFrame);
                break;
            }
        }
        if (at_safehouse && hud && !TheGameFlowManager.IsInFrontend()) {
            gEventPress = (gBenchFrame - roaming_frame) % 60 < 6 && gBenchFrame - roaming_frame >= 240;
        }
        if (fe_frame < 0 && TheGameFlowManager.IsInFrontend()) {
            fe_frame = gBenchFrame;
            LogMilestones("escaped");
            std::fprintf(stderr, "[benchmark] in the frontend (frame %d)\n", gBenchFrame);
        }
        // In the safehouse: "Resume Free Roam" (the menu's first, focused
        // option) back to the world, where the career's activities pick up
        // from their saved vars.
        static int back_frame = -1;
        if (fe_frame >= 0 && back_frame < 0) {
            const int in_fe = gBenchFrame - fe_frame;
            if (in_fe == 300) {
                std::fprintf(stderr, "[benchmark] frontend screen: %s, resume free roam\n", control);
            }
            gEventPress = in_fe >= 300 && in_fe < 306;
            // "Leave the safe house?" (No has the focus): left, enter.
            if (std::strcmp(control, "Dialog.fng") == 0) {
                const int in_dialog = gBenchFrame - gControlFrame;
                gEventLeft = in_dialog >= 60 && in_dialog < 66;
                gEventPress = in_dialog >= 90 && in_dialog < 96;
            }
            if (!TheGameFlowManager.IsInFrontend() && TheGameFlowManager.IsInGame() && hud && in_fe > 306) {
                back_frame = gBenchFrame;
                std::fprintf(stderr, "[benchmark] back in free roam (frame %d)\n", gBenchFrame);
            }
            if (in_fe > 300 + 60 * 60) {
                std::fprintf(stderr, "[benchmark] no free roam after the safehouse\n");
                Finish();
            }
        }
        // Back in free roam: the world map (M, the HUD's d-pad left) and the
        // rap sheet (B, d-pad down), each closed with escape.
        if (back_frame >= 0) {
            const int since_back = gBenchFrame - back_frame;
            gEventKey = since_back >= 60 && since_back < 66 ? PORT_KEY_A + ('M' - 'A') : since_back >= 300 && since_back < 306 ? PORT_KEY_A + ('B' - 'A') : -1;
            gEventEscape = (since_back >= 200 && since_back < 206) || (since_back >= 440 && since_back < 446);
        }
        if (back_frame >= 0 && gBenchFrame - back_frame == 600) {
            LogMilestones("back in free roam");
            CheckSaveRoundTrip();
            Finish();
        }
    }
}

// The career save scenario (profiles on, NFSMW_SAVE_DIR a scratch folder).
// Phase "save" (the default): the boot's "Create a new alias?" (yes), the
// name typed (BENCH), the profile saved; a new career, an event won as in
// career-event, the autosave the game makes back in free roam; the career's
// state is written to <out>/career-save.state and the game quits. Phase
// "load" (NFSMW_BENCHMARK_PHASE=load): the boot loads the profile, the
// profile saved again in memory must match the file byte for byte, the
// career's state must match the saved one; "Resume Career" (the safehouse,
// "Resume Free Roam") and the state checked again in free roam.
bool gLoadPhase = false;
// Phase "menu-save" (a load phase too): in the safehouse's career menu
// (uiCareerCrib) the career is changed (cash) and saved by hand: the menu's
// Save (CSave::React, MemcardEnter 0x2251) through the memory card screens.
bool gMenuSavePhase = false;
// Profiles on: career-save, or career-rival given NFSMW_SAVE_DIR (the rival
// flow's autosave, MCE_RivalFlow, writes a new profile; the career-save load
// phase checks it).
bool gProfiles = false;
int gMenuSaveStage = 0; // 0 to Save, 1 saving, 2 saved
std::string gTypeQueue;
const char kProfileName[] = "BENCH";
int gStateMismatches = 0;
int gStateChecks = 0;

std::string ProfilePath() {
    const char *dir = std::getenv("NFSMW_SAVE_DIR");
    return std::string(dir ? dir : ".") + "/" + kProfileName + "/" + kProfileName;
}

long ProfileStamp() {
    struct stat st;
    if (stat(ProfilePath().c_str(), &st) != 0) {
        return 0;
    }
    return static_cast<long>(st.st_mtimespec.tv_sec) * 1000000000L + st.st_mtimespec.tv_nsec;
}

uint32 Fnv(const void *data, size_t size) {
    uint32 h = 2166136261u;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ static_cast<const uint8 *>(data)[i]) * 16777619u;
    }
    return h;
}

// The career, one fact per line (what the save holds and the game shows).
std::vector<std::string> CareerState() {
    std::vector<std::string> lines;
    char line[256];
    auto add = [&](const char *fmt, auto... args) {
        std::snprintf(line, sizeof(line), fmt, args...);
        lines.push_back(line);
    };
    UserProfile *profile = FEDatabase->GetUserProfile(0);
    CareerSettings *career = FEDatabase->GetCareerSettings();
    add("profile %s loaded %d", profile->GetProfileName(), FEDatabase->bProfileLoaded ? 1 : 0);
    add("cash %d", career->GetCash());
    add("bin %u", career->GetCurrentBin());
    add("current car 0x%08X", career->GetCurrentCar());
    add("adaptive difficulty %.4f", career->GetAdaptiveDifficulty());
    add("case file %s", career->GetCaseFileName());
    add("career started %d rap sheet %d", career->HasCareerStarted() ? 1 : 0, career->HasRapSheet() ? 1 : 0);
    FEPlayerCarDB *stable = FEDatabase->GetPlayerCarStable(0);
    add("bounty %u evaded %u busted %u", stable->GetTotalBounty(), stable->GetTotalEvadedPursuits(), stable->GetTotalBustedPursuits());
    for (int i = 0; i < 200; i++) {
        FECarRecord *car = stable->GetCarByIndex(i);
        if (car == nullptr || !car->IsValid()) {
            continue;
        }
        FECustomizationRecord *custom = car->IsCustomized() ? stable->GetCustomizationRecordByHandle(car->Customization) : nullptr;
        FECareerRecord *record = car->IsCareer() ? stable->GetCareerRecordByHandle(car->CareerHandle) : nullptr;
        add("car %d handle 0x%08X key 0x%08X filter 0x%08X custom %08X career bounty %u heat %.3f", i, car->Handle, car->FEKey, car->FilterBits,
            custom ? Fnv(custom, sizeof(*custom)) : 0, record ? record->GetBounty() : 0, record ? record->VehicleHeat : 0.0f);
    }
    if (GRaceDatabase::Exists()) {
        GRaceSaveInfo *info = GRaceDatabase::Get().GetScoreInfo();
        for (unsigned int i = 0; i < GRaceDatabase::Get().GetScoreInfoCount(); i++) {
            if (info[i].mRaceHash == 0 || info[i].mFlags == 0) {
                continue;
            }
            GRaceParameters *parms = GRaceDatabase::Get().GetRaceFromHash(info[i].mRaceHash);
            add("race %s flags 0x%X scores %08X", parms && parms->GetEventID() ? parms->GetEventID() : "?", info[i].mFlags,
                Fnv(&info[i].mHighScores, sizeof(info[i]) - 8));
        }
    }
    for (unsigned int i = 0; i < 150; i++) {
        SMSMessage *sms = career->GetSMSMessage(i);
        if (sms != nullptr && sms->GetFlags() != 0) {
            add("sms %u flags 0x%X order %u", i, sms->GetFlags(), sms->GetSortOrder());
        }
    }
    add("unlocks %08X", Fnv(TheUnlockData, sizeof(UnlockDatum) * 0x39));
    static char markers[0x1000];
    char *end = TheFEMarkerManager.SaveToBuffer(markers);
    add("markers %d temp %d data %08X", TheFEMarkerManager.GetNumCustomizeMarkers(), TheFEMarkerManager.GetNumTempMarkers(),
        Fnv(markers, end - markers));
    const unsigned int bin = career->GetCurrentBin();
    for (GMilestone *m = GManager::Get().GetFirstMilestone(false, bin); m != nullptr; m = GManager::Get().GetNextMilestone(m, false, bin)) {
        Attrib::Gen::milestonetypes type(m->GetTypeKey(), 0, nullptr);
        const char *name = type.IsValid() ? static_cast<const char *>(type.CollectionName()) : "?";
        add("milestone %s state %d goal %.1f recorded %.1f bounty %.0f", name ? name : "?",
            m->GetIsLocked() ? 1 : m->GetIsAvailable() ? 2 : m->GetIsDonePendingEscape() ? 3 : 4, m->GetRequiredValue(), m->GetRecordedPassValue(),
            m->GetBounty());
    }
    return lines;
}

std::string StatePath() {
    return gOutDir + "/career-save.state";
}

void WriteCareerState() {
    FILE *f = std::fopen(StatePath().c_str(), "w");
    std::vector<std::string> lines = CareerState();
    for (const std::string &l : lines) {
        if (f) {
            std::fprintf(f, "%s\n", l.c_str());
        }
        std::fprintf(stderr, "[benchmark]   %s\n", l.c_str());
    }
    if (f) {
        std::fclose(f);
    }
    std::fprintf(stderr, "[benchmark] career state (%zu lines) -> %s\n", lines.size(), StatePath().c_str());
}

// The career now against the saved one: every line must match.
void CheckCareerState(const char *when) {
    std::vector<std::string> saved;
    if (FILE *f = std::fopen(StatePath().c_str(), "r")) {
        char buf[512];
        while (std::fgets(buf, sizeof(buf), f)) {
            buf[std::strcspn(buf, "\n")] = '\0';
            saved.push_back(buf);
        }
        std::fclose(f);
    }
    std::vector<std::string> now = CareerState();
    int mismatches = 0;
    for (const std::string &l : saved) {
        if (std::find(now.begin(), now.end(), l) == now.end()) {
            std::fprintf(stderr, "[benchmark]   saved only: %s\n", l.c_str());
            mismatches++;
        }
    }
    for (const std::string &l : now) {
        if (std::find(saved.begin(), saved.end(), l) == saved.end()) {
            std::fprintf(stderr, "[benchmark]   now only:   %s\n", l.c_str());
            mismatches++;
        }
    }
    gStateChecks++;
    gStateMismatches += mismatches;
    std::fprintf(stderr, "[benchmark] career state %s: %zu saved lines, %d mismatches%s\n", when, saved.size(), mismatches,
                 saved.empty() ? " (no saved state: run the save phase first)" : "");
}

// The profile as the game would save it now against the file it loaded:
// byte for byte (the file's body follows the realmc header and the 8-byte
// memory card header).
void CheckProfileResave() {
    const int size = FEDatabase->GetUserProfileSaveSize(false);
    std::vector<char> now(size), file(size);
    FEDatabase->SaveUserProfileToBuffer(now.data(), size);
    FILE *f = std::fopen(ProfilePath().c_str(), "rb");
    bool read = f && std::fseek(f, 0x1C + 8, SEEK_SET) == 0 && std::fread(file.data(), 1, size, f) == static_cast<size_t>(size);
    if (f) {
        std::fclose(f);
    }
    const int career_end = 0x10 + FEDatabase->GetCareerSettings()->GetSaveBufferSize(false);
    int diffs = 0;
    for (int i = 0; read && i < size; i++) {
        if (now[i] == file[i]) {
            continue;
        }
        int j = i;
        while (j + 1 < size && (now[j + 1] != file[j + 1] || (j + 2 < size && now[j + 2] != file[j + 2]))) {
            j++;
        }
        if (diffs++ < 16) {
            std::fprintf(stderr, "[benchmark]   profile differs at 0x%X..0x%X (%s +0x%X)\n", i, j,
                         i < 0x10 ? "version" : i < 0x4010 ? "gameplay data" : i < career_end ? "career" : "profile",
                         i < 0x4010 ? i - 0x10 : i < career_end ? i - 0x4010 : i - career_end);
        }
        i = j;
    }
    gStateChecks++;
    gStateMismatches += read ? diffs : 1;
    std::fprintf(stderr, "[benchmark] profile saved again: %s (%d bytes, %d differing ranges)\n", !read ? "no profile file" : diffs ? "differs" : "identical",
                 size, diffs);
}

// Memory card screens: enter (after left: "yes", button 1) on each
// prompt; the profile name typed when the name entry is up.
void MemcardScreens(int frame) {
    const char *control = ControlPackageName();
    static std::string last;
    static int since = 0;
    if (last != control) {
        last = control;
        since = frame;
    }
    if (std::strstr(control, "MC_") == nullptr) {
        return;
    }
    gEventPress = false;
    gEventLeft = false;
    static bool typed = false;
    if (gKeyboardManager.IsCapturing()) {
        if (!typed) {
            typed = true;
            // The field starts with the default name: backspace it away.
            gTypeQueue = std::string(16, '\b') + kProfileName + "\r";
            std::fprintf(stderr, "[benchmark] profile name: %s (frame %d)\n", kProfileName, frame);
        }
        since = frame;
        return;
    }
    const int t = frame - since;
    gEventLeft = t >= 60 && t % 120 >= 60 && t % 120 < 66;
    gEventPress = t >= 60 && t % 120 >= 90 && t % 120 < 96;
}

// Load phase, from the main menu: enter on "Career", "Resume Career" (the
// career menu, CResumeCareer::React) and "Resume Free Roam" (the safehouse),
// each the focused first option; "Leave the safe house?": left (yes), enter.
// The career menu's option list (IconScrollerMenu::Options is protected).
struct CareerCribAccess : uiCareerCrib {
    static IconOption *CurrentOption(uiCareerCrib *crib) {
        return (crib->*(&CareerCribAccess::Options)).GetCurrentOption();
    }
};

// Menu-save phase, in the career menu: the cash changed once, then right
// until the focus is on Save, enter; once saved, left back to "Resume Free
// Roam". Returns true while it drives the menu.
// Once the profile file changes: the profile saved again in memory must
// match it, and the career's state is written (the next load checks it).
bool MenuSave(int frame, const char *control, int t) {
    int &stage = gMenuSaveStage;
    static long stamp = 0;
    if (stage == 1 && ProfileStamp() != stamp) {
        stage = 2;
        std::fprintf(stderr, "[benchmark] menu save: profile written (frame %d, control %s)\n", frame, control);
        CheckProfileResave();
        WriteCareerState();
    }
    if (stage == 1) {
        return true;
    }
    uiCareerCrib *crib = dynamic_cast<uiCareerCrib *>(FEngFindScreen(control));
    if (crib == nullptr) {
        return false;
    }
    gEventKey = -1;
    if (stage == 2) {
        // Saved: back left to "Resume Free Roam" (the first option); the
        // load phase's presses take it from there.
        if (dynamic_cast<CResumeFreeRoam *>(CareerCribAccess::CurrentOption(crib)) != nullptr) {
            return false;
        }
        gEventKey = t % 30 < 3 ? PORT_KEY_LEFT : -1;
        return true;
    }
    static bool changed = false;
    if (!changed) {
        changed = true;
        FEDatabase->GetCareerSettings()->AwardCash(12345);
        std::fprintf(stderr, "[benchmark] menu save: cash now %d (frame %d)\n", FEDatabase->GetCareerSettings()->GetCash(), frame);
    }
    if (t < 60) {
        return true;
    }
    // Save is the last option: right until it has the focus.
    if (dynamic_cast<CSave *>(CareerCribAccess::CurrentOption(crib)) == nullptr) {
        gEventKey = t % 30 < 3 ? PORT_KEY_RIGHT : -1;
        return true;
    }
    gEventPress = t % 60 >= 30 && t % 60 < 36;
    if (gEventPress) {
        stage = 1;
        stamp = ProfileStamp();
        std::fprintf(stderr, "[benchmark] menu save: Save pressed (frame %d)\n", frame);
    }
    return true;
}

void CareerSaveBoot(int frame, bool menus) {
    MemcardScreens(frame);
    if (gMenuSavePhase && menus && std::strstr(ControlPackageName(), "MC_") != nullptr) {
        MenuSave(frame, ControlPackageName(), 0);
    }
    if (!menus || std::strstr(ControlPackageName(), "MC_") != nullptr || !TheGameFlowManager.IsInFrontend()) {
        return;
    }
    const char *control = ControlPackageName();
    static std::string last;
    static int since = 0;
    if (last != control) {
        last = control;
        since = frame;
        std::fprintf(stderr, "[benchmark] menu: %s (frame %d)\n", control, frame);
    }
    const int t = frame - since;
    if (gMenuSavePhase) {
        gEventLeft = false;
        gEventPress = false;
        if (MenuSave(frame, control, t)) {
            return;
        }
    }
    const bool dialog = std::strcmp(control, "Dialog.fng") == 0;
    gEventLeft = dialog && t % 120 >= 60 && t % 120 < 66;
    gEventPress = t >= 60 && t % 120 >= 90 && t % 120 < 96;
}

void CareerSaveUpdate() {
    static long boot_stamp = -1;
    if (boot_stamp < 0) {
        boot_stamp = ProfileStamp();
    }
    if (gLoadPhase) {
        static int roaming_frame = -1;
        const bool hud = std::strstr(ControlPackageName(), "HUD") != nullptr || *ControlPackageName() == '\0';
        if (roaming_frame < 0 && hud && GRaceStatus::Exists() && GRaceStatus::Get().GetPlayMode() == GRaceStatus::kPlayMode_Roaming) {
            roaming_frame = gBenchFrame;
        }
        if (roaming_frame >= 0 && gBenchFrame - roaming_frame == 300) {
            CheckCareerState("in free roam");
            CheckSaveRoundTrip();
            if (gMenuSavePhase && gMenuSaveStage != 2) {
                std::fprintf(stderr, "[benchmark] menu save: the career menu's Save never wrote the profile\n");
                gStateMismatches++;
            }
            std::fprintf(stderr, "[benchmark] career-save load: %d checks, %d mismatches: %s\n", gStateChecks, gStateMismatches,
                         gStateMismatches == 0 ? "PASS" : "FAIL");
            Finish();
        }
        return;
    }
    // Save phase: the career event runs (CareerEventUpdate); back in free
    // roam, the autosave.
    static long race_stamp = -1;
    if (gEventPhase == kEvent_Racing && race_stamp < 0) {
        race_stamp = ProfileStamp();
        std::fprintf(stderr, "[benchmark] profile %s before the race\n", race_stamp != boot_stamp ? "autosaved" : "not saved");
    }
    if (race_stamp < 0) {
        return;
    }
    // The state as the save has it: taken the frame the file is written
    // (the save is synchronous in the benchmark).
    static int saved_frame = -1;
    if (saved_frame < 0 && ProfileStamp() != race_stamp) {
        saved_frame = gBenchFrame;
        std::fprintf(stderr, "[benchmark] autosaved after the race (frame %d, control %s)\n", gBenchFrame, ControlPackageName());
        WriteCareerState();
    }
    if (gEventPhase != kEvent_BackRoaming) {
        return;
    }
    const int since = gBenchFrame - gEventPhaseFrame;
    if (saved_frame >= 0 && since == 300) {
        CheckSaveRoundTrip();
        Finish();
    }
    if (saved_frame < 0 && since == 1800) {
        std::fprintf(stderr, "[benchmark] no autosave 30 s into free roam\n");
        Finish();
    }
}

// career-rival with profiles: the memory card screens (the rival flow's
// autosave prompts) and, each time the profile file is written, the career's
// state as that save has it (career-save's load phase checks the last one).
void RivalProfileUpdate() {
    MemcardScreens(gBenchFrame);
    static long stamp = -1;
    const long now = ProfileStamp();
    if (stamp < 0) {
        stamp = now;
        return;
    }
    if (now != stamp) {
        stamp = now;
        std::fprintf(stderr, "[benchmark] profile written (frame %d, control %s, bin %u)\n", gBenchFrame, ControlPackageName(),
                     FEDatabase->GetCareerSettings()->GetCurrentBin());
        CheckProfileResave();
        WriteCareerState();
    }
}

// The rival scenario. At frame 60 the bin's requirements are met the way
// playing would meet them (debug switches, each defaulting to the bin's
// requirement): NFSMW_DEBUG_RACES_WON=<n> of its world races won,
// NFSMW_DEBUG_CHALLENGES=<n> of its milestones completed (awarded, with
// their bounty, GMilestone::DebugForceComplete), NFSMW_DEBUG_BOUNTY=<n>
// bounty in all (the rest added to the career car).
void LogBin(const char *when) {
    const int bin_number = FEDatabase->GetCareerSettings()->GetCurrentBin();
    GRaceBin *bin = GRaceDatabase::Get().GetBinNumber(bin_number);
    if (bin == nullptr) {
        std::fprintf(stderr, "[benchmark] no bin %d\n", bin_number);
        return;
    }
    std::fprintf(stderr, "[benchmark] bin %d (%s, frame %d): races won %d/%d, challenges %d/%d, bounty %u/%d\n", bin_number, when, gBenchFrame,
                 bin->GetAwardedRaceWins(), bin->GetRequiredRaceWins(), bin->GetCompletedChallenges(), bin->GetRequiredChallenges(),
                 FEDatabase->GetPlayerCarStable(0)->GetTotalBounty(), bin->GetRequiredBounty());
    for (unsigned int i = 0; i < bin->GetBossRaceCount(); i++) {
        GRaceParameters *race = GRaceDatabase::Get().GetRaceFromHash(bin->GetBossRaceHash(i));
        std::fprintf(stderr, "[benchmark]   boss race %s: unlocked %d completed %d\n", race && race->GetEventID() ? race->GetEventID() : "?",
                     GRaceDatabase::Get().CheckRaceScoreFlags(bin->GetBossRaceHash(i), GRaceDatabase::kUnlocked_Career) ? 1 : 0,
                     GRaceDatabase::Get().CheckRaceScoreFlags(bin->GetBossRaceHash(i), GRaceDatabase::kCompleted_ContextCareer) ? 1 : 0);
    }
}

void MeetBinRequirements() {
    const int bin_number = FEDatabase->GetCareerSettings()->GetCurrentBin();
    GRaceBin *bin = GRaceDatabase::Get().GetBinNumber(bin_number);
    if (bin == nullptr) {
        return;
    }
    LogBin("before");
    int races = EnvInt("NFSMW_DEBUG_RACES_WON", bin->GetRequiredRaceWins());
    for (unsigned int i = 0; i < bin->GetWorldRaceCount() && races > 0; i++) {
        GRaceSaveInfo *info = GRaceDatabase::Get().GetScoreInfo(bin->GetWorldRaceHash(i));
        if (info != nullptr) {
            info->mFlags |= GRaceDatabase::kCompleted_ContextCareer | GRaceDatabase::kUnlocked_Career;
            races--;
        }
    }
    bin->RefreshProgress();
    int challenges = EnvInt("NFSMW_DEBUG_CHALLENGES", bin->GetRequiredChallenges()) - bin->GetCompletedChallenges();
    for (GMilestone *m = GManager::Get().GetFirstMilestone(false, bin_number); m != nullptr && challenges > 0;
         m = GManager::Get().GetNextMilestone(m, false, bin_number)) {
        if (!m->GetIsAwarded()) {
            m->DebugForceComplete();
            challenges--;
        }
    }
    const int bounty = EnvInt("NFSMW_DEBUG_BOUNTY", bin->GetRequiredBounty()) - static_cast<int>(FEDatabase->GetPlayerCarStable(0)->GetTotalBounty());
    if (bounty > 0) {
        Game_AwardPlayerBounty(bounty);
    }
    GManager::Get().RefreshEngageTriggerIcons();
    LogBin("requirements met");
}

// After a boss race: the next one (racing again), or the rival beaten: the
// post race screens, the rival flow in the frontend (enter on each screen,
// left and enter on a yes/no dialog), back into free roam in the next bin.
void RivalPostRace(const char *control, bool hud, bool racing, int since) {
    static const int start_bin = FEDatabase->GetCareerSettings()->GetCurrentBin();
    static int frontend_frame = -1;
    if (racing) {
        LogBin("next boss race");
        SetEventPhase(kEvent_Racing);
        return;
    }
    const bool in_frontend = TheGameFlowManager.IsInFrontend();
    if (in_frontend && frontend_frame < 0) {
        frontend_frame = gBenchFrame;
        std::fprintf(stderr, "[benchmark] rival flow: frontend, bin %u (frame %d)\n", FEDatabase->GetCareerSettings()->GetCurrentBin(), gBenchFrame);
        for (int i = 0; i < 6; i++) {
            FEMarkerManager::ePossibleMarker marker = FEMarkerManager::MARKER_NONE;
            int param = 0;
            TheFEMarkerManager.GetMarkerForLaterSelection(i, marker, param);
            if (marker != FEMarkerManager::MARKER_NONE) {
                std::fprintf(stderr, "[benchmark]   marker to select: %d (0x%X)\n", static_cast<int>(marker), param);
            }
        }
    }
    const int in_control = gBenchFrame - gControlFrame;
    const bool dialog = std::strstr(control, "Dialog") != nullptr;
    const bool sms = std::strncmp(control, "SMS_", 4) == 0;
    gEventLeft = dialog && in_control % 120 >= 60 && in_control % 120 < 66;
    gEventPress = !hud && !sms && in_control >= 90 && in_control % 120 >= 90 && in_control % 120 < 96;
    // The marker selection: the first (focused) card, then right to the next.
    const bool markers = std::strcmp(control, "SafehouseMarkers.fng") == 0;
    // Its intro ("You can now select 2 markers") plays first; then enter
    // and right in turn: a card, the next card, a card, leave.
    if (markers) {
        const int t = in_control - 480;
        gEventPress = t >= 0 && t % 120 < 6;
        gEventKey = t >= 0 && t % 120 >= 60 && t % 120 < 66 ? PORT_KEY_RIGHT : -1;
    } else {
        gEventKey = -1;
    }
    if (!in_frontend && hud && TheGameFlowManager.IsInGame() && GRaceStatus::Exists() &&
        GRaceStatus::Get().GetPlayMode() == GRaceStatus::kPlayMode_Roaming && FEDatabase->GetCareerSettings()->GetCurrentBin() != start_bin) {
        std::fprintf(stderr, "[benchmark] rival beaten: free roam in bin %u (from %d, frame %d)\n", FEDatabase->GetCareerSettings()->GetCurrentBin(),
                     start_bin, gBenchFrame);
        LogBin("next bin");
        FEPlayerCarDB *stable = FEDatabase->GetPlayerCarStable(0);
        for (int i = 0; i < 200; i++) {
            FECarRecord *car = stable->GetCarByIndex(i);
            if (car != nullptr && car->IsValid() && (car->FilterBits & FE_CAR_FILTER_PINKSLIP) != 0) {
                std::fprintf(stderr, "[benchmark]   pink slip car: handle 0x%08X key 0x%08X (%s)\n", car->Handle, car->FEKey, car->GetDebugName());
            }
        }
        std::fprintf(stderr, "[benchmark]   cash %d, customize markers %d\n", FEDatabase->GetCareerSettings()->GetCash(),
                     TheFEMarkerManager.GetNumCustomizeMarkers());
        SetEventPhase(kEvent_BackRoaming);
    }
    if (since > 60 * 600) {
        std::fprintf(stderr, "[benchmark] rival flow: not back in free roam after 10 minutes\n");
        Finish();
    }
}

void RivalUpdate() {
    if (gBenchFrame == 60) {
        MeetBinRequirements();
    }
    if (gBenchFrame % 600 == 0) {
        LogBin("free roam");
    }
}

bool MainMenuUp() {
    return TheGameFlowManager.IsInFrontend() && cFEng::Get() != nullptr && cFEng::Get()->IsPackageInControl("MainMenu.fng");
}

bool StartReached() {
    if (gScenario->frontend) {
        return MainMenuUp();
    }
    return TheGameFlowManager.IsInGame();
}

double Percentile(std::vector<double> v, double fraction) {
    if (v.empty()) {
        return 0;
    }
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(v.size() * fraction))];
}

[[noreturn]] void Finish() {
    PortMacBenchmarkFinish();
    std::string path = gOutDir + "/" + gScenario->name + ".csv";
    FILE *f = std::fopen(path.c_str(), "w");
    std::vector<double> frame_ms, gpu_ms;
    int max_cars = 0;
    if (f) {
        std::fprintf(f, "# scenario %s race %s car %s frames %d\n", gScenario->name, gRace.c_str(), gCar.c_str(), gFrames);
        std::fprintf(f, "frame,frame_ms,game_ms,render_ms,present_ms,encode_ms,gpu_ms,cars,screenshot\n");
    }
    for (const FrameRecord &r : gRecords) {
        if (r.frame <= 0) {
            continue;
        }
        double gpu = 0, encode = 0;
        PortMacBenchmarkGPUTime(r.frame, &gpu);
        PortMacBenchmarkEncodeTime(r.frame, &encode);
        // The first frame's wall time includes the load before it.
        if (r.frame > 1) {
            frame_ms.push_back(r.frame_ms);
            gpu_ms.push_back(gpu);
        }
        max_cars = std::max(max_cars, r.cars);
        if (f) {
            std::fprintf(f, "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d\n", r.frame, r.frame_ms, r.game_ms, r.render_ms, r.present_ms, encode,
                         gpu, r.cars,
                         r.shot ? 1 : 0);
        }
    }
    if (f) {
        std::fclose(f);
    }
    double total = 0;
    for (double ms : frame_ms) {
        total += ms;
    }
    std::printf("[benchmark] %s: %d frames in %.1f s, median %.3f ms p90 %.3f p99 %.3f, avg %.1f fps, gpu median %.3f ms, up to %d cars -> %s\n",
                gScenario->name,
                gBenchFrame, total / 1000.0, Percentile(frame_ms, 0.5), Percentile(frame_ms, 0.9), Percentile(frame_ms, 0.99),
                total > 0 ? frame_ms.size() / (total / 1000.0) : 0.0, Percentile(gpu_ms, 0.5), max_cars, path.c_str());
    std::fflush(nullptr);
    _exit(0);
}

} // namespace

void PortBenchmarkConfigure(int argc, char **argv) {
    const char *name = std::getenv("NFSMW_BENCHMARK");
    for (int i = 1; i + 1 < argc; i++) {
        if (std::strcmp(argv[i], "--benchmark") == 0) {
            name = argv[i + 1];
        }
    }
    if (name == nullptr || *name == '\0') {
        return;
    }
    for (const Scenario &s : kScenarios) {
        if (std::strcmp(s.name, name) == 0) {
            gScenario = &s;
        }
    }
    if (gScenario == nullptr) {
        std::fprintf(stderr, "[benchmark] unknown scenario '%s', there are:\n", name);
        PrintScenarios(stderr);
        std::exit(2);
    }
    gLaunch = Now();
    if (std::strcmp(gScenario->name, "rain") == 0) {
        setenv("NFSMW_RAIN", "1", 1); // RainNative.cpp's AlwaysRain
    }
    gRace = std::getenv("NFSMW_BENCHMARK_RACE") ? std::getenv("NFSMW_BENCHMARK_RACE") : (gScenario->race ? gScenario->race : "");
    gCar = std::getenv("NFSMW_BENCHMARK_CAR") ? std::getenv("NFSMW_BENCHMARK_CAR") : (gScenario->car ? gScenario->car : "");
    gFrames = EnvInt("NFSMW_BENCHMARK_FRAMES", gScenario->frames);
    gTimeout = EnvInt("NFSMW_BENCHMARK_TIMEOUT", 180);
    gVSync = EnvInt("NFSMW_BENCHMARK_VSYNC", 0) != 0;
    if (const char *shots = std::getenv("NFSMW_BENCHMARK_SHOTS")) {
        for (const char *p = shots; *p;) {
            int f = std::atoi(p);
            if (f > 0) {
                gShots.push_back(f);
            }
            const char *comma = std::strchr(p, ',');
            p = comma ? comma + 1 : p + std::strlen(p);
        }
    } else {
        for (int f : gScenario->shots) {
            if (f > 0 && f <= gFrames) {
                gShots.push_back(f);
            }
        }
    }
    if (const char *out = std::getenv("NFSMW_BENCHMARK_OUT")) {
        gOutDir = out;
    } else {
        char cwd[1024];
        gOutDir = getcwd(cwd, sizeof(cwd)) ? cwd : ".";
    }
    std::fprintf(stderr, "[benchmark] scenario %s: %d frames, out %s\n", gScenario->name, gFrames, gOutDir.c_str());
    if (gScenario->rival && std::getenv("NFSMW_SAVE_DIR") && *std::getenv("NFSMW_SAVE_DIR")) {
        gProfiles = true;
        if (ProfileStamp() != 0) {
            std::fprintf(stderr, "[benchmark] career-rival with profiles: %s exists; it starts without it (delete it)\n", ProfilePath().c_str());
            std::exit(2);
        }
    }
    if (gScenario->career_save) {
        gProfiles = true;
        // Never the player's own profiles: a scratch folder, and the save
        // phase starts from one without the benchmark's profile.
        const char *dir = std::getenv("NFSMW_SAVE_DIR");
        const char *phase = std::getenv("NFSMW_BENCHMARK_PHASE");
        const bool load = phase != nullptr && (std::strcmp(phase, "load") == 0 || std::strcmp(phase, "menu-save") == 0);
        if (dir == nullptr || *dir == '\0') {
            std::fprintf(stderr, "[benchmark] career-save needs NFSMW_SAVE_DIR=<scratch folder>\n");
            std::exit(2);
        }
        if (!load && ProfileStamp() != 0) {
            std::fprintf(stderr, "[benchmark] career-save: %s exists; the save phase starts without it (delete it)\n", ProfilePath().c_str());
            std::exit(2);
        }
        if (load && ProfileStamp() == 0) {
            std::fprintf(stderr, "[benchmark] career-save load: no %s (run the save phase first)\n", ProfilePath().c_str());
            std::exit(2);
        }
    }
}

bool PortBenchmarkActive() {
    return gScenario != nullptr;
}

void PortBenchmarkInit() {
    if (gScenario == nullptr) {
        return;
    }
    IsSoundEnabled = 0; // audio runs on real time
    if (!gProfiles) {
        IsMemcardEnabled = 0;
    }
    if (gScenario->frontend) {
        return;
    }
    if (gScenario->free_roam) {
        SkipDDayRaces = 1;
        SkipCareerIntro = 1;
        // The race AI is slow on some of the career's tracks: finish the event
        // a minute in, first (GRaceStatus::SkipToEndOfRaceInPlace, through
        // GManager::Update's NFSMW_DEBUG_SKIP_RACE), unless told otherwise
        // (NFSMW_DEBUG_SKIP_RACE=0 races it out).
        const char *phase = std::getenv("NFSMW_BENCHMARK_PHASE");
        gMenuSavePhase = gScenario->career_save && phase && std::strcmp(phase, "menu-save") == 0;
        gLoadPhase = gScenario->career_save && phase && (std::strcmp(phase, "load") == 0 || gMenuSavePhase);
        if (gScenario->career_event || gScenario->rival || (gScenario->career_save && !gLoadPhase)) {
            setenv("NFSMW_DEBUG_SKIP_RACE", "1:60", 0);
            if (std::strcmp(std::getenv("NFSMW_DEBUG_SKIP_RACE"), "0") == 0) {
                unsetenv("NFSMW_DEBUG_SKIP_RACE");
            }
        }
        return;
    }
    SkipFE = 1;
    std::strncpy(SkipFERaceID, gRace.c_str(), 15);
    SkipFERaceID[15] = '\0';
    static std::string car = gCar;
    if (!car.empty()) {
        SkipFEPlayerCar = car.c_str();
    }
    SkipFENumAICars = EnvInt("NFSMW_BENCHMARK_OPPONENTS", gScenario->opponents);
    SkipFENumLaps = gScenario->laps;
    SkipFETrafficDensity = EnvInt("NFSMW_BENCHMARK_TRAFFIC", gScenario->traffic);
    SkipFEDisableTraffic = SkipFETrafficDensity == 0;
    SkipFEDisableCops = gScenario->cops ? 0 : 1;
    SkipFEMaxCops = gScenario->cops ? 8 : 0;
}

// QuickGame::HandleSkipFEOptions makes the SkipFE race the startup race, but
// only once the QuickGame is loading its players, after its constructor took
// the start position from World_DEBUGStartLocation: without a startup race
// that's a debug spot far from the race's grid. Do the same before the track
// loads (the race database is up after InitializeEverything).
void PortBenchmarkSetStartupRace() {
    if (gScenario == nullptr || !SkipFE || SkipFERaceID[0] == '\0' || GRaceDatabase::Get().GetStartupRace()) {
        return;
    }
    GRaceParameters *parms = GRaceDatabase::Get().GetRaceFromName(SkipFERaceID);
    if (parms == nullptr) {
        std::fprintf(stderr, "[benchmark] no race '%s' (NFSMW_BENCHMARK=list-races lists them)\n", SkipFERaceID);
        std::fflush(nullptr);
        _exit(2);
    }
    parms->BlockUntilLoaded();
    GRaceCustom *race = GRaceDatabase::Get().AllocCustomRace(parms);
    race->SetReversed(SkipFETrackDirection == eDIRECTION_BACKWARD);
    race->SetNumLaps(parms->GetIsLoopingRace() ? SkipFENumLaps : 1);
    race->SetTrafficDensity(SkipFEDisableTraffic ? 0 : SkipFETrafficDensity);
    race->SetNumOpponents(SkipFENumAICars);
    race->SetDifficulty(static_cast<GRace::Difficulty>(SkipFEDifficulty));
    if (SkipFEDisableCops) {
        race->SetCopsEnabled(false);
    }
    FEDatabase->RaceMode = race->GetRaceType();
    GRaceDatabase::Get().SetStartupRace(race, GRace::kRaceContext_QuickRace);
    GRaceDatabase::Get().FreeCustomRace(race);
}

float PortBenchmarkFixedFrameMs() {
    return gScenario ? 1000.0f / 60.0f : 0.0f;
}

bool PortBenchmarkVSync(bool requested) {
    return gScenario ? gVSync : requested;
}

// The scripted input: nothing but the presses that get the frontend scenario
// past the splash screen. Returns false when the benchmark is off.
bool PortBenchmarkInput(PortInputState *state) {
    if (gScenario == nullptr) {
        return false;
    }
    std::memset(state, 0, sizeof(*state));
    state->mouse_x = -100.0f; // off the screen, over no widget
    state->mouse_y = -100.0f;
    if ((gScenario->frontend || gScenario->free_roam) && !gStarted && gEnterPresses > 0) {
        state->keys[PORT_KEY_ENTER] = 1.0f;
    }
    if (gEventPress) {
        state->keys[PORT_KEY_ENTER] = 1.0f;
    }
    if (gEventLeft) {
        state->keys[PORT_KEY_LEFT] = 1.0f;
    }
    if (gEventEscape) {
        state->keys[PORT_KEY_ESCAPE] = 1.0f;
    }
    if (gEventKey >= 0 && gEventKey < PORT_KEY_COUNT) {
        state->keys[gEventKey] = 1.0f;
    }
    return true;
}

int PortBenchmarkTakeChars(int *out, int max) {
    int n = std::min(max, static_cast<int>(gTypeQueue.size()));
    for (int i = 0; i < n; i++) {
        out[i] = static_cast<unsigned char>(gTypeQueue[i]);
    }
    gTypeQueue.erase(0, n);
    return n;
}

void PortBenchmarkEndFrame(uint64_t render_start_ns) {
    if (gScenario == nullptr) {
        return;
    }
    static int frame = 0;
    frame++;
    if (std::strcmp(gScenario->name, "write-options") == 0) {
        std::fflush(nullptr);
        _exit(0); // eInitEnginePlat has written them by the first frame
    }
    if (std::strcmp(gScenario->name, "list-races") == 0 && GRaceDatabase::Exists() && TheGameFlowManager.IsInFrontend()) {
        ListRaces();
        std::fflush(nullptr);
        _exit(0);
    }
    if (!gStarted && StartReached()) {
        gStarted = true;
        gStartTime = Now();
        std::fprintf(stderr, "[benchmark] start at frame %d, %.1f s after launch\n", frame, (gStartTime - gLaunch) / 1e9);
    }
    if (!gStarted && gProfiles) {
        const char *control = ControlPackageName();
        if (gLastControlPackage != control) {
            gLastControlPackage = control;
            std::fprintf(stderr, "[benchmark] boot control: %s (frame %d)\n", control, frame);
        }
    }
    if (!gStarted) {
        // Frontend: hold enter for 6 frames of every 60 until the main menu
        // is up (the splash screen waits for a press).
        static bool career_started = false;
        const bool to_menu = gScenario->frontend || (gScenario->free_roam && !career_started);
        gEnterPresses = to_menu && TheGameFlowManager.IsInFrontend() && !MainMenuUp() && frame % 60 < 6 ? 1 : 0;
        if (gProfiles) {
            if (std::strstr(ControlPackageName(), "MC_") != nullptr) {
                gEnterPresses = 0;
            }
            CareerSaveBoot(frame, gLoadPhase && career_started);
        }
        // Free roam: a new career from the main menu (uiCareerCrib's "yes"
        // with the D-Day flow complete).
        if (gScenario->free_roam && !career_started && MainMenuUp() && gLoadPhase) {
            // The profile the boot loaded: the career menu's "Resume Career"
            // (CResumeCareer::React) into the safehouse.
            career_started = true;
            std::fprintf(stderr, "[benchmark] main menu, profile %s loaded %d (frame %d)\n", FEDatabase->GetUserProfile(0)->GetProfileName(),
                         FEDatabase->bProfileLoaded ? 1 : 0, frame);
            CheckProfileResave();
            CheckCareerState("after the load");
        }
        if (gScenario->free_roam && !career_started && MainMenuUp()) {
            career_started = true;
            // The career menu's press would give player 1 the pad it came from.
            FEDatabase->SetPlayersJoystickPort(0, 0);
            FEDatabase->GetCareerSettings()->StartNewCareer(true);
            // The skip-D-Day switches put the career straight into bin 15;
            // a career that played D-Day enters bin 15 from bin 16 (EEnterBin:
            // its milestones and speed traps become available, its baseline
            // unlocks are awarded). Do that here too.
            FEDatabase->GetCareerSettings()->SetCurrentBin(16);
            new EEnterBin(15);
            RaceStarter::StartCareerFreeRoam();
            std::fprintf(stderr, "[benchmark] new career, free roam (frame %d)\n", frame);
        }
        if ((Now() - gLaunch) / 1e9 > gTimeout) {
            std::fprintf(stderr, "[benchmark] %s: no start after %d s (game flow state %d), giving up\n", gScenario->name, gTimeout,
                         static_cast<int>(TheGameFlowManager.GetState()));
            std::fflush(nullptr);
            _exit(3);
        }
    } else {
        gBenchFrame++;
        if (gScenario->free_roam) {
            FreeRoamScreens();
        }
        if (gScenario->career_event || gScenario->rival || (gScenario->career_save && !gLoadPhase)) {
            CareerEventUpdate();
        }
        if (gScenario->career_save) {
            CareerSaveUpdate();
        }
        if (gScenario->rival) {
            RivalUpdate();
            if (gProfiles) {
                RivalProfileUpdate();
            }
        }
        if (gScenario->milestone) {
            MilestoneUpdate();
        }
        if (gScenario->busted) {
            BustedUpdate();
        }
        if (!gScenario->frontend && !gEventHoldCar) {
            DriveWithAI();
        }
    }

    std::string png;
    bool shot = gStarted && std::find(gShots.begin(), gShots.end(), gBenchFrame) != gShots.end();

    // NFSMW_BENCHMARK_SCREENS=1: a screenshot of every screen that takes
    // control, 90 frames in (<frame>_<package>.png, from the launch).
    static const bool screens = EnvInt("NFSMW_BENCHMARK_SCREENS", 0) != 0;
    static std::string screen_pkg;
    static int screen_frame = 0;
    if (screens) {
        const char *control = ControlPackageName();
        if (screen_pkg != control) {
            screen_pkg = control;
            screen_frame = frame;
        }
        if (frame - screen_frame == 90) {
            char name[96];
            std::snprintf(name, sizeof(name), "screen_%05d_%s.png", frame, *control ? control : "game");
            png = gOutDir + "/" + name;
            shot = true;
        }
    }
    if (shot && png.empty()) {
        char name[64];
        std::snprintf(name, sizeof(name), "%s_%05d.png", gScenario->name, gBenchFrame);
        png = gOutDir + "/" + name;
    }
    uint64_t present_start = Now();
    PortMacBenchmarkDrawFrame(gStarted ? gBenchFrame : 0, shot ? png.c_str() : nullptr);
    uint64_t end = Now();

    FrameRecord r;
    r.frame = gStarted ? gBenchFrame : 0;
    r.frame_ms = gLastEnd ? (end - gLastEnd) / 1e6 : 0;
    r.game_ms = gLastEnd ? (render_start_ns - gLastEnd) / 1e6 : 0;
    r.render_ms = (present_start - render_start_ns) / 1e6;
    r.present_ms = (end - present_start) / 1e6;
    r.shot = shot;
    r.cars = static_cast<int>(IVehicle::Count(VEHICLE_ALL) - IVehicle::Count(VEHICLE_INACTIVE));
    gLastEnd = end;
    if (gStarted) {
        gRecords.push_back(r);
        if (gBenchFrame >= gFrames) {
            Finish();
        }
    }
}
