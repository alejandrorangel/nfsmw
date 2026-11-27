#include "Speed/Indep/Src/Gameplay/GTimer.h"

#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Generated/Messages/MNotifyTimer.h"

// PC: 0x5E5040.
GEventTimer::GEventTimer() : mInterval(1.0f), mRunning(false), mElapsed(0.0f), mNameHash(0) {
    mName[0] = '\0';
}

GEventTimer::~GEventTimer() {}

// PC: 0x5DDF50. Ticks MNotifyTimer every interval while running.
void GEventTimer::Update(float dT) {
    if (!mRunning) {
        return;
    }
    mElapsed += dT;
    if (mElapsed >= mInterval) {
        MNotifyTimer(mName).Post(UCrc32(UCRC32_Gameplay));
        mElapsed -= mInterval;
    }
}
