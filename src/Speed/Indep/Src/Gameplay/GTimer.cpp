#include "Speed/Indep/Src/Gameplay/GTimer.h"

// PC: 0x5E5040.
GEventTimer::GEventTimer() : mInterval(1.0f), mRunning(false), mElapsed(0.0f), mNameHash(0) {
    mName[0] = '\0';
}

GEventTimer::~GEventTimer() {}
