// GInfractionManager from speed.exe 1.3 (the decomp's GInfractionManager.cpp is empty).
//
// The cop AI reports infractions (hit and run, assaulting an officer, property
// damage, off road) through GInfractionManager::Get(); with only the link stubs
// mObj stayed null and nothing was recorded.
#include "Speed/Indep/Src/Gameplay/GInfractionManager.h"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"

GInfractionManager *GInfractionManager::mObj = nullptr;

GInfractionManager::GInfractionManager()
    : mInfractions(0), //
      mNumThisPursuit(0), //
      mSpeeding(false), //
      mDrivingRecklessly(false), //
      mRacing(false) {}

GInfractionManager::~GInfractionManager() {}

// PC: 0x5DE2C0
void GInfractionManager::Init() {
    mObj = new GInfractionManager();
}

// PC: 0x5E54D0
void GInfractionManager::Shutdown() {
    delete mObj;
    mObj = nullptr;
}

// PC: 0x5DE310
void GInfractionManager::PursuitStarted() {
    mInfractions = 0;
    mNumThisPursuit = 0;
    mSpeeding = false;
    mDrivingRecklessly = false;
    mRacing = false;
}

static unsigned int CountBits(unsigned int bits) {
    unsigned int count = 0;
    for (; bits != 0; bits >>= 1) {
        count += bits & 1;
    }
    return count;
}

// PC: 0x5DE330
unsigned int GInfractionManager::GetNumInfractions() {
    return CountBits(mInfractions);
}

// PC: 0x5DE350
bool GInfractionManager::DidInfractionOccur(InfractionType infraction) {
    return (mInfractions & infraction) != 0;
}

// PC: 0x5FDDA0. Only counts while roaming or in a pursuit race.
void GInfractionManager::ReportInfraction(InfractionType infraction) {
    if (!GRaceStatus::Exists()) {
        return;
    }
    GRaceStatus &status = GRaceStatus::Get();
    bool roaming = status.GetPlayMode() == GRaceStatus::kPlayMode_Roaming;
    GRaceParameters *parms = status.GetRaceParameters();
    bool pursuitRace = parms != nullptr && parms->GetIsPursuitRace();
    if (!roaming && !pursuitRace) {
        return;
    }
    if ((mInfractions & infraction) == 0) {
        mInfractions |= infraction;
        // TODO(port): PC also queues EReportInfraction(infraction) (vtable 0x8A72F8, dtor 0x641A30) -- the HUD infraction
        // notice; the event isn't reconstructed yet.
        GManager::Get().TrackValue("total_infractions", static_cast<float>(CountBits(mInfractions)));
    }
    mNumThisPursuit++;
}
