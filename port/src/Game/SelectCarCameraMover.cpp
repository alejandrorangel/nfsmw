// SelectCarCameraMover: the garage / car-select orbit camera, reconstructed
// from speed.exe 1.3 (the decomp has only the class declaration,
// Speed/Indep/Src/Camera/Movers/SelectCar.hpp).
//
//   0x476870 ctor            0x46B7E0 CreateCameraMatrix
//   0x4769E0 Update          0x46B920 SetCurrentOrientation
//   0x476E70/0x476F30/0x476FF0  Set{V,H}RotateSpeed / SetZoomSpeed
//   0x4770B0 SetDesiredOrientation   0x46B980 FindBestAngleGoal
//
// Camera data angles are in degrees. ControlMode: 0 animating towards the
// goal orientation, 1 user rotating/zooming, 2 at rest.

#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Camera/Movers/SelectCar.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

// (int)(degrees * 65536) / 360, as the original computes it.
bAngle DegreesToAngle(float degrees) {
    int scaled = static_cast<int>(degrees * 65536.0f);
    return static_cast<bAngle>(scaled / 360);
}

float WrapDegrees(float a) {
    if (a > 360.0f) {
        a -= 360.0f;
    }
    if (a < 0.0f) {
        a += 360.0f;
    }
    return a;
}

void Lerp(bVector3 &out, const bVector3 &a, const bVector3 &b, float t) {
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    out.z = a.z + (b.z - a.z) * t;
}

} // namespace

SelectCarCameraMover::SelectCarCameraMover(int view_id) : CameraMover(view_id, CM_SELECT_CAR) {
    ControlMode = 1;
    LookingAtParts = 0;
    CurrentCameraData.OrbitVAngle = 0.0f;
    CurrentCameraData.OrbitHAngle = 0.0f;
    CurrentCameraData.Radius = 0.0f;
    CurrentCameraData.RollAngle = 0.0f;
    CurrentCameraData.FOV = 0.0f;
    CurrentCameraData.LookAt = bVector3(0.0f, 0.0f, 0.0f);
    StartAnimCameraData = CurrentCameraData;
    GoalAnimCameraData = CurrentCameraData;
    RadiusSpeed = 0.0f;
    OrbitVSpeed = 0.0f;
    OrbitHSpeed = 0.0f;
    CurrentAnimationTime = 0.0f;
    TotalAnimationTime = 1.0f;
    Damping = 10.0f;
    Periods = 2;
}

SelectCarCameraMover::~SelectCarCameraMover() {}

void SelectCarCameraMover::CreateCameraMatrix(bMatrix4 *camera_matrix, SelectCarCameraData *camera_data) {
    eIdentity(camera_matrix);
    eRotateZ(camera_matrix, camera_matrix, DegreesToAngle(camera_data->OrbitHAngle));
    eRotateX(camera_matrix, camera_matrix, DegreesToAngle(camera_data->OrbitVAngle));
    bVector3 offset(0.0f, 0.0f, camera_data->Radius);
    eTranslate(camera_matrix, camera_matrix, &offset);

    bMatrix4 inverse;
    eInvertTransformationMatrix(&inverse, camera_matrix);
    bVector3 eye(inverse.v3.x, inverse.v3.y, inverse.v3.z);
    bVector3 up;
    ComputeBankedUpVector(&up, &eye, &camera_data->LookAt, DegreesToAngle(camera_data->RollAngle));
    eCreateLookAtMatrix(camera_matrix, eye, camera_data->LookAt, up);
}

void SelectCarCameraMover::SetCurrentOrientation(bVector3 &orbit, float roll, float fov, bVector3 &lookAt) {
    CurrentCameraData.OrbitVAngle = orbit.x;
    CurrentCameraData.OrbitHAngle = orbit.y;
    CurrentCameraData.Radius = orbit.z;
    CurrentCameraData.RollAngle = roll;
    CurrentCameraData.FOV = fov;
    CurrentCameraData.LookAt = lookAt;
}

// The goal angle (or goal +/- 360) closest to start.
float SelectCarCameraMover::FindBestAngleGoal(float start, float goal) {
    float best = goal;
    float best_distance = std::fabs(start - goal);
    float candidates[2] = {goal + 360.0f, goal - 360.0f};
    for (float c : candidates) {
        float d = std::fabs(start - c);
        if (d < best_distance) {
            best = c;
            best_distance = d;
        }
    }
    return best;
}

void SelectCarCameraMover::Update(float dT) {
    if (ControlMode != 2) {
        float elapsed = dT + CurrentAnimationTime;
        float progress = 1.0f;
        CurrentAnimationTime = elapsed;
        if (TotalAnimationTime > 0.0f && elapsed < TotalAnimationTime) {
            progress = elapsed / TotalAnimationTime;
        }
        // Damped spring from the start orientation to the goal (overshoots
        // Periods + 0.5 times while settling).
        float p2 = progress * progress;
        float blend = 1.0f - std::exp(-Damping * p2) * std::cos((static_cast<float>(Periods) + 0.5f) * p2 * 3.1415927f);

        if (ControlMode == 1) {
            float step = dT * 60.0f;
            CurrentCameraData.OrbitHAngle += step * OrbitHSpeed;
            float v = CurrentCameraData.OrbitVAngle + step * OrbitVSpeed;
            CurrentCameraData.OrbitVAngle = v > 110.0f ? 110.0f : (v < 94.5f ? 94.5f : v);
            float r = CurrentCameraData.Radius + step * RadiusSpeed * 0.125f;
            CurrentCameraData.Radius = r > 6.65f ? 6.65f : (r < 4.65f ? 4.65f : r);
            CurrentCameraData.RollAngle = (1.0f - blend) * StartAnimCameraData.RollAngle + blend * GoalAnimCameraData.RollAngle;
            CurrentCameraData.FOV = (1.0f - blend) * StartAnimCameraData.FOV + blend * GoalAnimCameraData.FOV;
            Lerp(CurrentCameraData.LookAt, StartAnimCameraData.LookAt, GoalAnimCameraData.LookAt, blend);
            CurrentCameraData.OrbitHAngle = WrapDegrees(CurrentCameraData.OrbitHAngle);
        } else if (ControlMode == 0) {
            float a = 1.0f - blend;
            CurrentCameraData.OrbitVAngle = a * StartAnimCameraData.OrbitVAngle + blend * GoalAnimCameraData.OrbitVAngle;
            CurrentCameraData.OrbitHAngle = a * StartAnimCameraData.OrbitHAngle + blend * GoalAnimCameraData.OrbitHAngle;
            CurrentCameraData.Radius = a * StartAnimCameraData.Radius + blend * GoalAnimCameraData.Radius;
            CurrentCameraData.RollAngle = a * StartAnimCameraData.RollAngle + blend * GoalAnimCameraData.RollAngle;
            CurrentCameraData.FOV = a * StartAnimCameraData.FOV + blend * GoalAnimCameraData.FOV;
            Lerp(CurrentCameraData.LookAt, StartAnimCameraData.LookAt, GoalAnimCameraData.LookAt, blend);
            CurrentCameraData.OrbitHAngle = WrapDegrees(CurrentCameraData.OrbitHAngle);
            CurrentCameraData.RollAngle = WrapDegrees(CurrentCameraData.RollAngle);
            if (progress >= 1.0f) {
                ControlMode = 2;
            }
        }
    }

    bMatrix4 camera_matrix;
    CreateCameraMatrix(&camera_matrix, &CurrentCameraData);
    static const bool trace = std::getenv("NFSMW_TRACE_SCENE") != nullptr;
    static int n;
    if (trace && n++ % 120 == 0) {
        const SelectCarCameraData &d = CurrentCameraData;
        std::fprintf(stderr, "[camera] mode %d V %.1f H %.1f R %.2f roll %.1f fov %.1f look (%.2f %.2f %.2f) row3 (%.2f %.2f %.2f) row0 (%.2f %.2f %.2f)\n",
                     ControlMode, d.OrbitVAngle, d.OrbitHAngle, d.Radius, d.RollAngle, d.FOV, d.LookAt.x, d.LookAt.y, d.LookAt.z,
                     camera_matrix.v3.x, camera_matrix.v3.y, camera_matrix.v3.z, camera_matrix.v0.x, camera_matrix.v0.y, camera_matrix.v0.z);
    }
    if (!Camera::StopUpdating) {
        GetCamera()->SetFieldOfView(DegreesToAngle(CurrentCameraData.FOV));
        GetCamera()->SetTargetDistance(CurrentCameraData.Radius);
    }
    GetCamera()->SetCameraMatrix(camera_matrix, dT);
}

// Rotate/zoom input: from rest, first ease roll/fov/look-at to the user-orbit
// defaults over ~0.56 s. Ignored while animating to a goal orientation.
void SelectCarCameraMover::BeginUserControl() {
    if (ControlMode != 1) {
        StartAnimCameraData.RollAngle = CurrentCameraData.RollAngle;
        StartAnimCameraData.FOV = CurrentCameraData.FOV;
        StartAnimCameraData.LookAt = CurrentCameraData.LookAt;
        GoalAnimCameraData.RollAngle = 0.0f;
        GoalAnimCameraData.FOV = 45.0f;
        GoalAnimCameraData.LookAt = bVector3(0.0f, 0.0f, 0.75f);
        GoalAnimCameraData.RollAngle = FindBestAngleGoal(StartAnimCameraData.RollAngle, GoalAnimCameraData.RollAngle);
        CurrentAnimationTime = 0.0f;
        TotalAnimationTime = 0.555f;
    }
    ControlMode = 1;
}

void SelectCarCameraMover::SetVRotateSpeed(float f) {
    if (ControlMode != 0) {
        BeginUserControl();
        OrbitVSpeed = f;
    }
}

void SelectCarCameraMover::SetHRotateSpeed(float f) {
    if (ControlMode != 0) {
        BeginUserControl();
        OrbitHSpeed = f;
    }
}

void SelectCarCameraMover::SetZoomSpeed(float f) {
    if (ControlMode != 0) {
        BeginUserControl();
        RadiusSpeed = f;
    }
}

void SelectCarCameraMover::SetDesiredOrientation(bVector3 &orbit, float roll, float fov, bVector3 &lookAt, float animSpeed, float damping,
                                                 int periods) {
    ControlMode = 0;
    StartAnimCameraData = CurrentCameraData;
    GoalAnimCameraData = CurrentCameraData;
    GoalAnimCameraData.OrbitVAngle = orbit.x;
    GoalAnimCameraData.OrbitHAngle = orbit.y;
    GoalAnimCameraData.Radius = orbit.z;
    GoalAnimCameraData.RollAngle = roll;
    GoalAnimCameraData.FOV = fov;
    GoalAnimCameraData.LookAt = lookAt;
    Damping = damping;
    Periods = periods;
    GoalAnimCameraData.OrbitVAngle = FindBestAngleGoal(StartAnimCameraData.OrbitVAngle, GoalAnimCameraData.OrbitVAngle);
    GoalAnimCameraData.OrbitHAngle = FindBestAngleGoal(StartAnimCameraData.OrbitHAngle, GoalAnimCameraData.OrbitHAngle);
    GoalAnimCameraData.RollAngle = FindBestAngleGoal(StartAnimCameraData.RollAngle, GoalAnimCameraData.RollAngle);
    CurrentAnimationTime = 0.0f;
    TotalAnimationTime = animSpeed;
}
