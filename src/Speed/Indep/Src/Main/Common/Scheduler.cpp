#include "Speed/Indep/Src/Main/Scheduler.h"
#include "Speed/Indep/Src/Main/Event.h"
#include "Speed/Indep/Src/Misc/Timer.hpp"
#include "Speed/Indep/Src/World/OnlineManager.hpp"

// Reconstructed from the PC build (speed.exe v1.3); addresses below are PC.

void bThreadYield(int a);

Scheduler *Scheduler::fgScheduler = nullptr;

// PC 0x9203AC. Written only by a debug toggle (sub_42A810); when set the
// scheduler catches up at most 2 sim ticks per game loop instead of 6.
static bool gSchedulerReducedCatchUp = false;

// PC 0x6469C0
Schedule::Schedule(int numBuckets) : fNumBuckets(numBuckets), fAllocationHandle(1000) {
    for (int i = 0; i < 8; i++) {
        fTaskList[i] = nullptr;
    }
    for (int i = 0; i < fNumBuckets; i++) {
        fTaskList[i] = new (gFastMem.Alloc(sizeof(TaskList), nullptr)) TaskList;
    }
}

// PC 0x646A90
Schedule::~Schedule() {
    for (int i = 0; i < fNumBuckets; i++) {
        TaskList *list = fTaskList[i];
        if (list) {
            list->~TaskList();
            gFastMem.Free(list, sizeof(TaskList), nullptr);
        }
    }
}

int Schedule::EmptiestBucket() {
    unsigned int least = 999999;
    int leastInd = -1;
    for (int i = 0; i < fNumBuckets; i++) {
        if (fTaskList[i]->size() < least) {
            least = fTaskList[i]->size();
            leastInd = i;
        }
    }
    return leastInd;
}

// PC 0x647DD0
int Schedule::AddTask(int tag, Event::StaticData *staticData, unsigned short priority, bool deterministic, int tickDelay, int repeat) {
    TaskList *list = fTaskList[EmptiestBucket()];
    TaskRecord record;
    record.fTag = tag;
    record.fStaticData = staticData;
    record.fPriority = static_cast<unsigned char>(priority);
    record.fTickDelay = static_cast<unsigned short>(tickDelay);
    record.fRepeat = static_cast<unsigned short>(repeat);
    record.fAllocationHandle = fAllocationHandle;
    list->push_back(record);
    return fAllocationHandle++;
}

// PC 0x642F60
bool Schedule::RemoveTask(int handle) {
    for (int bankIndex = 0; bankIndex < 8; bankIndex++) {
        TaskList *list = fTaskList[bankIndex];
        if (!list) {
            continue;
        }
        for (TaskList::iterator iter = list->begin(); iter != list->end(); ++iter) {
            if (iter->fAllocationHandle == handle) {
                list->erase(iter);
                return true;
            }
        }
    }
    return false;
}

void Schedule::RemoveAllTasks() {
    for (int bankIndex = 0; bankIndex < fNumBuckets; bankIndex++) {
        fTaskList[bankIndex]->clear();
    }
}

// PC 0x642FD0. Runs every task of the given priority in one bank: a task
// waits out its tick delay, then fires its event's make-event callback each
// tick; a non-zero repeat count removes it after that many runs.
void Schedule::RunTasks(int bankIndex, unsigned short priority) {
    TaskList *list = fTaskList[bankIndex];
    TaskList::iterator iter = list->begin();
    while (iter != list->end()) {
        TaskRecord &task = *iter;
        if (task.fPriority == priority) {
            if (task.fTickDelay != 0) {
                task.fTickDelay--;
            } else {
                RegisterEvent::MakeEventCallback callback = RegisterEvent::LookupEvent(task.fTag);
#if defined(EA_PORT_NATIVE)
                // The PC table has every event; ours only the reconstructed ones.
                if (callback)
#endif
                    callback(task.fStaticData);
                if (task.fRepeat != 0) {
                    task.fRepeat--;
                    if (task.fRepeat == 0) {
                        TaskList::iterator saveIter = iter;
                        ++saveIter;
                        RemoveTask(task.fAllocationHandle);
                        iter = saveIter;
                        continue;
                    }
                }
            }
        }
        ++iter;
    }
}

// PC 0x643090
void Schedule_OncePerGameLoop::Process(int time, unsigned short priority) {
    RunTasks(0, priority);
}

// PC 0x6430A0
void Schedule_SimRate::Process(int time, unsigned short priority) {
    RunTasks(0, priority);
}

// PC 0x6430B0
void Schedule_HalfSimRate::Process(int time, unsigned short priority) {
    RunTasks(time & 1, priority);
}

// PC 0x6430C0
void Schedule_QuarterSimRate::Process(int time, unsigned short priority) {
    RunTasks(time & 3, priority);
}

// PC 0x6482D0
Scheduler::Scheduler(float timestep)
    : fSchedule_OncePerGameLoop(nullptr), //
      fSchedule_SimRate(nullptr),         //
      fSchedule_HalfSimRate(nullptr),     //
      fSchedule_QuarterSimRate(nullptr),  //
      mTimer(0),                          //
      mTime(0.0f),                        //
      mTickBank(0.0f),                    //
      fTimeStep(timestep),                //
      fSingleStepMode(false),             //
      fFullSpeedMode(false),              //
      mEventTick(0) {
    fScheduleList = new (gFastMem.Alloc(sizeof(ScheduleList), nullptr)) ScheduleList;

    fSchedule_OncePerGameLoop = new Schedule_OncePerGameLoop();

    fSchedule_SimRate = new Schedule_SimRate();
    AddSchedule(fSchedule_SimRate);

    fSchedule_HalfSimRate = new Schedule_HalfSimRate();
    AddSchedule(fSchedule_HalfSimRate);

    fSchedule_QuarterSimRate = new Schedule_QuarterSimRate();
    AddSchedule(fSchedule_QuarterSimRate);
}

// PC 0x646940
Scheduler::~Scheduler() {
    if (fSchedule_OncePerGameLoop) {
        delete fSchedule_OncePerGameLoop;
    }
    for (ScheduleList::iterator iter = fScheduleList->begin(); iter != fScheduleList->end(); ++iter) {
        if (*iter) {
            delete *iter;
        }
    }
    if (fScheduleList) {
        fScheduleList->~ScheduleList();
        gFastMem.Free(fScheduleList, sizeof(ScheduleList), nullptr);
    }
}

// PC 0x6484E0
void Scheduler::Init(float timestep) {
    if (!fgScheduler) {
        fgScheduler = new Scheduler(timestep);
    }
}

// PC 0x647420
void Scheduler::Shutdown() {
    if (fgScheduler) {
        delete fgScheduler;
    }
    fgScheduler = nullptr;
}

void Scheduler::AddSchedule(Schedule *schedule) {
    fScheduleList->push_back(schedule);
}

// PC 0x642DB0
void Scheduler::RunNormalSpeed(unsigned int delta_tick) {
    if (fSingleStepMode && delta_tick != 0) {
        delta_tick = 1;
    }

    unsigned int max_elapsed = gSchedulerReducedCatchUp ? 2 : 6;
    bool online_racing = TheOnlineManager.GetState() == OLS_RACING;
    if (!online_racing && delta_tick > max_elapsed) {
        delta_tick = max_elapsed;
    }

    for (unsigned int i = 0; i < delta_tick; i++) {
        for (int priority = 0; priority < kNumPriorityLevels; priority++) {
            for (ScheduleList::iterator iter = fScheduleList->begin(); iter != fScheduleList->end(); ++iter) {
                (*iter)->Process(mEventTick, priority);
            }
        }
        EventManager::RunEvents();
        mEventTick++;
    }

    // PC: if online racing, TheOnlineManager.UpdateOutgoing() (online not reconstructed)

    for (int priority = 0; priority < kNumPriorityLevels; priority++) {
        fSchedule_OncePerGameLoop->Process(0, priority);
    }
    EventManager::RunEvents();
}

// PC 0x6277D0. Online races lock to the host's master clock (not
// reconstructed); otherwise the target is real time.
Timer Scheduler::GetTargetTimer() {
    return RealTimer;
}

// PC 0x642E60
float Scheduler::Run(bool full_speed) {
    Timer target_timer = GetTargetTimer();
    float time_delta_seconds = (target_timer - mTimer).GetPackedTime() * (1.0f / TIMER_SHIFT_VALUE_FLOAT);

    if (time_delta_seconds < 0.0f) {
        mTimer = target_timer;
        return 0.0f;
    }

    if (!full_speed && time_delta_seconds < fTimeStep) {
        bThreadYield(static_cast<int>((fTimeStep - time_delta_seconds) * 1000.0f));
    }
    mTimer = target_timer;

    float elapsed = time_delta_seconds > 0.0f ? time_delta_seconds : 0.0f;
#if defined(EA_PORT_NATIVE)
    // On PC the loop is locked to vsync, so each frame runs a steady number
    // of sim ticks. Natively frames are paced to a 60 Hz grid by sleeping;
    // jitter (and a long frame followed by a short catch-up one) tipped the
    // tick bank into 0- and 2-tick frames -- visible judder. Run one tick per
    // paced frame and keep the drift between sim time and real time in a
    // separate bank; only when real time is a whole step ahead/behind does a
    // frame run two ticks / none, so the sim still tracks real time.
    if (!full_speed && elapsed > 0.0f && elapsed < 3.0f * fTimeStep) {
        static float drift = 0.0f;
        drift += elapsed - fTimeStep;
        float ticks = 1.0f;
        if (drift >= fTimeStep) {
            ticks = 2.0f;
            drift -= fTimeStep;
        } else if (drift <= -fTimeStep) {
            ticks = 0.0f;
            drift += fTimeStep;
        }
        elapsed = ticks * fTimeStep;
        mTickBank = 0.0f;
    }
#endif
    mTime += elapsed;
    float delta_tick = elapsed / fTimeStep + mTickBank;
    unsigned int ticks_to_run = static_cast<unsigned int>(delta_tick);
    mTickBank = delta_tick - static_cast<float>(ticks_to_run);

    if (full_speed) {
        ticks_to_run = 4;
        mTickBank = 0.0f;
    }

#if defined(EA_PORT_NATIVE)
    extern int gPortLastSchedulerTicks; // FERenderMetal.cpp (NFSMW_TRACE_FRAMETIME)
    gPortLastSchedulerTicks = static_cast<int>(ticks_to_run);
#endif
    RunNormalSpeed(ticks_to_run);
    return static_cast<float>(ticks_to_run) * fTimeStep;
}

// PC 0x627860
void Scheduler::Synchronize(Timer to) {
    mTime = 0.0f;
    mEventTick = 0;
    mTimer = to;
    mTickBank = 0.0f;
}
