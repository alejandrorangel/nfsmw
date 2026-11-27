#include "Speed/Indep/Src/Gameplay/GMilestone.h"

#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/gameplay.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/milestonetypes.h"
#include "Speed/Indep/Src/Generated/Events/EReportMilestoneAtStake.hpp"
#include "Speed/Indep/Src/Generated/Messages/MNotifyMilestoneReached.h"
#include "Speed/Indep/Src/Lua/LuaGameHooks.h"

// A career milestone (a "challenge": a value, like the cops damaged in one
// pursuit, to reach in a bin). States: 1 locked, 2 available, 3 reached
// (pending the pursuit's escape), 4 awarded. mFlags bit 0: a bigger value is
// better (the milestone type's); bit 1 (DebugForceComplete): record the goal
// itself as the value reached.

// PC: inlined (AllocateMilestones zeroes the array).
GMilestone::GMilestone()
    : mTypeKey(0), //
      mChallengeKey(0), //
      mState(0), //
      mFlags(0), //
      mBinNumber(0), //
      mRequiredValue(0.0f), //
      mRecordedValue(0.0f) {}

// PC: 0x5EE220. The type's last known value.
float GMilestone::GetCurrentValue() const {
    return GManager::Get().GetValue(mTypeKey);
}

// PC: 0x5E54F0. The challenge's bounty.
float GMilestone::GetBounty() const {
    Attrib::Gen::gameplay challenge(mChallengeKey, 0, nullptr);
    if (!challenge.IsValid()) {
        return 0.0f;
    }
    return static_cast<float>(challenge.Bounty());
}

// PC: 0x5E55A0. The milestone type's localization tag.
int GMilestone::GetLocalizationTag() const {
    Attrib::Gen::milestonetypes type(mTypeKey, 0, nullptr);
    if (!type.IsValid()) {
        return 0;
    }
    return type.LocalizationTag();
}

// PC: 0x5E5640. The challenge's jump-to marker (attribute 0xDDF411F5).
Attrib::Key GMilestone::GetJumpMarkerKey() const {
    Attrib::Gen::gameplay challenge(mChallengeKey, 0, nullptr);
    const Attrib::Key *key = static_cast<const Attrib::Key *>(challenge.GetAttributePointer(0xDDF411F5, 0));
    return key != nullptr ? *key : 0;
}

// Not on PC (debug only): reached and awarded with the goal as the value.
void GMilestone::DebugForceComplete() {
    mFlags |= 2;
    mState = 3;
    NotifyPursuitOver(true);
}

void GMilestone::Init(unsigned int challengeKey) {
    mChallengeKey = challengeKey;
    Reset();
}

// PC: 0x5EE250. Locked, from the challenge collection: the milestone type
// (MilestoneName), the bin, the goal.
void GMilestone::Reset() {
    Attrib::Gen::gameplay challenge(mChallengeKey, 0, nullptr);
    const char *name = challenge.MilestoneName();
    mTypeKey = Attrib::StringToKey(name != nullptr ? name : "");
    mState = 1;
    mBinNumber = static_cast<unsigned short>(challenge.BinIndex());
    mRequiredValue = challenge.GoalEasy();
    mRecordedValue = 0.0f;
    if (GManager::Get().GetIsBiggerValueBetter(mTypeKey)) {
        mFlags |= 1;
    }
}

// PC: inlined in EnableBinMilestones.
void GMilestone::Unlock() {
    if (mState == 1) {
        mState = 2;
    }
}

void GMilestone::SetGoal(float required) {
    mRequiredValue = required;
}

bool GMilestone::ValueMeetsGoal(float value) {
    float margin = mRequiredValue - value;
    if ((mFlags & 1) == 0) {
        margin = -margin;
    }
    return margin <= 0.0f;
}

// PC: 0x5DE4B0. An available milestone whose value reaches the goal is at
// stake: it's awarded if the pursuit is escaped.
void GMilestone::NotifyProgress(float value) {
    if (mState != 2) {
        return;
    }
    if (ValueMeetsGoal(value)) {
        new EReportMilestoneAtStake(this);
        mState = 3;
    }
}

// PC: 0x6015B0. Escaping awards a milestone at stake (the value reached,
// MNotifyMilestoneReached, the bin's progress, the challenge's bounty);
// getting busted makes it available again.
void GMilestone::NotifyPursuitOver(bool escaped) {
    if (mState != 3) {
        return;
    }
    if (!escaped) {
        mState = 2;
        return;
    }
    float value = GManager::Get().GetValue(mTypeKey);
    if ((mFlags & 2) != 0) {
        value = mRequiredValue;
    }
    mRecordedValue = value;
    mState = 4;
    Attrib::Gen::milestonetypes type(mTypeKey, 0, nullptr);
    MNotifyMilestoneReached(type.CollectionName(), value).Post(UCrc32(UCRC32_Gameplay));
    Attrib::Gen::gameplay challenge(mChallengeKey, 0, nullptr);
    GRaceBin *bin = GRaceDatabase::Get().GetBinNumber(mBinNumber);
    if (bin != nullptr) {
        bin->RefreshProgress();
    }
    Game_AwardPlayerBounty(challenge.Bounty());
    Game_ChallengeCompleted();
}
