#ifndef CAMERA_MOVERS_CUBIC_H
#define CAMERA_MOVERS_CUBIC_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#if defined(EA_PORT_NATIVE)
#include "Speed/Indep/Src/Camera/CameraMover.hpp"
#include "Speed/Indep/Src/Misc/Point.hpp"
#include "Speed/Indep/Src/Misc/Timer.hpp"

struct CubicCameraTweak;
class CubicAccelAverage;

// The in-race chase / bumper / hood camera (CM_DRIVE_CUBIC). Native port of
// speed.exe 1.3; the code lives in port/src/Game/CameraDriveNative.cpp.
// Eye, look and forward are cubic splines in the anchor's local frame,
// seeking the per-POV offsets from the car's camerainfo attributes.
class CubicCameraMover : public CameraMover {
  public:
    CubicCameraMover(int view_id, CameraAnchor *anchor, int pov_type, bool blend_from_ice, bool disable_lag, bool look_back,
                     bool perfect_focus); // 0x481BD0
    ~CubicCameraMover() override;         // 0x474100

    void Update(float dT) override; // 0x47D3C0
    CameraAnchor *GetAnchor() override {
        return pCar;
    }
    void SetLookBack(bool b) override {
        bLookBack = b;
    }
    void SetDisableLag(bool disable) override {
        bAccelLag = !disable;
    }
    void SetPovType(int pov_type) override; // 0x46B050
    bool OutsidePOV() override;             // 0x46AFA0
    bool RenderCarPOV() override;           // 0x46AFD0
    float MinDistToWall() override;         // 0x46AF90
    unsigned short GetLookbackAngle() override {
        return bLookBack ? 0x8000 : 0;
    }
    void ResetState() override; // 0x46B0C0
    bool IsHoodCamera() override;

    void SetSnapNext() {
        bSnapNext = true;
    }
    void SetFirstTime() {
        bFirstTime = true;
    }
    float GetIceBlend() const {
        return fIceBlend;
    }

  private:
    bool IsUnderVehicle();                                                                         // 0x4741F0
    void SetForward(POV *pov, bool snap);                                                          // 0x46B1F0
    void BuildBasis(bMatrix4 *basis);                                                              // 0x46B410
    void SetDesired(bMatrix4 *basis, POV *pov, const CubicCameraTweak *tweak, bool snap);         // 0x47CF00
    void CameraAccelCurve(bVector3 *accel);                                                        // 0x47CDD0
    void CameraSpeedHug(bVector3 *offset);                                                         // 0x47CE80
    bVector3 AvoidablesPush(const bVector3 &pos, const bVector3 &dir);                             // 0x47C440
    float AvoidablesClearance(bVector4 *eye, bVector3 *push);                                      // 0x47C250
    bool MakeSpace(bMatrix4 *world_to_camera, bVector3 *target);                                   // 0x470FE0
    bool WallClearance(bVector3 *eye, bVector3 *target, bVector4 *push);                           // 0x470BF0
    bool AvoidAvoidables(bMatrix4 *world_to_camera, bVector3 *target, CameraAnchor *anchor);        // 0x47C6F0
    void TerrainVelocityNoise(bMatrix4 *world_to_camera, CameraAnchor *anchor, float speed_scale, float terrain_scale); // 0x47BEC0

    CameraAnchor *pCar;          // PC 0x80
    tCubic1D *pFov;              // PC 0x84
    tCubic3D *pEye;              // PC 0x88
    tCubic3D *pLook;             // PC 0x8C
    tCubic3D *pForward;          // PC 0x90
    tCubic3D *pUp;               // PC 0x94
    int nPovType;                // PC 0x98
    int nPovTypeUsed;            // PC 0x9C
    bool bAccelLag;              // PC 0xA0
    bool bLookBack;              // PC 0xA1
    bool bSnapNext;              // PC 0xA2
    bool bPerfectFocus;          // PC 0xA3
    bool bFirstTime;             // PC 0xA4
    int tLastGrounded;           // PC 0xA8 (WorldTimer ticks)
    int tLastUnderVehicle;       // PC 0xAC
    int tLastGearChange;         // PC 0xB0
    float fIceBlend;             // PC 0xB4
    bVector3 vSavedEye;          // PC 0xC0
    bVector2 vCameraImpact;      // PC 0xD0
    bVector2 vCameraImpactTimer; // PC 0xD8
    CubicAccelAverage *pAvgAccel; // PC 0xE0 tAverage<bVector3>(5)
};
#endif

#endif
