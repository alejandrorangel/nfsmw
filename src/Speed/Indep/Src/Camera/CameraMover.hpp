#ifndef CAMERA_CAMERAMOVER_H
#define CAMERA_CAMERAMOVER_H

#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/Camera/Camera.hpp"
#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include "./Camera.hpp"
#include "./CameraAI.hpp"
#include "CameraInfo.hpp"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/camerainfo.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/ecar.h"
#include "Speed/Indep/Src/Sim/SimSurface.h"
#include "Speed/Indep/Src/World/WCollisionMgr.h"
#include "Speed/Indep/Src/World/WWorldPos.h"
#include "Speed/Indep/bWare/Inc/bList.hpp"
#include "Speed/Indep/Src/World/WCollider.h"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"
#include "Speed/Indep/Libs/Support/Utility/UListable.h"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/bWare/Inc/bMath.hpp"
#include "Speed/Indep/Src/Physics/PVehicle.h"
#include "Speed/Indep/bWare/Inc/Espresso.hpp"
#include "Speed/Indep/Src/Camera/ICE/ICEManager.hpp"

class eView;
struct tCubic1D;
struct tCubic3D;

enum CameraMoverTypes {
    CM_NONE_TYPE = 0,
    CM_DRIVE_CUBIC = 1,
    CM_DEBUG_WORLD = 2,
    CM_ROAD_EDITOR = 3,
    CM_ORBIT_CAR = 4,
    CM_REAR_VIEW_MIRROR = 5,
    CM_TRACK_CAR = 6,
    CM_TRACK_COP = 7,
    CM_MAX = 8,
    CM_STILL = 10,
    CM_SELECT_CAR = 9,
    CM_ZONE_FREEZE = 11,
    CM_ZONE_PREVIEW = 12,
    CM_AUTO_PILOT = 13,
    CM_ICE = 14,
    CM_ANIMATION_CONTROLLER = 15,
    CM_COP_VIEW = 16,
    CM_ANIMATION_ENTITY = 17,
    CM_SHOWCASE = 18,
};

// total size: 0x124
class CameraAnchor {
  public:
    float GetVelocityMagnitude() {
        return UMath::Sqrt(mVelocity.x * mVelocity.x + mVelocity.y * mVelocity.y + mVelocity.z * mVelocity.z);
    }

    bVector3 *GetGeometryPosition() {
        return &mGeomPos;
    }

    unsigned int GetWorldID() const {
        return mWorldID;
    }

    short GetPOVType() {
        return this->mPOV.Type;
    }

#if defined(EA_PORT_NATIVE)
    // Native port (port/src/Game/CameraDriveNative.cpp, from speed.exe 1.3).
    CameraAnchor(int model);                                                                    // 0x4716A0
    ~CameraAnchor();                                                                            // 0x46A660
    void Update(float dT, const bMatrix4 &matrix, const bVector3 &velocity, const bVector3 &accel); // 0x468FA0
    POV *GetPov(int pov_type);                                                                  // 0x4713C0
    void SetModel(int model);                                                                   // 0x471370
    bVector3 *GetVelocity() {
        return &mVelocity;
    }
    bVector3 *GetAcceleration() {
        return &mAccel;
    }
    bMatrix4 *GetGeometryOrientation() {
        return &mGeomRot;
    }
    float GetTopSpeed() const {
        return mTopSpeed;
    }

  private:
    friend class CDActionDrive;
    friend class CubicCameraMover;
#else
  private:
#endif
    bVector3 mVelocity;                            // offset 0x0, size 0x10
    float mVelMag;                                 // offset 0x10, size 0x4
    float mTopSpeed;                               // offset 0x14, size 0x4
    bVector3 mGeomPos;                             // offset 0x18, size 0x10
    bVector3 mDimension;                           // offset 0x28, size 0x10
    bVector3 mAccel;                               // offset 0x38, size 0x10
    bMatrix4 mGeomRot;                             // offset 0x48, size 0x40
    int mModel;                                    // offset 0x88, size 0x4
    unsigned int mWorldID;                         // offset 0x8C, size 0x4
    SimSurface mSurface;                           // offset 0x90, size 0x14
    float mCollisionDamping;                       // offset 0xA4, size 0x4
    float mDrift;                                  // offset 0xA8, size 0x4
    float mGroundCollision;                        // offset 0xAC, size 0x4
    float mObjectCollision;                        // offset 0xB0, size 0x4
    bool mIsNosEngaged;                            // offset 0xB4, size 0x1
    bool mIsBrakeEngaged;                          // offset 0xB8, size 0x1
    bool mIsDragRace;                              // offset 0xBC, size 0x1
    bool mIsOverRev;                               // offset 0xC0, size 0x1
    bool mIsTouchingGround;                        // offset 0xC4, size 0x1
    bool mIsVehicleDestroyed;                      // offset 0xC8, size 0x1
    bool mIsGearChanging;                          // offset 0xCC, size 0x1
    bool mIsCloseToRoadBlock;                      // offset 0xD0, size 0x1
    float mZoom;                                   // offset 0xD4, size 0x4
    POV mPOV;                                      // offset 0xD8, size 0x24
    Attrib::Gen::ecar mModelAttributes;            // offset 0xFC, size 0x14
    Attrib::Gen::camerainfo mCameraInfoAttributes; // offset 0x110, size 0x14
};

// total size: 0x80
class CameraMover : public bTNode<CameraMover>, public WCollisionMgr::ICollisionHandler {
  public:
    CameraMover(int view_id, CameraMoverTypes type);

    CameraMoverTypes GetType() {
        return Type;
    }

    static void ComputeBankedUpVector(bVector3 *up, bVector3 *eye, bVector3 *look, bAngle bank);

    WUID GetAnchorID();

    Camera *GetCamera() {
        return pCamera;
    }

    bVector3 *GetPosition() {
        return pCamera->GetPosition();
    }

    float GetDistanceTo(const bVector3 *to) {
        bVector3 rel;

        bSub(&rel, GetPosition(), to);
        return bLength(&rel);
    }

    // Virtual methods
    virtual ~CameraMover();

    virtual void Update(float dT);
    virtual void Render(eView *view);

    void ChopperNoise(bMatrix4 *world_to_camera, float f_scale, bool useWorldTimer);
    void HandheldNoise(bMatrix4 *world_to_camera, float f_scale, bool useWorldTimer);
    void TerrainVelocityNoise(bMatrix4 *world_to_camera, CameraAnchor *p_car, float f_speed_scale, float f_terrain_scale);

    bool IsDriveCamera() {
        return this->Type == CM_DRIVE_CUBIC;
    }

#if defined(EA_PORT_NATIVE)
    // speed.exe 1.3 base implementations (0x470970..0x470A00) return 0.
    virtual CameraAnchor *GetAnchor() {
        return nullptr;
    }

    virtual void SetLookBack(bool b) {}

    virtual void SetLookbackSpeed(float speed) {}

    virtual void SetDisableLag(bool disable) {}

    virtual void SetPovType(int pov_type) {}

    virtual bool OutsidePOV() {
        return false;
    }

    virtual bool RenderCarPOV() {
        return false;
    }

    virtual float MinDistToWall();

    virtual unsigned short GetLookbackAngle() {
        return 0;
    }

    virtual void ResetState() {}

    virtual bool IsHoodCamera() {
        return false;
    }

    int GetViewID() const {
        return ViewID;
    }

    eView *GetView() {
        return pView;
    }

    // speed.exe 1.3: 0x468BE0 / 0x468C50 / 0x468D30 / 0x468E40
    void FovCubicInit(tCubic1D *fov);
    void EyeCubicInit(tCubic3D *eye, bMatrix4 *world_to_local, bVector3 *anchor_velocity);
    void LookCubicInit(tCubic3D *look, bMatrix4 *world_to_local, bVector3 *anchor_velocity);
    void SetEyeLook(tCubic3D *eye, tCubic3D *look, tCubic1D *fov, bMatrix4 *world_to_local, bVector3 *anchor_velocity);
#else
    virtual CameraAnchor *GetAnchor() {}

    virtual void SetLookBack(bool b) {}

    virtual void SetLookbackSpeed(float speed) {}

    virtual void SetDisableLag(bool disable) {}

    virtual void SetPovType(int pov_type) {}

    virtual bool OutsidePOV() {}

    virtual bool RenderCarPOV() {}

    virtual float MinDistToWall();

    virtual unsigned short GetLookbackAngle() {}

    virtual void ResetState() {}

    virtual bool IsHoodCamera() {}
#endif

    // ICollisionHandler
    bool OnWCollide(const WCollisionMgr::WorldCollisionInfo &cInfo, const UMath::Vector3 &cPoint, void *userdata) override;

    virtual void Enable();
    virtual void Disable();

  private:
#if defined(EA_PORT_NATIVE)
    friend class CubicCameraMover;
#endif
    CameraMoverTypes Type;       // offset 0xC, size 0x4
    int ViewID;                  // offset 0x10, size 0x4
    int Enabled;                 // offset 0x14, size 0x4
    eView *pView;                // offset 0x18, size 0x4
    Camera *pCamera;             // offset 0x1C, size 0x4
    int RenderDash;              // offset 0x20, size 0x4
    struct WCollider *mCollider; // offset 0x24, size 0x4
    WWorldPos mWPos;             // offset 0x28, size 0x3C
    float fAccumulatedClearance; // offset 0x64, size 0x4
    float fAccumulatedAdjust;    // offset 0x68, size 0x4
    float fSavedAdjust;          // offset 0x6C, size 0x4
    bVector3 vSavedForward;      // offset 0x70, size 0x10
};

void CameraMoverRestartRace();
void UpdateCameraMovers(float dT);

#endif
