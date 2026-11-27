// The weather and the rain, reconstructed from speed.exe 1.3 (the decomp's
// rain.cpp is stripped):
//
//   Rain::Init            0x73C6B0   Rain::Rain       0x749DC0 (decomp ctor)
//   Rain::SetRainIntensity 0x73CC80  SetRainBase      0x749D70 (decomp)
//   Rain::UpdateAndRender 0x758100   Rain::Update     0x757770
//   Rain::Wind            0x753B50   Rain::Render     0x74ACC0
//   GetDesiredRainIntensity 0x7539D0 GetDesiredCloudyness 0x753AC0
//   WorldWeatherTime      0x73C980   IsRainingAt 0x73C9F0  IsCloudyAt 0x73CA50
//   OnScreenRain::OnScreenRain 0x73CD30  ::Update 0x73CDB0  ::GetData 0x73CFA0
//   AmIinATunnelSlow      0x74B000   the tweak reload under AlwaysRain 0x74AB20
//
// The PC Rain object has the GameCube field order with PC alignment (Rain
// +0x234 NoRain, +0x23C inTunnel, +0x240 inOverpass, +0x28C intensity, +0x290
// CloudIntensity, +0x2A0 RainPoints, +0x2E60 RainPointsInf, +0x3610
// local2world, +0x3650 world2localrot, +0x3690 LenModifier, +0x3694
// DesiredIntensity, +0x3698 DesiredCloudyness, +0x369C DesiredRoadDampness,
// +0x36A0 RoadDampness, +0x36C8 Velocities), so the decomp's names are used.
//
// The weather: UpdateAndRender eases the intensity (1/s) towards the "Rain"
// parameter map's value where the player is and the cloud intensity (0.1/s)
// towards the rain's, or the "Clouds" map's when it doesn't rain. Each map
// field 0 is the amount and field 1 a threshold the position's weather curve
// must not exceed (offline the weather time is 1: the curve depends only on
// x). A scripted override (rainOverrideIntensity: Game_SetChanceOfRain sets
// -1 for no chance of rain, 0 to let the map decide; GRaceStatus and
// NISActivity::Release clear it) wins when non-zero. ___eDisplayFrame calls
// UpdateAndRender (player 1's) only in a race with the Particle option on
// (0x9017EC), so that also gates the weather.

#include "Speed/Indep/Src/World/Rain.hpp"
#include "Speed/Indep/Src/World/ParameterMaps.hpp"
#include "Speed/Indep/Src/World/TrackPath.hpp"
#include "Speed/Indep/Src/World/ScreenEffects.hpp"
#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Camera/CameraMover.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/EcstasyE.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#include "Speed/Indep/Src/Misc/Timer.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"
#include "Speed/Indep/bWare/Inc/Strings.hpp"
#include "Speed/PC/Src/PCGraphicsOptions.hpp"

#include "../Mac/MetalBridge.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// rain.cpp's tweakables (same values as the 1.3 .data at 0x904A20..).
extern float RAINX, RAINY, RAINZ, RAINZconstant;
extern float precipBoundX, precipBoundY, precipBoundZ;
extern float precipAheadX, precipAheadY, precipAheadZ;
extern float RAINwindEffect, RAINRadiusX, RAINRadiusY, RAINRadiusZ;
extern float precipPERCENT;     // 0x904AD8
extern float driveFactor;       // 0x904ACC
extern float PrevailingMult;    // 0x904AF4
extern float maxWindEffect;     // 0x904AF0
extern float windAng;           // 0x9B0A50
extern float wspeed;            // 0x904AEC
extern float twkCloudsMinAmount; // 0x9B0A3C (bss, 0)
extern float rainOverrideIntensity; // 0x9B0A48
extern uint32 precipRENDER;     // 0x904AD0
extern ParameterAccessor RainAccessor;  // 0x9B2A90
extern ParameterAccessor CloudAccessor; // 0x9B0C3C
extern ParameterAccessorBlendByDistance WindAccessor[2]; // 0x905800

int PrecipitationEnable = 1; // 0x8F86E4 (ENISRain sets it, NISActivity saves/restores it)
float GeneralRainAmount = 1.0f; // 0x904A14

namespace {

// AlwaysRain (0x9B0A30): a PC debug switch: rain at GeneralRainAmount
// everywhere, the road fully wet. NFSMW_RAIN=1.
int AlwaysRain() {
    static const int on = [] {
        const char *e = std::getenv("NFSMW_RAIN");
        return e != nullptr && *e != '\0' && *e != '0' ? 1 : 0;
    }();
    return on;
}

bool TraceRain() {
    static const bool on = std::getenv("NFSMW_TRACE_RAIN") != nullptr;
    return on;
}

const float kRoadReflection = 1.0f;     // RoadReflection 0x904B38
const float kWetRamp = 0.005f;          // 0x8B2F60
const float kRateOfChange = 1.0f;       // 0x904AC4 (intensity, per second)
const float kCloudRateOfChange = 10.0f; // 0x904AC8
const int kRainFrozen = 0;              // 0x9B085C
const int kDripsFrozen = 0;             // 0x9B0A58

uint16 gPrevHeading[2];     // 0x9B37A8, per player view
int gWindState = 1;         // 0x905CE0 (CHANGE)
float gWindChangeTime;      // 0x9B378C
float gLastZConstant;       // 0x9B3788

// The online flags (byte 0x9B0FB9, Online::IsInitialized): never set here.
bool OnlineWeather() {
    return false;
}

float WorldWeatherTime() {
    // 0x73C980: online the weather runs on a 503 s clock shared by the
    // players; offline it is 1.
    return 1.0f;
}

bAngle DegreesToAngle(float degrees) {
    // __ftol2(deg * 65536) / 360, as the PC truncates.
    int a = static_cast<int>(degrees * 65536.0f) / 360;
    return static_cast<bAngle>(a);
}

// 0x73C9F0: 1 - max(cos(0.714 t + 0.02 x degrees), 0).
float IsRainingAt(float x, float y) {
    float c = bCos(DegreesToAngle(WorldWeatherTime() * 0.71428573f + x * 0.02f));
    if (c < 0.0f) {
        c = 0.0f;
    }
    return 1.0f - std::fabs(c);
}

// 0x73CA50: 1 - |sin(...)|, negative sines counting as 0 offline.
float IsCloudyAt(float x, float y) {
    float s = bSin(DegreesToAngle(WorldWeatherTime() * 0.71428573f + x * 0.02f));
    if (s < 0.0f && !OnlineWeather()) {
        s = 0.0f;
    }
    return 1.0f - std::fabs(s);
}

} // namespace

// 0x7539D0.
float GetDesiredRainIntensity(float x, float y) {
    if (!OnlineWeather()) {
        if (rainOverrideIntensity > 0.0f) {
            return rainOverrideIntensity;
        }
        if (rainOverrideIntensity < 0.0f) {
            return 0.0f;
        }
    }
    float curve = IsRainingAt(x, y);
    if (OnlineWeather()) {
        curve *= curve;
    }
    if (RainAccessor.IsValid()) {
        RainAccessor.CaptureData(x, y);
        if (curve <= RainAccessor.GetDataFloat(1)) {
            return RainAccessor.GetDataFloat(0);
        }
    }
    return 0.0f;
}

// 0x753AC0.
float GetDesiredCloudyness(float x, float y) {
    float curve = IsCloudyAt(x, y);
    if (CloudAccessor.IsValid()) {
        CloudAccessor.CaptureData(x, y);
        if (curve <= CloudAccessor.GetDataFloat(1)) {
            return CloudAccessor.GetDataFloat(0);
        }
        return 0.0f;
    }
    return twkCloudsMinAmount;
}

// 0x73CD30.
OnScreenRain::OnScreenRain() {
    this->NumOnScreen = 0;
    for (int i = 0; i < MAX_OS_RAINPOINTS; i++) {
        OnScreenRainPointsDef &p = this->Points[i];
        p.x = 0.0f;
        p.y = 0.0f;
        p.timer = 0.0f;
        p.Maxtimer = bRandom(5.0f) + 0.3f;
        p.DripSpeed = bRandom(0.1f) + 0.02f;
        p.Size = bRandom(1.0f);
        p.DripShape = i & 3;
    }
}

// 0x73CDB0: 20 drops (10 split screen) while it rains outside a tunnel;
// each lives Maxtimer seconds at a random spot, sliding down at 0.2
// DripSpeed per second, and dies off the bottom. (The PC adds the zero z of
// the normalised (x - 0.5, y - 0.1, 0), times the camera speed, to x: they
// don't spread.) The drip shape cycles while the camera moves.
void OnScreenRain::Update(eView *view) {
    const float dt = WorldTimeElapsed;
    const bVector3 *vel = view->GetCamera()->GetVelocityPosition();
    const float speed = std::sqrt(vel->x * vel->x + vel->y * vel->y + vel->z * vel->z) * 0.0001f; // 0x904B30
    Rain *rain = view->Precipitation;
    if (rain->NoRain != 0) {
        this->NumOnScreen = 0;
    } else if (eGetCurrentViewMode() >= 3) {
        this->NumOnScreen = rain->GetRainIntensity() != 0.0f ? 10 : 0;
    } else {
        this->NumOnScreen = rain->GetRainIntensity() != 0.0f ? 20 : 0;
    }
    for (int i = 0; i < this->NumOnScreen; i++) {
        OnScreenRainPointsDef &p = this->Points[i];
        if (!kDripsFrozen) {
            p.timer -= dt;
        }
        if (p.timer <= 0.0f) {
            p.timer = p.Maxtimer;
            p.x = bRandom(1.0f);
            p.y = bRandom(1.0f);
            continue;
        }
        float y = 0.2f * p.DripSpeed * dt + p.y; // 0x904B2C
        p.y = y;
        if (y <= 1.0f && p.x <= 1.0f && p.x >= 0.0f) {
            // x += 0 (see above)
        } else {
            p.timer = 0.0f;
        }
        if (speed > 0.0025f) { // 0x904B34
            p.DripShape = (p.DripShape + 1) & 3;
        }
    }
}

// 0x73CFA0.
void OnScreenRain::GetData(int index, float *x, float *y, float *decay, float *size, int *dripShape) {
    const OnScreenRainPointsDef &p = this->Points[index];
    *x = p.x;
    *y = p.y;
    *decay = p.timer / p.Maxtimer;
    *size = p.Size;
    if (dripShape != nullptr) {
        *dripShape = p.DripShape;
    }
}

// 0x73C6B0.
void Rain::Init(RainType type, float percent) {
    this->texture_info[RAIN] = GetTextureInfo(bStringHash("RAINDROP"), 0, 0);
    this->NumRainPoints = MAXRAINPOINTS;
    for (int j = 0; j < 2; j++) {
        this->NumOfType[j] = 0;
        this->DesiredNumOfType[j] = 0;
    }
    this->NewSwapBuffer = 0;
    this->OldSwapBuffer = 1;
    this->NumOfType[type] = this->NumRainPoints;
    this->precipWindEffect[RAIN][0] = RAINwindEffect;
    this->precipWindEffect[INACTIVE][0] = 1.0f;
    this->precipWindEffect[INACTIVE][1] = 1.0f;
    this->precipWindEffect[RAIN][1] = RAINwindEffect * 0.5f;
    this->precipRadius[RAIN] = bVector3(RAINRadiusX, RAINRadiusY, RAINRadiusZ);
    this->precipSpeedRange[RAIN] = bVector3(RAINX, RAINY, RAINZ);
    this->precipZconstant[RAIN] = RAINZconstant;
    this->windType[RAIN] = VECTOR_WIND;
    this->windTime = 0.0f;
    this->windSpeed = bVector3(0.0f, 0.0f, 0.0f);
    for (int32 i = 0; i < this->NumRainPoints; i++) {
        this->RainPointsInf[i].status = 2;
        this->RainPointsInf[i].type = static_cast<uint8>(type);
        this->RainPointsInf[i].subType = static_cast<uint8>(bRandom(2));
        bVector3 &p = this->RainPoints[i].NormalizedPoint[this->NewSwapBuffer];
        p.x = bRandom(precipBoundX) - precipBoundX * 0.5f + precipAheadX;
        p.y = bRandom(precipBoundY) - precipBoundY * 0.5f + precipAheadY;
        p.z = bRandom(precipBoundZ) + precipAheadZ;
        this->RainPoints[i].NormalizedPoint[this->OldSwapBuffer] = p;
    }
    // Velocities for both types (INACTIVE's speed range and z constant are
    // whatever the object holds; the PC never sets them either).
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 10; i++) {
            this->Velocities[j][i].x = bRandom(this->precipSpeedRange[j].x) - this->precipSpeedRange[j].x * 0.5f;
            this->Velocities[j][i].y = bRandom(this->precipSpeedRange[j].y) - this->precipSpeedRange[j].y * 0.5f;
            this->Velocities[j][i].z = -bRandom(this->precipSpeedRange[j].z) - this->precipZconstant[j];
            this->Velocities[j][i].pad = -99999.9f;
        }
    }
}

// 0x73CC80: the active (RAIN) share of the 350 points.
void Rain::SetRainIntensity(float percent) {
    this->intensity = AlwaysRain() ? GeneralRainAmount : percent;
    int inactive = static_cast<int>((1.0f - this->intensity) * 350.0f);
    this->DesiredNumOfType[INACTIVE] = inactive;
    this->DesiredActive = 350 - inactive;
    this->Percentages[INACTIVE] = 1.0f - this->Percentages[RAIN];
}

// 0x753B50: the prevailing wind from the "Wind" map (field 1, degrees),
// scaled by 0.01 into rain space, and gusts: every 1..6 s a new target
// within +-0.025 x 25 of it, approached over a second.
void Rain::Wind(float time) {
    int index = this->MyView->GetID() - 1;
    if (index >= 0 && index < 2 && WindAccessor[index].IsValid()) {
        bVector3 *pos = this->MyView->GetCamera()->GetPosition();
        WindAccessor[index].CaptureData(pos->x, pos->y, 50.0f);
        bAngle a = DegreesToAngle(WindAccessor[index].GetDataFloat(1));
        this->PrevailingWindSpeed.x = bSin(a);
        this->PrevailingWindSpeed.y = bCos(a);
        this->PrevailingWindSpeed.z = 0.0f;
    }
    bVector3 &w = this->PrevailingWindSpeed;
    w.x *= PrevailingMult;
    w.y *= PrevailingMult;
    w.z *= PrevailingMult;
    // eMulVector(bVector4) with the vector's w: taken as a direction (0).
    const bMatrix4 &m = this->world2localrot;
    bVector3 r(w.x * m.v0.x + w.y * m.v1.x + w.z * m.v2.x, w.x * m.v0.y + w.y * m.v1.y + w.z * m.v2.y,
               w.x * m.v0.z + w.y * m.v1.z + w.z * m.v2.z);
    w = r;
    if (gWindState == 0) {
        this->windTime += time;
        if (this->DesiredWindTime <= this->windTime) {
            gWindState = 1;
        }
    } else if (gWindState == 1) {
        this->DesiredWindTime = bRandom(5.0f) + 1.0f;
        this->windTime = 0.0f;
        gWindState = 2;
        this->DesiredwindSpeed.x = (bRandom(0.05f) - 0.025f) * maxWindEffect + w.x;
        this->DesiredwindSpeed.y = (bRandom(0.05f) - 0.025f) * maxWindEffect + w.y;
        this->DesiredwindSpeed.z = w.z;
    } else if (gWindState == 2) {
        this->windSpeed.x += (this->DesiredwindSpeed.x - this->windSpeed.x) * time;
        this->windSpeed.y += (this->DesiredwindSpeed.y - this->windSpeed.y) * time;
        gWindChangeTime += time;
        if (gWindChangeTime >= 1.0f) {
            gWindChangeTime = 0.0f;
            gWindState = 0;
        }
    }
}

// 0x757770. Rain space: x forward along the followed car (or the camera
// direction), z up, origin at the car (10 m ahead looking back) or a metre
// in front of the camera; the points live in [aheadX +- boundX/2] x [+-
// boundY/2] x [0, boundZ], fall by their velocity plus the wind and minus
// the camera's velocity (x -0.03), wrap around the box sideways and
// respawn at the top below z 0. Also: the tunnel/overpass flags (track path
// zones 3, 4/5 and 7 whose elevation is above the point) and NoRain under
// them.
void Rain::Update() {
    if (TheGameFlowManager.GetState() != GAMEFLOW_STATE_RACING || kRainFrozen) {
        return;
    }
    float f = WorldTimeElapsed * 30.0f; // 0x8B2F58
    if (f > 1.0f) {
        f = 1.0f;
    }
    f = 1.0f - f;
    const float step = 1.0f - f * f * f;
    Camera *camera = this->MyView->GetCamera();
    CameraMover *mover = this->MyView->GetCameraMover();
    CameraAnchor *anchor = mover != nullptr ? mover->GetAnchor() : nullptr;
    const bool drive = mover != nullptr && mover->GetType() == CM_DRIVE_CUBIC;
    bVector3 pos, dir;
    if (drive && anchor != nullptr) {
        this->local2world = *anchor->GetGeometryOrientation();
        const bVector4 &fwd = this->local2world.v0;
        dir = bVector3(fwd.x, fwd.y, fwd.z);
        pos = *anchor->GetGeometryPosition();
        if (mover->GetLookbackAngle() != 0) {
            pos.x += dir.x * 10.0f;
            pos.y += dir.y * 10.0f;
            pos.z += dir.z * 10.0f;
        }
    } else {
        this->local2world = *camera->GetCameraMatrix();
        this->local2world.v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
        dir = *camera->GetDirection();
        const bVector3 *p = camera->GetPosition();
        pos = bVector3(p->x + dir.x, p->y + dir.y, p->z + dir.z); // x 1.0 (0x904AE0)
    }
    const bVector3 *cam = camera->GetPosition();
    this->twoDpos = bVector2(cam->x, cam->y);
    this->inTunnel = 0;
    this->inOverpass = 0;
    float best = 9999.0f;
    TrackPathZone *zone = TheTrackPathManager.FindZone(&this->twoDpos, TRACK_PATH_ZONE_TUNNEL, nullptr);
    if (zone != nullptr && pos.z < zone->Elevation) {
        best = zone->Elevation;
        this->inTunnel = 1;
        this->the_zone = zone;
    }
    static const eTrackPathZoneType kOver[3] = {TRACK_PATH_ZONE_OVERPASS, TRACK_PATH_ZONE_OVERPASS_SMALL, TRACK_PATH_ZONE_GARAGE};
    for (int i = 0; i < 3; i++) {
        zone = TheTrackPathManager.FindZone(&this->twoDpos, kOver[i], nullptr);
        if (zone != nullptr && pos.z < zone->Elevation && zone->Elevation < best) {
            if (i < 2) {
                best = zone->Elevation;
            }
            this->the_zone = zone;
            this->inOverpass = 1;
        }
    }
    bInvertMatrix(&this->world2localrot, &this->local2world);
    this->Wind(WorldTimeElapsed);
    // sub_73CCF0(0, precipPERCENT)
    this->DesiredNumOfType[RAIN] = static_cast<uint32>(static_cast<float>(this->DesiredActive) * precipPERCENT);
    this->Percentages[RAIN] = precipPERCENT;
    if (drive) {
        const bVector3 *v = camera->GetVelocityPosition();
        const bMatrix4 &m = this->world2localrot;
        bVector3 &c = this->CamVelLOCAL;
        c.x = (v->x * m.v0.x + v->y * m.v1.x + v->z * m.v2.x) * driveFactor;
        c.y = 0.0f;
        c.z = (v->x * m.v0.z + v->y * m.v1.z + v->z * m.v2.z) * driveFactor;
        float len = bLength(&c);
        this->LenModifier = len;
        if (len > 1.0f) {
            this->LenModifier = 1.0f;
            bNormalize(&c, &c);
        }
    } else {
        this->CamVelLOCAL = bVector3(0.0f, 0.0f, 0.0f);
    }
    if (AlwaysRain()) {
        // 0x74AB20: the tweakables again (constant here), new velocities
        // when RAINZconstant changed.
        this->precipWindEffect[RAIN][0] = RAINwindEffect;
        this->precipWindEffect[RAIN][1] = RAINwindEffect * 0.5f;
        this->precipRadius[RAIN] = bVector3(RAINRadiusX, RAINRadiusY, RAINRadiusZ);
        this->precipSpeedRange[RAIN] = bVector3(RAINX, RAINY, RAINZ);
        this->precipZconstant[RAIN] = RAINZconstant;
        if (gLastZConstant != RAINZconstant) {
            for (int i = 0; i < 10; i++) {
                this->Velocities[RAIN][i].x = bRandom(RAINX) - RAINX * 0.5f;
                this->Velocities[RAIN][i].y = bRandom(RAINY) - RAINY * 0.5f;
                this->Velocities[RAIN][i].z = -bRandom(RAINZ) - RAINZconstant;
            }
            gLastZConstant = RAINZconstant;
        }
    }
    bNormalize(&dir, &dir);
    uint16 heading = static_cast<uint16>(0x4000 - bASin(dir.x));
    if (dir.y < 0.0f) {
        heading = static_cast<uint16>(0xFBB4 - heading);
    }
    bMatrix4 rot;
    eCreateRotationZ(&rot, heading);
    rot.v3 = bVector4(pos.x, pos.y, pos.z, 1.0f);
    this->local2world = rot;
    // Turning: the points swing about the origin by the heading change
    // (clamped to +-400) x 0.00005.
    const int view_index = this->MyView->GetID() != 1 ? 1 : 0;
    float turn = static_cast<float>(static_cast<uint16>(gPrevHeading[view_index] - heading));
    gPrevHeading[view_index] = heading;
    if (turn >= 32768.0f) {
        turn -= 65536.0f;
    }
    if (turn > 400.0f) {
        turn = 400.0f;
    } else if (turn < -400.0f) {
        turn = -400.0f;
    }
    uint32 swap = this->OldSwapBuffer;
    this->OldSwapBuffer = this->NewSwapBuffer;
    this->NewSwapBuffer = swap;
    this->aabbMax.x = precipBoundX * 0.5f + precipAheadX;
    this->aabbMin.x = precipBoundX * -0.5f + precipAheadX;
    this->aabbMax.y = precipBoundY * 0.5f + precipAheadY;
    this->aabbMin.y = precipBoundY * -0.5f + precipAheadY;
    // 0x757CB1, as the PC does it (unsigned; the counts always add up to 350
    // after SetRainIntensity, so this doesn't change anything).
    const uint32 diff = 350 - (this->DesiredNumOfType[0] + this->DesiredNumOfType[1]);
    if (diff != 0) {
        for (int i = 0; i < 2; i++) {
            if (this->DesiredNumOfType[i] >= diff) {
                this->DesiredNumOfType[i] -= diff;
                break;
            }
        }
    }
    bool settled = this->NumOfType[0] == this->DesiredNumOfType[0] && this->NumOfType[1] == this->DesiredNumOfType[1];
    this->NoRain = 0;
    this->twoDpos = bVector2(pos.x, pos.y);
    if (this->inOverpass != 0 || this->inTunnel != 0) {
        this->NoRain = 1;
    }
    this->NoRainAhead = this->NoRain;
    this->renderCount = 0;
    const float swing = turn * 0.00005f; // 0x8B2FE8
    for (int i = 0; i < this->NumRainPoints; i++) {
        RainPointsDef2 &inf = this->RainPointsInf[i];
        bVector3 &np = this->RainPoints[i].NormalizedPoint[this->NewSwapBuffer];
        bVector3 &op = this->RainPoints[i].NormalizedPoint[this->OldSwapBuffer];
        if (inf.status == 0) {
            this->renderCount++;
            np.y += swing * np.x;
            op.y += swing * op.x;
            const float we = this->precipWindEffect[inf.type][inf.subType];
            const bVector3 &v = this->Velocities[inf.type][i % 10];
            np.x = this->CamVelLOCAL.x + op.x + (we * this->windSpeed.x + v.x) * step;
            np.y = op.y + this->CamVelLOCAL.y + (we * this->windSpeed.y + v.y) * step;
            np.z = this->CamVelLOCAL.z + op.z + (we * this->windSpeed.z + v.z) * step;
            if (np.z >= 0.0f) {
                if (np.x > this->aabbMax.x) {
                    np.x -= precipBoundX;
                    op.x -= precipBoundX;
                } else if (np.x < this->aabbMin.x) {
                    np.x += precipBoundX;
                    op.x += precipBoundX;
                } else if (np.y > this->aabbMax.y) {
                    np.y -= precipBoundY;
                    op.y -= precipBoundY;
                } else if (np.y < this->aabbMin.y) {
                    np.y += precipBoundY;
                    op.y += precipBoundY;
                }
            } else {
                inf.status = 1;
                this->renderCount--;
            }
        } else if (inf.status < 3) {
            if (!settled) {
                for (uint32 t = 0; t < 2; t++) {
                    if (inf.type != t && this->NumOfType[t] < this->DesiredNumOfType[t]) {
                        this->NumOfType[inf.type]--;
                        inf.type = static_cast<uint8>(t);
                        this->NumOfType[t]++;
                        break;
                    }
                }
            }
            if (inf.type == INACTIVE) {
                if (inf.status == 1) {
                    np.z = bRandom(precipBoundZ) + precipAheadZ;
                    inf.status = 2;
                }
            } else {
                if (inf.status == 1) {
                    np.z += precipBoundZ;
                    op.z += precipBoundZ;
                }
                inf.status = 0;
                this->renderCount++;
            }
        }
        if (this->NoRain != 0) {
            if (inf.status == 0) {
                this->renderCount--;
            }
            this->NumOfType[inf.type]--;
            inf.type = INACTIVE;
            this->NumOfType[INACTIVE]++;
            inf.status = 2;
        }
    }
}

// 0x74ACC0: each active point as a streak quad from its last position to
// its new one (the IDI_PARTICLES_FX batch 0x9877CC, sub_6C1120 ..
// sub_6D1F60), in rain space with local2world: width RAINRadiusX across the
// view direction, half-length RAINRadiusY + LenModifier, u one of ten 0.1
// strips of RAINDROP, colour 0x80808080 (PRECIPpoly[0]).
void Rain::Render() {
    if (PrecipitationEnable == 0 || precipRENDER == 0 || TheGameFlowManager.GetState() != GAMEFLOW_STATE_RACING || this->renderCount == 0) {
        return;
    }
    const bVector3 *d = this->MyView->GetCamera()->GetDirection();
    const bMatrix4 &m = this->world2localrot;
    const bVector3 view_dir(d->x * m.v0.x + d->y * m.v1.x + d->z * m.v2.x, d->x * m.v0.y + d->y * m.v1.y + d->z * m.v2.y,
                            d->x * m.v0.z + d->y * m.v1.z + d->z * m.v2.z);
    static PortParticleVertex quads[MAXRAINPOINTS * 4];
    for (int type = 0; type < 1; type++) {
        if (this->NumOfType[type] == 0) {
            continue;
        }
        TextureInfo *texture = this->texture_info[type];
        if (texture == nullptr || texture->GetPlatInfo() == nullptr) {
            return;
        }
        uint32 color = 0;
        std::memcpy(&color, this->PRECIPpoly[0].Colours[0], 4); // bytes B, G, R, A as the D3DCOLOR
        int n = 0;
        for (int i = 0; i < this->NumRainPoints; i++) {
            const RainPointsDef2 &inf = this->RainPointsInf[i];
            if (inf.type != type || inf.status != 0) {
                continue;
            }
            const bVector3 &np = this->RainPoints[i].NormalizedPoint[this->NewSwapBuffer];
            const bVector3 &op = this->RainPoints[i].NormalizedPoint[this->OldSwapBuffer];
            bVector3 len(op.x - np.x, op.y - np.y, op.z - np.z);
            bNormalize(&len, &len);
            bVector3 wid;
            bCross(&wid, &view_dir, &len);
            const float rx = this->precipRadius[type].x;
            wid = bVector3(wid.x * rx, wid.y * rx, wid.z * rx);
            const float ry = this->precipRadius[type].y + this->LenModifier;
            len = bVector3(len.x * ry, len.y * ry, len.z * ry);
            const float a = static_cast<float>(i % 10) * 0.1f;
            const float u0 = a + 0.1f, u1 = a, v0 = 1.0f, v1 = 0.0f;
            // sub_6D1E30: p - (B + A), p + (A - B), p + (A + B), p - (A - B).
            PortParticleVertex *q = &quads[n * 4];
            q[0] = {np.x - (len.x + wid.x), np.y - (len.y + wid.y), np.z - (len.z + wid.z), color, u0, v0};
            q[1] = {np.x + (wid.x - len.x), np.y + (wid.y - len.y), np.z + (wid.z - len.z), color, u1, v0};
            q[2] = {np.x + (wid.x + len.x), np.y + (wid.y + len.y), np.z + (wid.z + len.z), color, u1, v1};
            q[3] = {np.x - (wid.x - len.x), np.y - (wid.y - len.y), np.z - (wid.z - len.z), color, u0, v1};
            n++;
        }
        if (n == 0) {
            continue;
        }
        uint32_t flags = 0;
        switch (texture->AlphaBlendType) {
            case TEXBLEND_SRCCOPY:
                break;
            case TEXBLEND_ADDATIVE:
            case TEXBLEND_DEST_ADDATIVE:
                flags |= PORT_3D_ADDITIVE;
                break;
            default:
                flags |= PORT_3D_ALPHA_BLEND;
                break;
        }
        if (texture->AlphaUsageType == TEXUSAGE_PUNCHTHRU) {
            flags |= PORT_3D_ALPHA_TEST;
        }
        PortMetal3DAddParticles(texture->GetPlatInfo()->MetalTexture, flags, reinterpret_cast<const float *>(&this->local2world), quads, n);
    }
}

// 0x758100.
void Rain::UpdateAndRender() {
    const float dt = WorldTimeElapsed;
    if (TheGameFlowManager.GetState() != GAMEFLOW_STATE_RACING) {
        return;
    }
    if (TheGameFlowManager.IsPaused()) {
        // The precipitation fog tint (black by default) at the cloud
        // intensity (x 8 split screen while raining), and the streaks as
        // they were.
        ScreenEffectDef def = {};
        def.r = static_cast<float>(this->fogR);
        def.g = static_cast<float>(this->fogG);
        def.b = static_cast<float>(this->fogB);
        def.a = 128.0f;
        def.intensity = eGetCurrentViewMode() >= 3 && this->intensity > 0.0f ? this->CloudIntensity * 8.0f : this->CloudIntensity;
        def.UpdateFnc = nullptr;
        if (this->MyView->ScreenEffects != nullptr) {
            this->MyView->ScreenEffects->AddScreenEffect(SE_TINT, &def, 1, static_cast<ScreenEffectControl>(0));
        }
        this->Render();
        return;
    }
    this->OSrain.Update(this->MyView);
    windAng += wspeed * dt;
    if (windAng > 360.0f) {
        windAng = 0.0f;
    }
    if (PrecipitationEnable == 0) {
        this->inOverpass = 0;
        this->inTunnel = 0;
        int t = AmIinATunnelSlow(this->MyView, 1);
        this->inTunnel = t;
        if (t & 2) {
            this->inOverpass = 1;
            this->inTunnel = 0;
        }
        if (this->inTunnel != 0) {
            this->inTunnel = 1;
        }
        this->intensity = 0.0f;
        this->RoadDampness = 0.0f;
        return;
    }
    if (!AlwaysRain()) {
        this->DesiredRoadDampness = this->intensity == 0.0f ? 0.0f : kRoadReflection;
    } else {
        this->RoadDampness = kRoadReflection;
    }
    float damp = (this->DesiredRoadDampness - this->RoadDampness) * kWetRamp + this->RoadDampness;
    if (this->DesiredRoadDampness <= this->RoadDampness) {
        this->RoadDampness = damp < 0.05f ? 0.0f : damp; // 0x8B2E60
    } else {
        this->RoadDampness = damp;
    }
    if (RainAccessor.IsValid() && CloudAccessor.IsValid()) {
        const bVector3 *p = this->MyView->GetCamera()->GetPosition();
        float rain = GetDesiredRainIntensity(p->x, p->y);
        this->DesiredIntensity = rain;
        this->DesiredCloudyness = rain > 0.0f ? rain : GetDesiredCloudyness(p->x, p->y);
        if (eGetCurrentViewMode() > 2) {
            this->DesiredIntensity *= 0.125f;
        }
    }
    const float inv = 1.0f / (dt == 0.0f ? 0.001f : dt);
    this->intensity += (this->DesiredIntensity - this->intensity) / (inv * kRateOfChange);
    if (this->DesiredIntensity == 0.0f && this->intensity < 0.01f) {
        this->intensity = 0.0f;
    }
    if (this->intensity > 1.0f) {
        this->intensity = 1.0f;
    }
    this->CloudIntensity += (this->DesiredCloudyness - this->CloudIntensity) / (inv * kCloudRateOfChange);
    if (this->DesiredCloudyness == 0.0f && this->CloudIntensity < twkCloudsMinAmount + 0.01f) {
        this->CloudIntensity = twkCloudsMinAmount;
    }
    if (this->CloudIntensity > 1.0f) {
        this->CloudIntensity = 1.0f;
    }
    this->SetRainIntensity(this->intensity);
    this->percentPrecip[RAIN] = 1.0f;
    this->Update();
    this->Render();
    if (TraceRain()) {
        static bool dumped;
        if (!dumped && RainAccessor.IsValid() && CloudAccessor.IsValid()) {
            // Where the weather maps make it rain (offline: no override),
            // on a 200 m grid: R rain (amount x 10), c clouds only.
            dumped = true;
            float saved = rainOverrideIntensity;
            rainOverrideIntensity = 0.0f;
            for (int iy = 0; iy < 40; iy++) {
                char line[64];
                float y = 4000.0f - iy * 200.0f;
                for (int ix = 0; ix < 40; ix++) {
                    float x = -4000.0f + ix * 200.0f;
                    float r = GetDesiredRainIntensity(x, y);
                    float c = GetDesiredCloudyness(x, y);
                    line[ix] = r > 0.0f ? static_cast<char>('0' + (r >= 0.95f ? 9 : static_cast<int>(r * 10.0f))) : c > 0.0f ? 'c' : '.';
                }
                line[40] = '\0';
                std::fprintf(stderr, "[rain map] y %5.0f %s\n", y, line);
            }
            rainOverrideIntensity = saved;
        }
        static int frame;
        if (frame++ % 120 == 0) {
            const bVector3 *p = this->MyView->GetCamera()->GetPosition();
            std::fprintf(stderr,
                         "[rain] at (%.0f %.0f) maps rain %d clouds %d wind %d override %.2f: desired rain %.2f cloud %.2f -> intensity %.2f "
                         "cloud %.2f damp %.2f, points %d drawn, tunnel %d overpass %d, drops %d\n",
                         p->x, p->y, RainAccessor.IsValid(), CloudAccessor.IsValid(), WindAccessor[0].IsValid(), rainOverrideIntensity,
                         this->DesiredIntensity, this->DesiredCloudyness, this->intensity, this->CloudIntensity, this->RoadDampness,
                         this->renderCount, this->inTunnel, this->inOverpass, this->OSrain.GetNumOnScreen());
        }
    }
    // TODO(port): the rain curtains at tunnel mouths while inside one
    // (IsValidRainCurtainPos, sub_74A070 / sub_74A160 / sub_74A320 /
    // sub_74A5D0) aren't ported.
    Rain *rain = this->MyView->Precipitation;
    if (rain == nullptr || (rain->inTunnel == 0 && rain->inOverpass == 0)) {
        this->IsValidRainCurtainPos = CT_INACTIVE;
    }
}

// 0x74B000: 1 in a tunnel zone below its elevation, + 2 under an overpass
// (zones 4, 5, 7) with CheckOverPass, at the view's anchor.
int AmIinATunnelSlow(eView *view, int CheckOverPass) {
    CameraMover *mover = view->GetCameraMover();
    CameraAnchor *anchor = mover != nullptr ? mover->GetAnchor() : nullptr;
    if (anchor == nullptr) {
        return 0;
    }
    const bVector3 *p = anchor->GetGeometryPosition();
    bVector2 xy(p->x, p->y);
    int r = 0;
    TrackPathZone *zone = TheTrackPathManager.FindZone(&xy, TRACK_PATH_ZONE_TUNNEL, nullptr);
    if (zone != nullptr && p->z < zone->Elevation) {
        r = 1;
    }
    if (CheckOverPass == 0) {
        return r;
    }
    static const eTrackPathZoneType kOver[3] = {TRACK_PATH_ZONE_OVERPASS, TRACK_PATH_ZONE_OVERPASS_SMALL, TRACK_PATH_ZONE_GARAGE};
    for (int i = 0; i < 3; i++) {
        zone = TheTrackPathManager.FindZone(&xy, kOver[i], nullptr);
        if (zone != nullptr && p->z < zone->Elevation) {
            return r + 2;
        }
    }
    return r;
}

// ___eDisplayFrame 0x6DF63A / sub_6D4620: with Rain Effect (g_RainEnable)
// and the Particle option on in a race, player 1's on-screen drops refract
// the frame (IDI_RAIN_DROP_FX) before the visual treatment.
void PortRainDropsFrame() {
    if (TheGameFlowManager.GetState() != GAMEFLOW_STATE_RACING || gPCGraphicsOptions.RainEnable == 0 ||
        gPCGraphicsOptions.ParticleSystemEnable == 0) {
        return;
    }
    eView *view = eGetView(1, false);
    Rain *rain = view != nullptr ? view->Precipitation : nullptr;
    if (rain == nullptr || rain->OSrain.GetNumOnScreen() == 0) {
        return;
    }
    static const unsigned int hash = bStringHash("RAINDROPPC");
    TextureInfo *displace = GetTextureInfo(hash, 1, 0);
    if (displace == nullptr || displace->GetPlatInfo() == nullptr) {
        return;
    }
    PortRainDrop drops[MAX_OS_RAINPOINTS];
    int n = rain->OSrain.GetNumOnScreen();
    for (int i = 0; i < n; i++) {
        float x, y, decay, size;
        rain->OSrain.GetData(i, &x, &y, &decay, &size, nullptr);
        drops[i].x = x;
        drops[i].y = y;
        drops[i].alpha = decay * 3.0f; // 0x8959E4
        drops[i].size = size * 0.12f;  // 0x8AF9F4
    }
    PortMetal3DSetRainDrops(displace->GetPlatInfo()->MetalTexture, drops, n);
}
