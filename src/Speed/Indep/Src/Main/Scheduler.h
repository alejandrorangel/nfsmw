#ifndef MAIN_SCHEDULER_H
#define MAIN_SCHEDULER_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include "Speed/Indep/Libs/Support/Utility/FastMem.h"
#include "Speed/Indep/Libs/Support/Utility/UStandard.h"
#include "Speed/Indep/Src/Main/Event.h"
#include "Speed/Indep/Src/Misc/Timer.hpp"

// total size: 0x14
struct TaskRecord {
    int fTag;                       // offset 0x0, size 0x4
    Event::StaticData *fStaticData; // offset 0x4, size 0x4
    unsigned char fPriority;        // offset 0x8, size 0x1
    unsigned short fTickDelay;      // offset 0xA, size 0x2
    unsigned short fRepeat;         // offset 0xC, size 0x2
    int fAllocationHandle;          // offset 0x10, size 0x4
};

// total size: 0x8
class TaskList : public UTL::Std::list<TaskRecord, _type_list> {};

// A bank of tasks (event makers) that run at a fixed fraction of the game
// loop / sim rate. Tasks are spread over fNumBuckets banks; the subclasses
// pick which bank runs on a given tick.
// total size: 0x2C (PC: vtable at 0x0)
class Schedule {
  public:
    USE_FASTALLOC(Schedule)

    Schedule(int numBuckets);
    virtual ~Schedule();

    int AddTask(int tag, Event::StaticData *staticData, unsigned short priority, bool deterministic, int tickDelay, int repeat);
    bool RemoveTask(int handle);
    void RemoveAllTasks();

    virtual void Process(int time, unsigned short priority) = 0;

  protected:
    void RunTasks(int bankIndex, unsigned short priority);

  private:
    int EmptiestBucket();

    int fNumBuckets;               // offset 0x4, size 0x4
    struct TaskList *fTaskList[8]; // offset 0x8, size 0x20
    int fAllocationHandle;         // offset 0x28, size 0x4
};

class Schedule_OncePerGameLoop : public Schedule {
  public:
    Schedule_OncePerGameLoop() : Schedule(1) {}
    ~Schedule_OncePerGameLoop() override {}
    void Process(int time, unsigned short priority) override;
};

class Schedule_SimRate : public Schedule {
  public:
    Schedule_SimRate() : Schedule(1) {}
    ~Schedule_SimRate() override {}
    void Process(int time, unsigned short priority) override;
};

class Schedule_HalfSimRate : public Schedule {
  public:
    Schedule_HalfSimRate() : Schedule(2) {}
    ~Schedule_HalfSimRate() override {}
    void Process(int time, unsigned short priority) override;
};

class Schedule_QuarterSimRate : public Schedule {
  public:
    Schedule_QuarterSimRate() : Schedule(4) {}
    ~Schedule_QuarterSimRate() override {}
    void Process(int time, unsigned short priority) override;
};

class ScheduleList : public UTL::Std::list<Schedule *, _type_list> {};

// total size: 0x2C (PC)
class Scheduler {
    enum PriorityLevels {
        kPriHighest = 0,
        kPriHigh = 2,
        kPriMedium = 4,
        kPriLow = 6,
        kPriLowest = 7,
        kNumPriorityLevels = 8,
    };

  public:
    USE_FASTALLOC(Scheduler)

    static void Init(float timestep);
    static void Shutdown();

    Scheduler(float timestep);
    ~Scheduler();
    void AddSchedule(Schedule *schedule);
    float Run(bool full_speed);
    void Synchronize(Timer to);

    static Scheduler &Get() {
        return *fgScheduler;
    }

    float GetTime() const {
        return mTime;
    }

    float GetTimeStep() const {
        return fTimeStep;
    }

    void SetSingleStepMode() {
        fSingleStepMode = true;
    }

    void ClearSingleStepMode() {
        fSingleStepMode = false;
    }

    void SetFullSpeedMode() {
        fFullSpeedMode = true;
    }

    void ClearFullSpeedMode() {
        fFullSpeedMode = false;
    }

    Schedule *fSchedule_OncePerGameLoop; // offset 0x0, size 0x4
    Schedule *fSchedule_SimRate;         // offset 0x4, size 0x4
    Schedule *fSchedule_HalfSimRate;     // offset 0x8, size 0x4
    Schedule *fSchedule_QuarterSimRate;  // offset 0xC, size 0x4

  private:
    Timer GetTargetTimer();
    void RunNormalSpeed(unsigned int delta_tick);

    static Scheduler *fgScheduler;

    ScheduleList *fScheduleList; // offset 0x10, size 0x4
    Timer mTimer;                // offset 0x14, size 0x4
    float mTime;                 // offset 0x18, size 0x4
    float mTickBank;             // offset 0x1C, size 0x4
    const float fTimeStep;       // offset 0x20, size 0x4
    bool fSingleStepMode;        // offset 0x24, size 0x1
    bool fFullSpeedMode;         // offset 0x25, size 0x1
    unsigned int mEventTick;     // offset 0x28, size 0x4
};

#endif
