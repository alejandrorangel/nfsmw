// CameraAI::Action / CameraAI::Director as the native port reconstructs
// them from speed.exe 1.3 (implemented in CameraDriveNative.cpp; the ICE
// action lives in ICENative.cpp). Native-only: the decomp has no
// declarations for these.
#pragma once

#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Camera/CameraMover.hpp"
#include "Speed/Indep/Src/Misc/attribuserinclude.h"

class IPlayer;

namespace CameraAI {
class Director;

class Action {
  public:
    virtual ~Action() {}
    virtual void Update(float dT) = 0;
    virtual void Reset() = 0;
    virtual const Attrib::StringKey &GetName() const = 0;
    virtual Attrib::StringKey GetNext() const {
        return Attrib::StringKey();
    }
    virtual CameraMover *GetMover() = 0;
    virtual void SetSpecial(float f) {}
};

typedef Action *(*ActionConstructor)(Director *director);
ActionConstructor FindActionConstructor(unsigned int hash32);

// total size (PC): 0x2C8
class Director {
  public:
    explicit Director(EVIEW_ID view_id);
    ~Director();

    EVIEW_ID GetViewID() const {
        return mViewID;
    }
    Action *GetAction() {
        return mAction;
    }
    CameraMover *GetMover() {
        return mAction != nullptr ? mAction->GetMover() : nullptr;
    }
    bool IsCinematicMomement() const {
        return mIsCinematicMomement;
    }
    void Update(float dT);
    void SetAction(const Attrib::StringKey &desired);
    void SelectAction();
    void TotaledStart();
    void ResetTimers() {
        mJumpTime = 0.0f;
        mPursuitStartTime = 0.0f;
        mIsCinematicMomement = false;
        mCinematicSlowdownSeconds = 0.0f;
    }
    void SetCinematicSlowdown(float seconds) {
        mCinematicSlowdownSeconds = seconds;
    }

    static Director *sDirectors[2];
    static int sNumDirectors;

  private:
    void SwitchToDesired(bool from_select);

    EVIEW_ID mViewID;
    Attrib::StringKey mDesiredMode;
    Action *mAction;
    float mPursuitStartTime;
    float mJumpTime;
    bool mIsCinematicMomement;
    float mCinematicSlowdownSeconds;
};

// The local player rendering into view_id, or null.
IPlayer *FindPlayerForView(int view_id);

} // namespace CameraAI

// CDActionIce::Construct (0x485EE0), ICENative.cpp.
CameraAI::Action *ConstructCDActionIce(CameraAI::Director *director);
