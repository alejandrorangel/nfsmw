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
// each frame is presented in lockstep with the game loop (BenchmarkMac.mm),
// so a run is always the same frames, drawn as fast as the machine can.
//
// Counting starts when the race is on (game flow RACING) or the main menu is
// up; after the scenario's frames it writes <scenario>.csv (per frame: wall
// time, the game loop, the render build, the present, the GPU) and the
// screenshots <scenario>_<frame>.png to $NFSMW_BENCHMARK_OUT (default: the
// current directory at launch), prints a summary and exits.
//
// Overrides: NFSMW_BENCHMARK_FRAMES=<n>, NFSMW_BENCHMARK_SHOTS=<f>,<f>,...,
// NFSMW_BENCHMARK_RACE=<event id>, NFSMW_BENCHMARK_CAR=<vehicle>,
// NFSMW_BENCHMARK_OPPONENTS=<n>, NFSMW_BENCHMARK_TRAFFIC=<0..100>,
// NFSMW_BENCHMARK_TIMEOUT=<s> (real seconds to reach the start, 180),
// NFSMW_BENCHMARK_VSYNC=1.

#include "Benchmark.h"

#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/RaceStarter.hpp"
#include "Speed/Indep/Src/Gameplay/GActivity.h"
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
        if (parms == nullptr || parms->GetEventID() == nullptr || parms->GetIsBossRace() || !trigger->IsEnabled()) {
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

void CareerEventUpdate() {
    const char *control = ControlPackageName();
    if (gLastControlPackage != control) {
        gLastControlPackage = control;
        std::fprintf(stderr, "[benchmark] control: %s (frame %d)\n", control, gBenchFrame);
    }
    const bool racing = GRaceStatus::Exists() && GRaceStatus::Get().GetPlayMode() == GRaceStatus::kPlayMode_Racing;
    const bool hud = std::strstr(control, "HUD") != nullptr || *control == '\0';
    const int since = gBenchFrame - gEventPhaseFrame;
    gEventPress = false;
    gEventLeft = false;
    switch (gEventPhase) {
        case kEvent_FreeRoam:
            if (gBenchFrame == 120) {
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
                gEventPress = since >= 60 && since % 60 < 6;
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
            gEventPress = !hud && since % 60 < 6;
            if (!racing) {
                SetEventPhase(kEvent_PostRace);
            }
            break;
        case kEvent_PostRace:
            gEventPress = !hud && since % 60 < 6;
            if (hud && !racing && TheGameFlowManager.IsInGame() && since > 120) {
                std::fprintf(stderr, "[benchmark] career event %s: cash %d -> %d, completed %d\n", gEventRace->GetEventID(), gEventStartCash,
                             static_cast<int>(FEDatabase->GetCareerSettings()->GetCash()),
                             GRaceDatabase::Get().CheckRaceScoreFlags(gEventRace->GetEventHash(), GRaceDatabase::kCompleted_ContextCareer) ? 1 : 0);
                SetEventPhase(kEvent_BackRoaming);
            }
            break;
        case kEvent_BackRoaming:
            if (since >= 600) {
                Finish();
            }
            break;
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
        std::fprintf(f, "frame,frame_ms,game_ms,render_ms,present_ms,gpu_ms,cars,screenshot\n");
    }
    for (const FrameRecord &r : gRecords) {
        if (r.frame <= 0) {
            continue;
        }
        double gpu = 0;
        PortMacBenchmarkGPUTime(r.frame, &gpu);
        // The first frame's wall time includes the load before it.
        if (r.frame > 1) {
            frame_ms.push_back(r.frame_ms);
            gpu_ms.push_back(gpu);
        }
        max_cars = std::max(max_cars, r.cars);
        if (f) {
            std::fprintf(f, "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d\n", r.frame, r.frame_ms, r.game_ms, r.render_ms, r.present_ms, gpu, r.cars,
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
}

bool PortBenchmarkActive() {
    return gScenario != nullptr;
}

void PortBenchmarkInit() {
    if (gScenario == nullptr) {
        return;
    }
    IsSoundEnabled = 0; // audio runs on real time
    IsMemcardEnabled = 0;
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
        if (gScenario->career_event) {
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
    return true;
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
    if (!gStarted) {
        // Frontend: hold enter for 6 frames of every 60 until the main menu
        // is up (the splash screen waits for a press).
        static bool career_started = false;
        const bool to_menu = gScenario->frontend || (gScenario->free_roam && !career_started);
        gEnterPresses = to_menu && TheGameFlowManager.IsInFrontend() && !MainMenuUp() && frame % 60 < 6 ? 1 : 0;
        // Free roam: a new career from the main menu (uiCareerCrib's "yes"
        // with the D-Day flow complete).
        if (gScenario->free_roam && !career_started && MainMenuUp()) {
            career_started = true;
            // The career menu's press would give player 1 the pad it came from.
            FEDatabase->SetPlayersJoystickPort(0, 0);
            FEDatabase->GetCareerSettings()->StartNewCareer(true);
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
        if (gScenario->career_event) {
            CareerEventUpdate();
        }
        if (!gScenario->frontend && !(gScenario->career_event && gEventHoldCar)) {
            DriveWithAI();
        }
    }

    std::string png;
    bool shot = gStarted && std::find(gShots.begin(), gShots.end(), gBenchFrame) != gShots.end();
    if (shot) {
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
