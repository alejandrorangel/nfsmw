// ICE ("in-car editor") cinematic cameras, reconstructed from speed.exe 1.3.
// The decomp has only declarations for these:
//
//   chunks       LoaderICECameras 0x4788E0  UnloaderICECameras 0x47FB40
//                ICEManager::LoadCameraSet 0x4787C0  UnloadCameraSet 0x47F9F0
//                LoadCameraShakes 0x478890  AddCameraGroup 0x4786E0
//                GetCameraGroup 0x46E760  Init 0x47F310  Resolve 0x478320
//   tracks       SetGenericCameraToPlay 0x46E2D0  ChooseCameraPlaybackTrack 0x483D30
//                ChooseGenericCamera 0x4785F0  GetCameraData(uint, int) 0x47B3F0
//                GetNumSceneCameraTrack 0x46E710  GetParameter 0x47B3A0
//                ICETrack::GetParameter 0x47B030  GetKeyNumber 0x46D2E0
//                ICETrack::GetCameraData 0x47B150  GetNeighbour 0x46E5E0
//                GetIntervalSize 0x46E530  IsContinuous 0x477EE0 / 0x46D0A0
//                GetSlope 0x47F490  ICEData::GetEye 0x47EDB0  GetLook 0x47EE00
//                GetShakeTrack 0x478290
//   overlays     ICE::ShowOverlay 0x46EA50  HideOverlay 0x46EAE0  GetOverlayName 0x46E9C0
//   ICEAnchor    ctor 0x46D500  Update 0x46D3F0
//   ICEMover     ctor 0x4846C0  dtor 0x478000  SetDesired 0x482160  Update 0x482EC0
//                GetEye 0x46DD10  GetLook 0x46DDD0  GetDutch 0x46DE90  GetFOV 0x46DEE0
//                view matrix 0x46D800  smooth-entry helpers 0x46DA10 / 0x46DB30 /
//                0x46DC50 / 0x46DCA0  DOF 0x46D5E0 / 0x46D680
//   CDActionIce  Construct 0x485EE0  ctor 0x4856E0  dtor 0x473CD0  Update 0x484270
//                AquireCar 0x473FD0  GetName 0x473E00  GetNext 0x473E80
//
// ICE camera chunks are in-place 32-bit data: each track keeps its PC layout
// (bTNode links and the group pointer are 4-byte slots we never use as
// pointers); the native side indexes the tracks through its own group tables.

#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Camera/CameraMover.hpp"
#include "Speed/Indep/Src/Camera/ICE/ICEAnimScene.hpp"
#include "Speed/Indep/Src/Camera/ICE/ICEData.hpp"
#include "Speed/Indep/Src/Camera/ICE/ICEManager.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Generated/Events/ELoadingScreenOff.hpp"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Generated/Messages/MICECameraFinished.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/INIS.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/ICollisionBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/IEngine.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRigidBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISuspension.h"
#include "Speed/Indep/Src/Interfaces/Simables/ITransmission.h"
#include "Speed/Indep/Src/Interfaces/Simables/IVehicle.h"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#include "Speed/Indep/Src/Misc/Point.hpp"
#include "Speed/Indep/Src/Misc/Timer.hpp"
#include "Speed/Indep/Src/World/WorldConn.h"
#include "Speed/Indep/bWare/Inc/bChunk.hpp"
#include "Speed/Indep/bWare/Inc/bPrintf.hpp"
#include "Speed/Indep/bWare/Inc/Strings.hpp"
#include "Speed/Indep/Src/FEng/FEngine.h"
#include "CameraAINative.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <vector>

void FlushAccumulationBuffer();

// ---------------------------------------------------------------------------
// In-place chunk layouts (PC, 32-bit)
// ---------------------------------------------------------------------------

static_assert(sizeof(ICEData) == 0x84, "ICEData is loaded in place");
static_assert(offsetof(ICEData, fParameter) == 0x10, "ICEData layout");
static_assert(offsetof(ICEData, fDutch) == 0x4C, "ICEData layout");
static_assert(offsetof(ICEData, fAperture) == 0x7C, "ICEData layout");
static_assert(sizeof(ICEShakeData) == 0x18, "ICEShakeData is loaded in place");

namespace {

// ICETrack as it sits in the chunk (0x28 header + NumKeys * ICEData).
struct ICETrackImage {
    uint32 Next;    // 0x00 bTNode<ICETrack> (32-bit, unused natively)
    uint32 Prev;    // 0x04
    uint32 Group;   // 0x08 natively: group context + 1 (0 = none)
    float Start;    // 0x0C playback start (timer seconds)
    float Length;   // 0x10 seconds
    int16 NumKeys;  // 0x14
    int8 Allocated; // 0x16
    char Name[14];  // 0x17

    ICEData *GetKey(int n) {
        return reinterpret_cast<ICEData *>(reinterpret_cast<char *>(this) + 0x28) + n;
    }
    int GetContext() const {
        return static_cast<int>(Group) - 1;
    }
};
static_assert(offsetof(ICETrackImage, Start) == 0x0C, "ICETrack layout");
static_assert(offsetof(ICETrackImage, NumKeys) == 0x14, "ICETrack layout");
static_assert(offsetof(ICETrackImage, Name) == 0x17, "ICETrack layout");
static_assert(sizeof(ICETrackImage) == 0x28, "ICETrack header is 0x28 bytes");

// ICEShakeTrack in the chunk (0x20 header + NumKeys * ICEShakeData).
struct ICEShakeImage {
    uint32 Next;    // 0x00
    uint32 Prev;    // 0x04
    uint32 Group;   // 0x08
    int16 NumKeys;  // 0x0C
    int8 Allocated; // 0x0E
    char Name[14];  // 0x0F

    const ICEShakeData *GetKey(int n) const {
        return reinterpret_cast<const ICEShakeData *>(reinterpret_cast<const char *>(this) + 0x20) + n;
    }
};
static_assert(offsetof(ICEShakeImage, NumKeys) == 0x0C, "ICEShakeTrack layout");
static_assert(offsetof(ICEShakeImage, Name) == 0x0F, "ICEShakeTrack layout");
static_assert(sizeof(ICEShakeImage) == 0x20, "ICEShakeTrack header is 0x20 bytes");

// A camera set sub-chunk: {id, size, handle, num tracks, tracks...}.
struct ICESetImage {
    uint32 ID;
    uint32 Size;
    uint32 Handle;
    int32 NumTracks;
};
static_assert(sizeof(ICESetImage) == 0x10, "ICE set header");

const unsigned int kChunkNisCameras = 0x8003B200;
const unsigned int kChunkGenericCameras = 0x8003B203;
const unsigned int kChunkCameraShakes = 0x3B211;

struct ICENativeGroup {
    uint32 Handle;
    std::vector<ICETrackImage *> Tracks;
};

// ICEManager pNisCameras.. pGenericCameras (0x100 / 10 / 50 / 50 groups).
const int kMaxGroups[eDCE_NUM_CONTEXTS] = {0x100, 10, 0x32, 0x32};
std::vector<ICENativeGroup> sGroups[eDCE_NUM_CONTEXTS];
std::vector<ICEShakeImage *> sShakes; // pShakeGroup
ICETrackImage *sPlaybackTrack;        // ICEManager::pPlaybackTrack (+0x24)
bool sMirror;                         // byte_9110A0: mirrored replay cameras
bool sReplayCameras;                  // dword_91103A: debug "replay cameras" toggle (0x42A810), off
bool sUseOldDutch;                    // bUseOldDutch 0x8EDEE0
unsigned char gOverlay;               // 0x9110A1: shown overlay, 0x80 = pop pending

bool TraceICE() {
    static int trace = -1;
    if (trace < 0) {
        const char *env = std::getenv("NFSMW_TRACE_ICE");
        trace = env != nullptr && env[0] != '\0' && env[0] != '0';
    }
    return trace != 0;
}

ICENativeGroup *GetGroup(int context, uint32 handle) {
    if (context < 0 || context >= eDCE_NUM_CONTEXTS) {
        return nullptr;
    }
    for (ICENativeGroup &g : sGroups[context]) {
        if (g.Handle == handle) {
            return &g;
        }
    }
    return nullptr;
}

ICENativeGroup *AddGroup(int context, uint32 handle) {
    if (ICENativeGroup *g = GetGroup(context, handle)) {
        return g;
    }
    if (context < 0 || context >= eDCE_NUM_CONTEXTS || static_cast<int>(sGroups[context].size()) >= kMaxGroups[context]) {
        return nullptr;
    }
    sGroups[context].push_back(ICENativeGroup{handle, {}});
    return &sGroups[context].back();
}

ICETrackImage *GetTrack(ICENativeGroup *group, const char *name) {
    for (ICETrackImage *t : group->Tracks) {
        if (bStrCmp(name, t->Name) == 0) {
            return t;
        }
    }
    return nullptr;
}

float TimerSeconds() {
    return TheICEManager.IsUsingRealTime() ? RealTimer.GetSeconds() : WorldTimer.GetSeconds();
}

ICEScene *GetNISScene() {
    INIS *nis = INIS::Get();
    return nis != nullptr ? nis->GetScene() : nullptr;
}

// --- tracks ---------------------------------------------------------------

// ICETrack::GetParameter (0x47B030): playback position over the track, 0..1.
float TrackGetParameter(ICETrackImage *track) {
    switch (track->GetContext()) {
        case eDCE_NIS: {
            ICEScene *scene = GetNISScene();
            if (scene == nullptr) {
                return 0.0f;
            }
            float start = scene->GetTimeStart();
            return (scene->GetTimeElapsed() - start) / (scene->GetTimeTotalLength() - start);
        }
        case eDCE_FMV:
        case eDCE_GENERIC:
        case eDCE_REPLAY: { // TODO(port): replay tracks run off the replay clock (0x46E290)
            if (track->Length <= 0.0f) {
                return 0.0f;
            }
            float t = (TimerSeconds() - track->Start) / track->Length;
            return t > 1.0f ? 1.0f : t;
        }
        default:
            return 0.0f;
    }
}

// ICETrack::GetKeyNumber (0x46D2E0): last key starting at or before t.
int TrackGetKeyNumber(ICETrackImage *track, float t) {
    int i = track->NumKeys - 1;
    while (i > 0 && !(track->GetKey(i)->fParameter <= t)) {
        i--;
    }
    return i;
}

float KeyParameter(ICETrackImage *track, int i) {
    if (i < 0) {
        return 0.0f;
    }
    return i < track->NumKeys ? track->GetKey(i)->fParameter : 1.0f;
}

// ICETrack::GetCameraData (0x47B150): the key playing now and its interval.
ICEData *TrackGetCameraData(ICETrackImage *track, float *p_start, float *p_end, float *p_current) {
    float t = TrackGetParameter(track);
    float clamped = t > 0.0f ? (t > 1.0f ? 1.0f : t) : 0.0f;
    if (t != clamped) {
        return nullptr;
    }
    int k = TrackGetKeyNumber(track, t);
    if (p_current != nullptr) {
        *p_current = t;
    }
    if (p_start != nullptr) {
        *p_start = KeyParameter(track, k);
    }
    if (p_end != nullptr) {
        *p_end = KeyParameter(track, k + 1);
    }
    if (k < 0 || k > track->NumKeys - 1) {
        return nullptr;
    }
    return track->GetKey(k);
}

int KeyIndex(ICETrackImage *track, const ICEData *key) {
    return static_cast<int>(key - track->GetKey(0));
}

// ICEManager::GetNeighbour (0x46E5E0): the key after (n != 0) or before.
ICEData *GetNeighbour(ICEData *key, int n, ICETrackImage *track) {
    if (track == nullptr) {
        return nullptr;
    }
    int i = KeyIndex(track, key) + (n != 0 ? 1 : -1);
    if (i < 0 || i > track->NumKeys - 1) {
        return nullptr;
    }
    return track->GetKey(i);
}

// ICEManager::GetIntervalSize (0x46E530).
float GetIntervalSize(ICEData *key, ICETrackImage *track) {
    if (track == nullptr) {
        return 0.0f;
    }
    int i = KeyIndex(track, key);
    float end = KeyParameter(track, i + 1);
    if (i < 0) {
        return end;
    }
    return end - (i < track->NumKeys ? track->GetKey(i)->fParameter : 1.0f);
}

// Eye/look points are vEye[2][3] / vLook[2][3] (the decomp header has the
// dimensions swapped).
const float *KeyPoint(const ICEData *key, int offset, int i) {
    return reinterpret_cast<const float *>(reinterpret_cast<const char *>(key) + offset) + i * 3;
}

void ScenePoint(bVector3 *p) {
    // TODO(port): 0x47B300 raises scene-space points by the animation's
    // elevation fix-up (0x46E150 / 0x46E1D0) when the NIS scene asks for it.
}

// ICEData::GetEye (0x47EDB0) / GetLook (0x47EE00).
void KeyGetEye(const ICEData *key, int i, bVector3 *p) {
    const float *v = KeyPoint(key, 0x1C, i);
    *p = bVector3(v[0], sMirror ? -v[1] : v[1], v[2]);
    if (key->nSpaceEye == eDCE_SCENE_SPACE) {
        ScenePoint(p);
    }
}

void KeyGetLook(const ICEData *key, int i, bVector3 *p) {
    const float *v = KeyPoint(key, 0x34, i);
    *p = bVector3(v[0], sMirror ? -v[1] : v[1], v[2]);
    if (key->nSpaceLook == eDCE_SCENE_SPACE) {
        ScenePoint(p);
    }
}

bool Near(float a, float b) {
    return std::fabs(a - b) <= 0.001f;
}

// 0x46D0A0: same spaces and the same eye/look points.
bool SamePoints(const ICEData *a, int ia, const ICEData *b, int ib) {
    if (a->nType != b->nType || a->nSpaceEye != b->nSpaceEye || a->nSpaceLook != b->nSpaceLook) {
        return false;
    }
    for (int j = 0; j < 3; j++) {
        if (!Near(KeyPoint(a, 0x1C, ia)[j], KeyPoint(b, 0x1C, ib)[j]) || !Near(KeyPoint(a, 0x34, ia)[j], KeyPoint(b, 0x34, ib)[j])) {
            return false;
        }
    }
    return true;
}

// 0x477EE0: does key a's end ia continue smoothly into key b's ib?
bool IsContinuous(const ICEData *a, int ia, const ICEData *b, int ib) {
    return Near(a->fTangentLength[ia], b->fTangentLength[ib]) && Near(a->fDutch[ia], b->fDutch[ib]) && Near(a->fLens[ia], b->fLens[ib]) &&
           SamePoints(a, ia, b, ib);
}

// ICEManager::GetSlope (0x47F490): spline tangents at end n of a key.
void GetSlope(bVector3 *eye_slope, bVector3 *look_slope, float *dutch_slope, float *lens_slope, ICEData *key, int n, ICETrackImage *track) {
    *eye_slope = bVector3(0.0f, 0.0f, 0.0f);
    *look_slope = bVector3(0.0f, 0.0f, 0.0f);
    *dutch_slope = 0.0f;
    *lens_slope = 0.0f;
    if (key->nType == 0) {
        return;
    }
    bVector3 e0, e1, l0, l1;
    ICEData *nb = GetNeighbour(key, n, track);
    float tl = key->fTangentLength[n];
    if (nb == nullptr || nb->nType == 0 || !IsContinuous(key, n, nb, n ^ 1)) {
        KeyGetEye(key, 0, &e0);
        KeyGetEye(key, 1, &e1);
        KeyGetLook(key, 0, &l0);
        KeyGetLook(key, 1, &l1);
        *eye_slope = bVector3((e1.x - e0.x) * tl, (e1.y - e0.y) * tl, (e1.z - e0.z) * tl);
        *look_slope = bVector3((l1.x - l0.x) * tl, (l1.y - l0.y) * tl, (l1.z - l0.z) * tl);
        *dutch_slope = (key->fDutch[1] - key->fDutch[0]) * tl;
        *lens_slope = (key->fLens[1] - key->fLens[0]) * tl;
        return;
    }
    KeyGetEye(nb, 0, &e0);
    KeyGetEye(nb, 1, &e1);
    bVector3 nb_eye = e1 - e0;
    KeyGetLook(nb, 0, &l0);
    KeyGetLook(nb, 1, &l1);
    bVector3 nb_look = l1 - l0;
    float nb_dutch = nb->fDutch[1] - nb->fDutch[0];
    float nb_lens = nb->fLens[1] - nb->fLens[0];
    KeyGetEye(key, 0, &e0);
    KeyGetEye(key, 1, &e1);
    bVector3 my_eye = e1 - e0;
    KeyGetLook(key, 0, &l0);
    KeyGetLook(key, 1, &l1);
    bVector3 my_look = l1 - l0;
    float my_dutch = key->fDutch[1] - key->fDutch[0];
    float my_lens = key->fLens[1] - key->fLens[0];
    float a = GetIntervalSize(key, track);
    float b = GetIntervalSize(nb, track);
    float w = a / (b + a);
    float w2 = 1.0f - w;
    if (b > 0.001f) {
        w = (w / b) * a;
    }
    float f2 = tl * w;
    float f3 = tl * w2;
    *eye_slope = bVector3(nb_eye.x * f2, nb_eye.y * f2, nb_eye.z * f2);
    bScaleAdd(eye_slope, eye_slope, &my_eye, f3);
    *look_slope = bVector3(nb_look.x * f2, nb_look.y * f2, nb_look.z * f2);
    bScaleAdd(look_slope, look_slope, &my_look, f3);
    *dutch_slope = f2 * nb_dutch + f3 * my_dutch;
    *lens_slope = f2 * nb_lens + f3 * my_lens;
}

// ICEManager::ChooseGenericCamera (0x4785F0).
ICETrackImage *PickGenericCamera(uint32 group_hash, const char *track_name) {
    ICENativeGroup *group = GetGroup(eDCE_GENERIC, group_hash);
    if (group == nullptr) {
        return nullptr;
    }
    ICETrackImage *track = GetTrack(group, track_name);
    if (track != nullptr) {
        track->Start = TimerSeconds();
    }
    return track;
}

const ICEShakeImage *GetShakeTrack(uint32 shake_type) {
    if (shake_type == 0) {
        return nullptr;
    }
    for (const ICEShakeImage *t : sShakes) {
        if (bStringHash(t->Name) == shake_type) {
            return t;
        }
    }
    return nullptr;
}

// --- overlays (ICE::ShowOverlay / HideOverlay) -----------------------------

const char *GetOverlayName(unsigned char id) {
    static const struct {
        unsigned char id;
        const char *name;
    } kOverlays[] = {{0, "<NONE>"}, {1, "FLASHERS.fng"}, {2, "BLACK_FADE_OUT.fng"}, {3, "BUSTED_OVERLAY.fng"}, {4, "BLACK_BACKGROUND.fng"}};
    for (const auto &o : kOverlays) {
        if (o.id == id) {
            return o.name;
        }
    }
    return kOverlays[0].name;
}

void ShowOverlay(unsigned char id) {
    if (id != 0 && ((gOverlay & 0x7F) == 0 || (gOverlay & 0x80) != 0)) {
        new ELoadingScreenOff();
        gOverlay = id;
        cFEng::Get()->PushNoControlPackage(GetOverlayName(id), FE_PACKAGE_PRIORITY_SECOND_CLOSEST);
    }
}

void HideOverlay() {
    if ((gOverlay & 0x7F) == 0) {
        return;
    }
    const char *name = GetOverlayName(gOverlay & 0x7F);
    if (cFEng::Get()->IsPackagePushed(name)) {
        cFEng::Get()->PopNoControlPackage(name);
        gOverlay = 0;
        return;
    }
    gOverlay |= 0x80;
}

// --- cubic helpers (the PC's inlined tCubic setters) -----------------------

void InitCubic(tCubic1D &c, short flags, float duration) {
    std::memset(&c, 0, sizeof(c));
    c.duration = duration;
    c.flags = flags;
}

void InitCubic(tCubic3D &c, short flags, float duration) {
    InitCubic(c.x, flags, duration);
    InitCubic(c.y, flags, duration);
    InitCubic(c.z, flags, duration);
}

void SetVal(tCubic3D *c, const bVector3 &v) {
    c->x.SetVal(v.x);
    c->y.SetVal(v.y);
    c->z.SetVal(v.z);
}

void SetdVal(tCubic3D *c, const bVector3 &v) {
    c->x.SetdVal(v.x);
    c->y.SetdVal(v.y);
    c->z.SetdVal(v.z);
}

void SetValDesired(tCubic3D *c, const bVector3 &v) {
    c->x.SetValDesired(v.x);
    c->y.SetValDesired(v.y);
    c->z.SetValDesired(v.z);
}

void SetdValDesired(tCubic3D *c, const bVector3 &v) {
    c->x.dValDesired = v.x;
    c->y.dValDesired = v.y;
    c->z.dValDesired = v.z;
}

void MakeCoeffs(tCubic3D *c) {
    c->x.MakeCoeffs();
    c->y.MakeCoeffs();
    c->z.MakeCoeffs();
}

// Val/ValDesired from a key's two ends, then the coefficients (inlined in 0x482160).
void SetKeyCubic(tCubic1D *c, float v0, float v1) {
    c->SetVal(v0);
    c->SetValDesired(v1);
    c->MakeCoeffs();
}

float Eval(tCubic1D *c, float t) {
    return c->GetVal(t);
}

// Lens (mm) to FOV angle over a 15.96 mm half frame.
float LensToFov(float lens) {
    return static_cast<float>(static_cast<int>((bATan(lens, 15.960001f) & 0x7FFF) << 1));
}

// 0x46D620: a lens slope as an FOV slope.
float LensSlopeToFov(float lens, float slope) {
    short a = static_cast<short>(bATan(lens, 15.960001f) << 1);
    short b = static_cast<short>(bATan(slope * 0.01f + lens, 15.960001f) << 1);
    return (static_cast<float>(b) - static_cast<float>(a)) * 100.0f;
}

// 0x46D5E0: focal length (mm) of an FOV angle.
float FovToFocalLength(unsigned short fov) {
    float t = bSin(static_cast<bAngle>(fov >> 1)) / bCos(static_cast<bAngle>(fov >> 1));
    if (t <= 0.0f) {
        t = 0.0001f;
    }
    return 15.960001f / t;
}

// 0x46D680: aperture setting (1/6 stops) to an f-number.
float ApertureToFStop(float aperture) {
    int i = static_cast<int>(aperture);
    if (i < 1) {
        return 1.0f;
    }
    if (i > 36) {
        i = 36;
    }
    return std::pow(2.0f, static_cast<float>(i) / 6.0f);
}

void QuatToMatrix(bMatrix4 *m, float x, float y, float z, float w) {
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float wx = x2 * w;
    m->v0 = bVector4((1.0f - y2 * y) - z2 * z, z2 * w + y2 * x, z2 * x - y2 * w, 0.0f);
    m->v1 = bVector4(y2 * x - z2 * w, (1.0f - x2 * x) - z2 * z, wx + z2 * y, 0.0f);
    m->v2 = bVector4(y2 * w + z2 * x, z2 * y - wx, (1.0f - x2 * x) - y2 * y, 0.0f);
    m->v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
}

} // namespace

// ---------------------------------------------------------------------------
// ICEManager
// ---------------------------------------------------------------------------

ICEManager::~ICEManager() {}

void ICEManager::Init() {
    for (int c = 0; c < eDCE_NUM_CONTEXTS; c++) {
        sGroups[c].clear();
        sGroups[c].reserve(kMaxGroups[c]);
    }
    sShakes.clear();
    sPlaybackTrack = nullptr;
}

void ICEManager::Resolve() {
    // TODO(port): the NIS / FMV groups named in TheAnimDirectory and the
    // replay categories (0x8EE3A0) get empty groups here on PC; camera set
    // chunks add the groups they fill, so only the generic ones are made.
    static const char *const kGenericCategoryNames[] = {"Cinematics", "Debug"}; // 0x8EDEE4
    for (const char *name : kGenericCategoryNames) {
        AddGroup(eDCE_GENERIC, bStringHash(name));
    }
    nGenericCameras = static_cast<int>(sGroups[eDCE_GENERIC].size());
}

void ICEManager::LoadCameraSet(bChunk *set_chunk) {
    int context = eDCE_NOCONTEXT;
    unsigned int id = set_chunk->GetID();
    if (id >= kChunkNisCameras && id <= kChunkGenericCameras) {
        context = static_cast<int>(id - kChunkNisCameras);
    }
    char *p = set_chunk->GetData();
    char *end = p + set_chunk->GetSize();
    int tracks = 0;
    while (p < end) {
        ICESetImage *set = reinterpret_cast<ICESetImage *>(p);
        ICENativeGroup *group = AddGroup(context, set->Handle);
        if (group != nullptr) {
            char *t = p + sizeof(ICESetImage);
            for (int i = 0; i < set->NumTracks; i++) {
                ICETrackImage *track = reinterpret_cast<ICETrackImage *>(t);
                track->Group = static_cast<uint32>(context + 1);
                group->Tracks.push_back(track);
                tracks++;
                t += sizeof(ICETrackImage) + track->NumKeys * sizeof(ICEData);
            }
        }
        p += 8 + set->Size;
    }
    nNisCameras = static_cast<int>(sGroups[eDCE_NIS].size());
    nFmvCameras = static_cast<int>(sGroups[eDCE_FMV].size());
    nReplayCameras = static_cast<int>(sGroups[eDCE_REPLAY].size());
    nGenericCameras = static_cast<int>(sGroups[eDCE_GENERIC].size());
    if (TraceICE()) {
        std::fprintf(stderr, "[ice] load camera set %08x: %d tracks\n", id, tracks);
    }
}

void ICEManager::UnloadCameraSet(bChunk *set_chunk) {
    int context = eDCE_NOCONTEXT;
    unsigned int id = set_chunk->GetID();
    if (id >= kChunkNisCameras && id <= kChunkGenericCameras) {
        context = static_cast<int>(id - kChunkNisCameras);
    }
    char *begin = set_chunk->GetData();
    char *end = begin + set_chunk->GetSize();
    for (char *p = begin; p < end;) {
        ICESetImage *set = reinterpret_cast<ICESetImage *>(p);
        if (ICENativeGroup *group = GetGroup(context, set->Handle)) {
            group->Tracks.clear();
        }
        p += 8 + set->Size;
    }
    // Native safety: PC leaves pPlaybackTrack dangling until the next choice.
    char *pt = reinterpret_cast<char *>(sPlaybackTrack);
    if (pt >= begin && pt < end) {
        sPlaybackTrack = nullptr;
    }
}

void ICEManager::LoadCameraShakes(bChunk *set_chunk) {
    char *p = set_chunk->GetData();
    int count = *reinterpret_cast<int32 *>(p);
    p += 4;
    for (int i = 0; i < count; i++) {
        ICEShakeImage *track = reinterpret_cast<ICEShakeImage *>(p);
        sShakes.push_back(track);
        p += sizeof(ICEShakeImage) + track->NumKeys * sizeof(ICEShakeData);
    }
}

void ICEManager::UnloadCameraShakes(bChunk *set_chunk) {
    sShakes.clear();
}

void ICEManager::SetGenericCameraToPlay(const char *group_name, const char *track_name) {
    nPlayGenericGroupHash = bStringHash(group_name);
    bStrNCpy(nPlayGenericTrackName, track_name, 13);
    nPlayGenericTrackName[13] = '\0';
    if (TraceICE()) {
        std::fprintf(stderr, "[ice] generic camera '%s' / '%s'\n", group_name, track_name);
    }
}

void ICEManager::ClearGenericCamera() {
    nPlayGenericGroupHash = bStringHash("");
    bStrNCpy(nPlayGenericTrackName, "", 13);
    nPlayGenericTrackName[13] = '\0';
}

bool ICEManager::ChooseCameraPlaybackTrack() {
    sPlaybackTrack = nullptr;
    sUseOldDutch = false;
    ICEScene *scene = GetNISScene();
    if (scene == nullptr) {
        sPlaybackTrack = PickGenericCamera(nPlayGenericGroupHash, nPlayGenericTrackName);
        // TODO(port): falls back to a replay camera (0x4804A0) for replays.
    } else {
        ICENativeGroup *group = GetGroup(eDCE_NIS, scene->GetSceneHash());
        if (group != nullptr) {
            char name[16];
            bSPrintf(name, "Track %d", scene->GetCameraTrackNumber());
            sPlaybackTrack = GetTrack(group, name);
            sUseOldDutch = true;
        }
    }
    if (TraceICE()) {
        std::fprintf(stderr, "[ice] playback track: %s (%.2f s, %d keys)\n", sPlaybackTrack != nullptr ? sPlaybackTrack->Name : "(none)",
                     sPlaybackTrack != nullptr ? sPlaybackTrack->Length : 0.0f, sPlaybackTrack != nullptr ? sPlaybackTrack->NumKeys : 0);
    }
    return sPlaybackTrack != nullptr;
}

ICEData *ICEManager::GetCameraData(uint32 scene_hash, int camTrack) {
    ICENativeGroup *group = GetGroup(eDCE_NIS, scene_hash);
    if (group == nullptr) {
        return nullptr;
    }
    char name[16];
    bSPrintf(name, "Track %d", camTrack);
    ICETrackImage *track = GetTrack(group, name);
    sPlaybackTrack = track;
    if (track == nullptr) {
        return nullptr;
    }
    float t = TrackGetParameter(track);
    if (t < 0.0f || t > 1.0f) {
        return nullptr;
    }
    int k = TrackGetKeyNumber(track, t);
    if (k < 0 || k > track->NumKeys - 1) {
        return nullptr;
    }
    return track->GetKey(k);
}

namespace ICE {
// ICE::HideOverlay (0x46EAE0), for the NIS's release.
void HideOverlay() {
    ::HideOverlay();
}
} // namespace ICE

// ICEGetPlayerCarTransform (0x47EED0): the first player's car matrix in
// render space (identity without one).
static void ICEGetPlayerCarTransform(bMatrix4 *out) {
    bIdentity(out);
    IPlayer *player = IPlayer::First(PLAYER_ALL);
    ISimable *simable = player != nullptr ? player->GetSimable() : nullptr;
    IRigidBody *body = simable != nullptr ? simable->GetRigidBody() : nullptr;
    if (body == nullptr) {
        return;
    }
    UMath::Matrix4 m;
    body->GetMatrix4(m);
    bConvertFromBond(*out, *reinterpret_cast<bMatrix4 *>(&m));
    const UMath::Vector3 &p = body->GetPosition();
    out->v3.x = p.z;
    out->v3.y = -p.x;
    out->v3.z = p.y;
}

// 0x47EF70: of the scene's camera tracks, the one whose first key looks most
// along the player's car (the eye-to-look direction on the ground plane
// against the car's forward axis). Tracks are taken in chunk order; with
// fewer than two the first.
int ICEManager::ChooseGoodSceneCameraTrackIndex(uint32 scene_hash, bMatrix4 *scene_origin) {
    bMatrix4 car;
    ICEGetPlayerCarTransform(&car);
    ICENativeGroup *group = GetGroup(eDCE_NIS, scene_hash);
    if (group == nullptr || group->Tracks.size() < 2) {
        return 0;
    }
    int best = 0;
    float bestDot = -1.0f;
    for (int i = 0; i < static_cast<int>(group->Tracks.size()); i++) {
        ICETrackImage *track = group->Tracks[i];
        if (track->NumKeys <= 0) {
            continue;
        }
        const ICEData *first = track->GetKey(0);
        int k = first->bSmooth != 0 ? 1 : 0;
        const ICEData *key = first + k;
        bVector4 points[2];
        for (int j = 0; j < 2; j++) {
            const float *v = KeyPoint(key, j == 0 ? 0x1C : 0x34, k);
            bVector4 &p = points[j];
            p = bVector4(v[0], sMirror ? -v[1] : v[1], v[2], 0.0f);
            int space = j == 0 ? key->nSpaceEye : key->nSpaceLook;
            if (space == eDCE_SCENE_SPACE) {
                bVector3 sp(p.x, p.y, p.z);
                ScenePoint(&sp);
                p = bVector4(sp.x, sp.y, sp.z, 0.0f);
            }
            if (space == eDCE_CAR_SPACE) {
                eMulVector(&p, &car, &p);
            } else if (space == eDCE_HYBRID_SPACE) {
                p.x += car.v3.x;
                p.y += car.v3.y;
                p.z += car.v3.z;
            } else if (space == eDCE_SCENE_SPACE) {
                eMulVector(&p, scene_origin, &p);
            }
        }
        bVector3 dir(points[0].x - points[1].x, points[0].y - points[1].y, 0.0f);
        bNormalize(&dir, &dir);
        float dot = car.v0.z * dir.z + dir.x * car.v0.x + dir.y * car.v0.y;
        if (bestDot < dot) {
            best = i;
            bestDot = dot;
        }
    }
    return best;
}

int ICEManager::GetNumSceneCameraTrack(uint32 scene_hash) {
    ICENativeGroup *group = GetGroup(eDCE_NIS, scene_hash);
    return group != nullptr ? static_cast<int>(group->Tracks.size()) : 0;
}

float ICEManager::GetParameter() {
    ICEScene *scene = GetNISScene();
    if (scene != nullptr && fParameterLength > 0.0f) {
        return (scene->GetTimeElapsed() - fParameterStart) / fParameterLength;
    }
    return 0.0f;
}

int LoaderICECameras(bChunk *chunk) {
    unsigned int id = chunk->GetID();
    if (id == kChunkCameraShakes) {
        TheICEManager.LoadCameraShakes(chunk);
        return 1;
    }
    if (id >= kChunkNisCameras && id <= kChunkGenericCameras) {
        TheICEManager.LoadCameraSet(chunk);
        return 1;
    }
    return 0;
}

int UnloaderICECameras(bChunk *chunk) {
    unsigned int id = chunk->GetID();
    if (id == kChunkCameraShakes) {
        TheICEManager.UnloadCameraShakes(chunk);
        return 1;
    }
    if (id >= kChunkNisCameras && id <= kChunkGenericCameras) {
        TheICEManager.UnloadCameraSet(chunk);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// ICEAnchor (PC 0x90 bytes)
// ---------------------------------------------------------------------------

namespace {

struct ICEAnchor {
    ICEAnchor() // 0x46D500
        : mGeomPos(0.0f, 0.0f, 0.0f), mVelocity(0.0f, 0.0f, 0.0f), mAccel(0.0f, 0.0f, 0.0f), mVelMag(0.0f), mTopSpeed(0.0f), mRPM(0.0f),
          mNumWheels(false), mForwardSlip(0.0f), mSlipAngle(0.0f), mIsTouchingGround(true), mIsNosEngaged(false), mNosPercentageLeft(0.0f) {
        bIdentity(&mGeomRot);
    }

    // 0x46D3F0
    void Update(float dT, const bMatrix4 &matrix, const bVector3 &velocity) {
        bVector3 pos(matrix.v3.x, matrix.v3.y, matrix.v3.z);
        float moved = bDistBetween(&mGeomPos, &pos);
        mGeomRot = matrix;
        mGeomRot.v3.x = 0.0f;
        mGeomRot.v3.y = 0.0f;
        mGeomRot.v3.z = 0.0f;
        mGeomPos = pos;
        mVelocity = velocity;
        float old = mVelMag;
        mVelMag = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
        if (dT > 0.0f && moved * (1.0f / dT) < 300.0f) {
            bVector3 a((mVelMag - old) * (1.0f / dT), 0.0f, 0.0f);
            bMulMatrix(&mAccel, &mGeomRot, &a);
        } else {
            mAccel = bVector3(0.0f, 0.0f, 0.0f);
        }
    }

    bVector3 mGeomPos;        // 0x00
    bMatrix4 mGeomRot;        // 0x10
    bVector3 mVelocity;       // 0x50
    bVector3 mAccel;          // 0x60
    float mVelMag;            // 0x70
    float mTopSpeed;          // 0x74
    float mRPM;               // 0x78
    bool mNumWheels;          // 0x7C
    float mForwardSlip;       // 0x80
    float mSlipAngle;         // 0x84
    bool mIsTouchingGround;   // 0x88
    bool mIsNosEngaged;       // 0x89
    float mNosPercentageLeft; // 0x8C
};

// ---------------------------------------------------------------------------
// ICEMover (PC 0x130 bytes)
// ---------------------------------------------------------------------------

class ICEMover : public CameraMover {
  public:
    ICEMover(int view_id, ICEAnchor *anchor);
    ~ICEMover() override;
    void Update(float dT) override;

  private:
    void SetDesired(bool force);
    bool CubicEye() const {
        return pICEData != nullptr && pICEData->bCubicEye;
    }
    bool CubicLook() const {
        return pICEData != nullptr && pICEData->bCubicLook;
    }
    void GetEye(bVector3 *p, float t);
    void GetLook(bVector3 *p, float t);
    float GetDutch(float t);
    unsigned short GetFOV(float t);
    void EyeFromCamera(tCubic3D *eye, const bMatrix4 *world_to_space, const bVector3 *anchor_velocity);
    void LookFromCamera(tCubic3D *look, const bMatrix4 *world_to_space, const bVector3 *anchor_velocity);
    void FovFromCamera(tCubic1D *fov);

    ICEAnchor *pCar;            // 0x80
    tCubic3D *pEye;             // 0x84
    tCubic3D *pLook;            // 0x88
    tCubic1D *pDutch;           // 0x8C
    tCubic1D *pFov;             // 0x90
    tCubic1D *pNearClip;        // 0x94
    tCubic1D *pNoiseAmplitude;  // 0x98
    tCubic1D *pNoiseFrequency;  // 0x9C
    tCubic1D *pFocalDistance;   // 0xA0
    tCubic1D *pAperture;        // 0xA4
    tCubic1D *pLetterbox;       // 0xA8
    tCubic1D *pSimSpeed;        // 0xAC
    tCubic3D *pAccelOffset;     // 0xB0
    float fParameter0;          // 0xB4 key interval start
    float fParameter1;          // 0xB8 key interval end
    int nSpaceEye;              // 0xBC
    int nSpaceLook;             // 0xC0
    ICEData *pICEData;          // 0xC4 key playing
    bool bViolatesTopology;     // 0xC8
    bMatrix4 mHybridToWorld;    // 0xD0
    bVector3 vSmoothCarPos;     // 0x110
    bVector3 vSmoothCarFwd;     // 0x120
};

tCubic1D *NewCubic1D(short flags) {
    tCubic1D *c = new tCubic1D(0, 1.0f);
    InitCubic(*c, flags, 1.0f);
    return c;
}

tCubic3D *NewCubic3D(short flags) {
    tCubic3D *c = new tCubic3D(0, 1.0f);
    InitCubic(*c, flags, 1.0f);
    return c;
}

const bVector3 &Row(const bMatrix4 &m, int i) {
    return *reinterpret_cast<const bVector3 *>(&(&m.v0)[i]);
}

ICEMover::ICEMover(int view_id, ICEAnchor *anchor) : CameraMover(view_id, CM_ICE), pCar(anchor) {
    GetCamera()->ClearVelocity();
    pEye = NewCubic3D(0);
    pLook = NewCubic3D(0);
    pDutch = NewCubic1D(0);
    pFov = NewCubic1D(0);
    pNearClip = NewCubic1D(0);
    pNoiseAmplitude = NewCubic1D(0);
    pNoiseFrequency = NewCubic1D(0);
    pFocalDistance = NewCubic1D(0);
    pAperture = NewCubic1D(0);
    pLetterbox = NewCubic1D(0);
    pSimSpeed = NewCubic1D(0);
    pAccelOffset = NewCubic3D(1);
    fParameter0 = 0.0f;
    fParameter1 = 0.0f;
    nSpaceEye = 0;
    nSpaceLook = 0;
    pICEData = nullptr;
    bViolatesTopology = false;
    HideOverlay();
    bIdentity(&mHybridToWorld);
    vSmoothCarPos = pCar->mGeomPos;
    vSmoothCarFwd = Row(pCar->mGeomRot, 0);
    SetDesired(true);
}

ICEMover::~ICEMover() {
    HideOverlay();
    delete pEye;
    delete pLook;
    delete pDutch;
    delete pFov;
    delete pNearClip;
    delete pNoiseAmplitude;
    delete pNoiseFrequency;
    delete pFocalDistance;
    delete pAperture;
    delete pLetterbox;
    delete pSimSpeed;
    delete pAccelOffset;
    Camera *camera = GetCamera();
    camera->SetSimTimeMultiplier(1.0f);
    camera->SetLetterBox(0.0f);
    if (!Camera::StopUpdating) {
        camera->SetDepthOfField(0.0f);
    }
    if (!Camera::StopUpdating) {
        camera->SetFocalDistance(0.0f);
    }
    camera->ClearVelocity();
}

void ICEMover::GetEye(bVector3 *p, float t) {
    if (CubicEye()) {
        *p = bVector3(pEye->x.GetVal(t), pEye->y.GetVal(t), pEye->z.GetVal(t));
        return;
    }
    bVector3 desired(pEye->x.ValDesired, pEye->y.ValDesired, pEye->z.ValDesired);
    *p = bVector3((1.0f - t) * pEye->x.Val, pEye->y.Val * (1.0f - t), pEye->z.Val * (1.0f - t));
    bScaleAdd(p, p, &desired, t);
}

void ICEMover::GetLook(bVector3 *p, float t) {
    if (CubicLook()) {
        *p = bVector3(pLook->x.GetVal(t), pLook->y.GetVal(t), pLook->z.GetVal(t));
        return;
    }
    bVector3 desired(pLook->x.ValDesired, pLook->y.ValDesired, pLook->z.ValDesired);
    *p = bVector3((1.0f - t) * pLook->x.Val, pLook->y.Val * (1.0f - t), pLook->z.Val * (1.0f - t));
    bScaleAdd(p, p, &desired, t);
}

float ICEMover::GetDutch(float t) {
    if (CubicEye()) {
        return pDutch->GetVal(t);
    }
    return t * pDutch->ValDesired + (1.0f - t) * pDutch->Val;
}

unsigned short ICEMover::GetFOV(float t) {
    float f = CubicEye() ? pFov->GetVal(t) : (1.0f - t) * pFov->Val + t * pFov->ValDesired;
    return static_cast<unsigned short>(static_cast<int>(f));
}

// 0x46DA10: the eye spline's start from where the camera is going now.
void ICEMover::EyeFromCamera(tCubic3D *eye, const bMatrix4 *world_to_space, const bVector3 *anchor_velocity) {
    Camera *camera = GetCamera();
    bVector3 pos;
    bScaleAdd(&pos, camera->GetPosition(), camera->GetVelocityPosition(), 1.0f / 30.0f);
    if (world_to_space != nullptr) {
        bMulMatrix(&pos, world_to_space, &pos);
    }
    SetVal(eye, pos);
    bVector3 vel = *camera->GetVelocityPosition();
    if (anchor_velocity != nullptr) {
        vel = vel - *anchor_velocity;
    }
    float d = eye->x.duration;
    bVector4 v(vel.x * d, vel.y * d, vel.z * d, 0.0f);
    if (world_to_space != nullptr) {
        eMulVector(&v, world_to_space, &v);
    }
    SetdVal(eye, bVector3(v.x, v.y, v.z));
}

// 0x46DB30
void ICEMover::LookFromCamera(tCubic3D *look, const bMatrix4 *world_to_space, const bVector3 *anchor_velocity) {
    Camera *camera = GetCamera();
    bVector3 pos;
    bScaleAdd(&pos, camera->GetTarget(), camera->GetVelocityTarget(), 1.0f / 30.0f);
    if (world_to_space != nullptr) {
        bMulMatrix(&pos, world_to_space, &pos);
    }
    SetVal(look, pos);
    bVector3 vel = *camera->GetVelocityTarget();
    if (anchor_velocity != nullptr) {
        vel = vel - *anchor_velocity;
    }
    float d = look->x.duration;
    bVector4 v(vel.x * d, vel.y * d, vel.z * d, 0.0f);
    if (world_to_space != nullptr) {
        eMulVector(&v, world_to_space, &v);
    }
    SetdVal(look, bVector3(v.x, v.y, v.z));
}

// 0x46DCA0
void ICEMover::FovFromCamera(tCubic1D *fov) {
    Camera *camera = GetCamera();
    float vel = static_cast<float>(camera->GetVelocityFov());
    fov->SetVal(vel * 1092.2667f + static_cast<float>(camera->GetFov()));
    fov->SetdVal(vel * fov->duration);
}

// 0x482160: set the splines up for the key now playing.
void ICEMover::SetDesired(bool force) {
    ICEData *prev = pICEData;
    ICETrackImage *track = sPlaybackTrack;
    ICEData *data = track != nullptr ? TrackGetCameraData(track, &fParameter0, &fParameter1, nullptr) : nullptr;
    bool changed = prev != data;
    if (!force && !changed) {
        return;
    }
    bool cut = changed && data != nullptr && prev != nullptr && !IsContinuous(prev, 1, data, 0);
    bool enter = data != nullptr && prev == nullptr && (data->bSmooth & 1) == 0;
    bool leave = prev != nullptr && data == nullptr && (prev->bSmooth & 1) == 0;
    pICEData = data;
    if (cut || enter || leave) {
        FlushAccumulationBuffer();
        vSmoothCarPos = pCar->mGeomPos;
        vSmoothCarFwd = Row(pCar->mGeomRot, 0);
    }
    if (changed) {
        int key = track != nullptr ? TrackGetKeyNumber(track, (fParameter1 + fParameter0) * 0.5f) : 0;
        if (INIS *nis = INIS::Get()) {
            char tag[32];
            bSPrintf(tag, "ICECut%d", key);
            nis->FireEventTag(tag);
        }
        if (TraceICE()) {
            std::fprintf(stderr, "[ice] key %d (%.3f..%.3f)%s\n", data != nullptr ? key : -1, fParameter0, fParameter1, cut ? " cut" : "");
        }
    }
    if (data == nullptr) {
        return;
    }
    nSpaceEye = data->nSpaceEye;
    nSpaceLook = data->nSpaceLook;
    bVector3 eye0, eye1, look0, look1, eye_slope0, eye_slope1, look_slope0, look_slope1;
    float dutch_slope0, dutch_slope1, lens_slope0, lens_slope1;
    KeyGetEye(data, 0, &eye0);
    KeyGetEye(data, 1, &eye1);
    KeyGetLook(data, 0, &look0);
    KeyGetLook(data, 1, &look1);
    GetSlope(&eye_slope0, &look_slope0, &dutch_slope0, &lens_slope0, data, 0, track);
    GetSlope(&eye_slope1, &look_slope1, &dutch_slope1, &lens_slope1, data, 1, track);
    SetVal(pEye, eye0);
    SetdVal(pEye, eye_slope0);
    SetValDesired(pEye, eye1);
    SetdValDesired(pEye, eye_slope1);
    SetVal(pLook, look0);
    SetdVal(pLook, look_slope0);
    SetValDesired(pLook, look1);
    SetdValDesired(pLook, look_slope1);
    pDutch->SetVal(data->fDutch[0]);
    pDutch->SetdVal(dutch_slope0);
    pDutch->SetValDesired(data->fDutch[1]);
    pDutch->dValDesired = dutch_slope1;
    pFov->SetVal(LensToFov(data->fLens[0]));
    pFov->SetdVal(LensSlopeToFov(data->fLens[0], lens_slope0));
    pFov->SetValDesired(LensToFov(data->fLens[1]));
    pFov->dValDesired = LensSlopeToFov(data->fLens[1], lens_slope1);
    if (cut || enter || leave) {
        bIdentity(&mHybridToWorld);
        bCopy(&mHybridToWorld, &pCar->mGeomRot, &pCar->mGeomPos);
    }
    if (changed) {
        TheICEManager.SetSmoothExit(((data->bSmooth >> 1) & 1) != 0);
        if ((data->bSmooth & 1) != 0) {
            // Smooth entry: start (or end) the splines where the camera is.
            bMatrix4 car_to_world, world_to_car, world_to_hybrid, world_to_scene;
            bCopy(&car_to_world, &pCar->mGeomRot, &pCar->mGeomPos);
            eInvertTransformationMatrix(&world_to_car, &car_to_world);
            eInvertTransformationMatrix(&world_to_hybrid, &mHybridToWorld);
            tCubic3D eye(0, 1.0f), look(0, 1.0f);
            tCubic1D dutch(0, 1.0f), fov(0, 1.0f);
            InitCubic(eye, 1, 1.0f);
            InitCubic(look, 1, 1.0f);
            InitCubic(dutch, 1, 1.0f);
            InitCubic(fov, 1, 1.0f);
            const bMatrix4 *scene_m = nullptr;
            if (nSpaceEye == eDCE_SCENE_SPACE || nSpaceLook == eDCE_SCENE_SPACE) {
                if (ICEScene *scene = GetNISScene()) {
                    eInvertTransformationMatrix(&world_to_scene, &scene->GetSceneTransformMatrix());
                    scene_m = &world_to_scene;
                }
            }
            auto space_matrix = [&](int space) -> const bMatrix4 * {
                switch (space) {
                    case eDCE_CAR_SPACE:
                        return &world_to_car;
                    case eDCE_HYBRID_SPACE:
                        return &world_to_hybrid;
                    case eDCE_SCENE_SPACE:
                        return scene_m;
                    default:
                        return nullptr;
                }
            };
            EyeFromCamera(&eye, space_matrix(nSpaceEye), nSpaceEye == eDCE_CAR_SPACE ? &pCar->mVelocity : nullptr);
            LookFromCamera(&look, space_matrix(nSpaceLook), nSpaceLook == eDCE_CAR_SPACE ? &pCar->mVelocity : nullptr);
            dutch.SetVal(0.0f); // 0x46DC50
            dutch.SetdVal(0.0f);
            FovFromCamera(&fov);
            float t = TheICEManager.GetParameter();
            if (std::fabs(t - fParameter0) >= std::fabs(t - fParameter1)) {
                SetValDesired(pEye, bVector3(eye.x.Val, eye.y.Val, eye.z.Val));
                SetdValDesired(pEye, bVector3(eye.x.dVal, eye.y.dVal, eye.z.dVal));
                SetValDesired(pLook, bVector3(look.x.Val, look.y.Val, look.z.Val));
                SetdValDesired(pLook, bVector3(look.x.dVal, look.y.dVal, look.z.dVal));
                pDutch->SetValDesired(dutch.Val);
                pDutch->dValDesired = dutch.dVal;
                pFov->SetValDesired(fov.Val);
                pFov->dValDesired = fov.dVal;
            } else {
                SetVal(pEye, bVector3(eye.x.Val, eye.y.Val, eye.z.Val));
                SetdVal(pEye, bVector3(eye.x.dVal, eye.y.dVal, eye.z.dVal));
                SetVal(pLook, bVector3(look.x.Val, look.y.Val, look.z.Val));
                SetdVal(pLook, bVector3(look.x.dVal, look.y.dVal, look.z.dVal));
                pDutch->SetVal(dutch.Val);
                pDutch->SetdVal(dutch.dVal);
                pFov->SetVal(fov.Val);
                pFov->SetdVal(fov.dVal);
            }
        }
    }
    MakeCoeffs(pEye);
    MakeCoeffs(pLook);
    pDutch->MakeCoeffs();
    pFov->MakeCoeffs();
    SetKeyCubic(pNearClip, data->fNearClip[0], data->fNearClip[1]);
    SetKeyCubic(pNoiseAmplitude, data->fNoiseAmplitude[0], data->fNoiseAmplitude[1]);
    SetKeyCubic(pNoiseFrequency, data->fNoiseFrequency[0], data->fNoiseFrequency[1]);
    SetKeyCubic(pFocalDistance, data->fFocalDistance[0], data->fFocalDistance[1]);
    SetKeyCubic(pAperture, data->fAperture[0], data->fAperture[1]);
    SetKeyCubic(pLetterbox, data->fLetterbox[0], data->fLetterbox[1]);
    SetKeyCubic(pSimSpeed, data->fSimSpeed[0], data->fSimSpeed[1]);
    int state = TheICEManager.GetState();
    if ((state == eDCE_OFF || state == eDCE_PLAYING_ANIMATION || state == eDCE_DISTANCE) && (prev == nullptr || prev->nOverlay != data->nOverlay)) {
        HideOverlay();
        ShowOverlay(data->nOverlay);
    }
}

// 0x46D800: world-to-camera from eye, look and a dutch roll.
void BuildViewMatrix(bMatrix4 *m, const bVector3 &eye, const bVector3 &look, bAngle dutch) {
    bVector3 fwd = look - eye;
    bNormalize(&fwd, &fwd);
    bVector3 down(0.0f, 0.0f, -1.0f);
    bVector3 right;
    bCross(&right, &down, &fwd);
    bCross(&down, &fwd, &right);
    bNormalize(&right, &right);
    bNormalize(&down, &down);
    m->v0 = bVector4(right.x, down.x, fwd.x, 0.0f);
    m->v1 = bVector4(right.y, down.y, fwd.y, 0.0f);
    m->v2 = bVector4(right.z, down.z, fwd.z, 0.0f);
    m->v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
    eRotateZ(m, m, dutch);
    bMatrix4 t;
    bIdentity(&t);
    t.v3 = bVector4(-eye.x, -eye.y, -eye.z, 1.0f);
    bMulMatrix(m, m, &t);
}

// 0x482EC0
void ICEMover::Update(float dT) {
    ICETrackImage *track = sPlaybackTrack;
    if (!TheICEManager.IsUsingRealTime() && TheGameFlowManager.IsPaused()) {
        return;
    }
    bool generic = false;
    if (track != nullptr) {
        generic = track->GetContext() == eDCE_GENERIC;
        if (track->GetContext() != eDCE_REPLAY) {
            sMirror = false;
        }
    }
    bViolatesTopology = false;
    SetDesired(false);

    bMatrix4 scene_m;
    bIdentity(&scene_m);
    if (nSpaceEye == eDCE_SCENE_SPACE || nSpaceLook == eDCE_SCENE_SPACE) {
        ICEScene *scene = GetNISScene();
        if (scene == nullptr) {
            return;
        }
        scene_m = scene->GetSceneTransformMatrix();
    }
    float t = track != nullptr ? TrackGetParameter(track) : TheICEManager.GetParameter();
    float s = 0.0f;
    float span = fParameter1 - fParameter0;
    if (std::fabs(span) > 0.0001f) {
        s = (t - fParameter0) / span;
    }
    if (t >= 1.0f) {
        if (TraceICE() && TheICEManager.IsGenericCameraPlaying()) {
            std::fprintf(stderr, "[ice] track finished\n");
        }
        TheICEManager.ClearGenericCamera();
    }

    Camera *camera = GetCamera();
    float sim_speed = Eval(pSimSpeed, s);
    if (sim_speed < 0.0f) {
        sim_speed = 0.01f;
    } else {
        if (sim_speed >= 100.0f) {
            sim_speed = 100.0f;
        }
        sim_speed *= 0.01f;
        camera->SetSimTimeMultiplier(sim_speed);
        if (sim_speed < 0.01f) {
            sim_speed = 0.01f;
        }
    }
    float near_clip = Eval(pNearClip, s);
    if (near_clip >= 0.0f) {
        camera->SetNearZ(near_clip);
    }
    unsigned short fov = GetFOV(s);
    if (fov != 0 && !Camera::StopUpdating) {
        camera->SetFieldOfView(fov);
    }
    bVector3 eye, look;
    GetEye(&eye, s);
    GetLook(&look, s);

    // The car's frame: as is, with the orientation the key started with, or
    // lagged behind (generic cameras only).
    bMatrix4 car;
    if (pICEData != nullptr && pICEData->bIgnoreOrientation) {
        bCopy(&car, &mHybridToWorld, &pCar->mGeomPos);
    } else if (pICEData != nullptr && pICEData->bCarSpaceLag && generic) {
        float f = t * 0.9f + 0.1f;
        bVector3 fwd = Row(pCar->mGeomRot, 0);
        vSmoothCarPos = vSmoothCarPos + (pCar->mGeomPos - vSmoothCarPos) * f;
        vSmoothCarFwd = vSmoothCarFwd + (fwd - vSmoothCarFwd) * f;
        bIdentity(&car);
        bNormalize(&vSmoothCarFwd, &vSmoothCarFwd);
        car.v0 = bVector4(vSmoothCarFwd.x, vSmoothCarFwd.y, vSmoothCarFwd.z, 0.0f);
        bVector3 v0 = Row(car, 0), v1, v2 = Row(car, 2);
        bCross(&v1, &v2, &v0);
        bCross(&v2, &v0, &v1);
        car.v1 = bVector4(v1.x, v1.y, v1.z, 0.0f);
        car.v2 = bVector4(v2.x, v2.y, v2.z, 0.0f);
        car.v3 = bVector4(vSmoothCarPos.x, vSmoothCarPos.y, vSmoothCarPos.z, 1.0f);
    } else {
        bCopy(&car, &pCar->mGeomRot, &pCar->mGeomPos);
    }
    switch (nSpaceEye) {
        case eDCE_CAR_SPACE:
            bMulMatrix(&eye, &car, &eye);
            break;
        case eDCE_HYBRID_SPACE:
            eye = eye + pCar->mGeomPos;
            break;
        case eDCE_SCENE_SPACE:
            bMulMatrix(&eye, &scene_m, &eye);
            break;
        default:
            break;
    }
    switch (nSpaceLook) {
        case eDCE_CAR_SPACE:
            bMulMatrix(&look, &car, &look);
            break;
        case eDCE_HYBRID_SPACE:
            look = look + pCar->mGeomPos;
            break;
        case eDCE_SCENE_SPACE:
            bMulMatrix(&look, &scene_m, &look);
            break;
        default:
            break;
    }
    if (pICEData != nullptr && pICEData->bCarSpaceLag) {
        // The camera lags the car's acceleration a little.
        SetValDesired(pAccelOffset, pCar->mAccel);
        pAccelOffset->x.Update(sim_speed * dT, 0.0f, 0.0f);
        pAccelOffset->y.Update(sim_speed * dT, 0.0f, 0.0f);
        pAccelOffset->z.Update(sim_speed * dT, 0.0f, 0.0f);
        bVector3 off(pAccelOffset->x.Val * 0.05f, pAccelOffset->y.Val * 0.05f, pAccelOffset->z.Val * 0.02f); // 0x8EE310
        off.x = std::min(std::max(off.x, -1.0f), 1.0f);                                                      // 0x8EE2F0 / 0x8EE300
        off.y = std::min(std::max(off.y, -1.0f), 1.0f);
        off.z = std::min(std::max(off.z, -0.2f), 1.0f);
        eye = eye - off;
        look = look - off;
    }
    int editor_state = TheICEManager.GetState();
    bMatrix4 world_to_camera;
    BuildViewMatrix(&world_to_camera, eye, look, static_cast<bAngle>(static_cast<int>(GetDutch(s) * 65536.0f)));
    float amplitude = std::max(Eval(pNoiseAmplitude, s), 0.0f);
    float frequency = std::max(Eval(pNoiseFrequency, s), 0.0f);
    float length = track != nullptr ? track->Length : 0.0f;
    if (pICEData == nullptr || pICEData->nShakeType == 0) {
        camera->SetNoiseAmplitude1(0.0f, 0.0f, amplitude, amplitude);
        camera->SetNoiseFrequency1(0.0f, 0.0f, frequency * 1.618f, frequency * 1.382f);
        camera->SetNoiseAmplitude2(0.0f, 0.0f, 0.0f, 0.0f);
        camera->SetNoiseFrequency2(0.0f, 0.0f, 0.0f, 0.0f);
        camera->ApplyNoise(&world_to_camera, length * t, 1.0f);
    } else if (const ICEShakeImage *shake = GetShakeTrack(pICEData->nShakeType)) {
        int n = shake->NumKeys;
        int frame = static_cast<int>(length * t * frequency * 30.0f);
        int i = 0;
        if (n > 0) {
            while (frame < 0) {
                frame += n;
            }
            i = frame % n;
        }
        const ICEShakeData *k = shake->GetKey(i);
        float qx = k->q[0] * amplitude, qy = k->q[1] * amplitude, qz = k->q[2] * amplitude;
        float w = std::sqrt(1.0f - std::sqrt(qx * qx + qy * qy + qz * qz));
        bMatrix4 m;
        QuatToMatrix(&m, qx, qy, qz, w);
        bVector3 p(k->p[0] * amplitude, k->p[1] * amplitude, k->p[2] * amplitude);
        bCopy(&m, &m, &p);
        bMulMatrix(&world_to_camera, &m, &world_to_camera);
    }
    if (track == nullptr || track->GetContext() != eDCE_REPLAY) {
        if (pICEData != nullptr && pICEData->bConstrainToWorld && editor_state < eDCE_CAMERA_SETTINGS + 1) {
            MakeSpace(&world_to_camera, &pCar->mGeomPos);
        }
        if (pICEData != nullptr && pICEData->bConstrainToCars && editor_state < eDCE_CAMERA_SETTINGS + 1) {
            // TODO(port): 0x47C6F0 pushes the eye clear of nearby cars (also
            // unported for the drive camera).
        }
    } else {
        bViolatesTopology = MakeSpace(&world_to_camera, &pCar->mGeomPos);
        // TODO(port): 0x47C6F0 (see above) also feeds bViolatesTopology.
    }
    float letterbox = Eval(pLetterbox, s);
    if (letterbox >= 0.0f) {
        camera->SetLetterBox(letterbox * 0.00125f);
    }
    float aperture = Eval(pAperture, s);
    if (aperture < 0.0f || aperture >= 37.0f) {
        if (!Camera::StopUpdating) {
            camera->SetFocalDistance(0.0f);
        }
        if (!Camera::StopUpdating) {
            camera->SetDepthOfField(0.0f);
        }
    } else {
        float focus = std::max(Eval(pFocalDistance, s), 0.01f) * 1000.0f; // mm
        float f = FovToFocalLength(GetFOV(s));
        float hyperfocal = (f * f) / (ApertureToFStop(aperture) * 0.03f) + f;
        float num = (hyperfocal - f) * focus;
        float near_focus = (num / ((hyperfocal + focus) - (f + f))) * 0.001f;
        float far_side = hyperfocal - focus;
        float far_focus = 100000.0f;
        if (far_side > 1e-05f) {
            far_focus = (num / far_side) * 0.001f;
        }
        if (far_focus < near_focus) {
            far_focus = near_focus + 0.01f;
        }
        if (!Camera::StopUpdating) {
            camera->SetFocalDistance((near_focus + far_focus) * 0.5f);
        }
        if (!Camera::StopUpdating) {
            camera->SetDepthOfField(far_focus - near_focus);
        }
    }
    float target_distance = bDistBetween(&eye, &look);
    if (!Camera::StopUpdating) {
        camera->SetTargetDistance(target_distance);
    }
    camera->SetCameraMatrix(world_to_camera, sim_speed * dT);
}

// ---------------------------------------------------------------------------
// CDActionIce (PC 0x2F0 bytes)
// ---------------------------------------------------------------------------

class CDActionIce : public CameraAI::Action {
  public:
    static CameraAI::Action *Construct(CameraAI::Director *director);
    CDActionIce(CameraAI::Director *director, IPlayer *player);
    ~CDActionIce() override;

    void Update(float dT) override;
    void Reset() override {} // 0x46AA40
    const Attrib::StringKey &GetName() const override {
        static const Attrib::StringKey name("CDActionIce");
        return name;
    }
    Attrib::StringKey GetNext() const override {
        return mDone ? mPrev : Attrib::StringKey("");
    }
    CameraMover *GetMover() override {
        return mMover;
    }

  private:
    void AquireCar();
    void ReleaseCar();
    void FeedAnchor(float dT);

    // TODO(port): the ActionQueue ("CDActionIce", 0x98C7A2F5) that ends the
    // camera on a debug input (dword_911039, never set) and the
    // Sim::Attachments vehicle hook are not ported.
    Attrib::StringKey mPrev;    // 0x2B0
    ICEAnchor *mAnchor;         // 0x2C0
    ICEMover *mMover;           // 0x2C4
    WorldConn::Reference mTarget; // 0x2C8
    IPlayer *mPlayer;           // 0x2D8
    IVehicle *mVehicle;         // 0x2E0
    bool mDone;                 // 0x2E8
};

CameraAI::Action *CDActionIce::Construct(CameraAI::Director *director) {
    IPlayer *player = CameraAI::FindPlayerForView(director->GetViewID());
    if (player == nullptr || player->GetSettings() == nullptr) {
        return nullptr;
    }
    ISimable *simable = player->GetSimable();
    if (simable == nullptr || simable->GetWorldID() == 0) {
        return nullptr;
    }
    if (!TheICEManager.ChooseCameraPlaybackTrack() && !sReplayCameras) {
        return nullptr;
    }
    return new CDActionIce(director, player);
}

CDActionIce::CDActionIce(CameraAI::Director *director, IPlayer *player)
    : mPrev(""), mAnchor(nullptr), mMover(nullptr), mTarget(0), mPlayer(player), mVehicle(nullptr), mDone(false) {
    if (director->GetAction() != nullptr) {
        mPrev = director->GetAction()->GetName();
    }
    mAnchor = new ICEAnchor();
    AquireCar();
    if (mTarget.GetMatrix() != nullptr) {
        FeedAnchor(0.0f);
    }
    mMover = new ICEMover(director->GetViewID(), mAnchor);
}

CDActionIce::~CDActionIce() {
    TheICEManager.ClearGenericCamera();
    ReleaseCar();
    delete mMover;
    delete mAnchor;
}

void CDActionIce::ReleaseCar() {
    mVehicle = nullptr;
    mTarget.Set(0);
}

void CDActionIce::AquireCar() {
    if (mPlayer == nullptr) {
        return;
    }
    ISimable *simable = mPlayer->GetSimable();
    if (simable == nullptr ? mVehicle != nullptr : (mVehicle == nullptr || mVehicle->GetSimable() != simable)) {
        ReleaseCar();
    }
    if (mVehicle != nullptr || simable == nullptr) {
        return;
    }
    mTarget.Set(simable->GetWorldID());
    if (mTarget.GetMatrix() == nullptr) {
        return;
    }
    IVehicle *vehicle = nullptr;
    if (!simable->QueryInterface(&vehicle) || vehicle == nullptr) {
        return;
    }
    mVehicle = vehicle;
    ITransmission *transmission = nullptr;
    if (mVehicle->QueryInterface(&transmission) && transmission != nullptr) {
        mAnchor->mTopSpeed = transmission->GetMaxSpeedometer();
    }
    ISuspension *suspension = nullptr;
    if (mVehicle->QueryInterface(&suspension) && suspension != nullptr) {
        mAnchor->mNumWheels = suspension->GetNumWheels() != 0;
    }
}

// The followed body, raised to its centre of gravity (0x485833 / 0x4842F0).
void CDActionIce::FeedAnchor(float dT) {
    bMatrix4 m = *mTarget.GetMatrix();
    ICollisionBody *cbody = nullptr;
    if (mVehicle != nullptr && mVehicle->QueryInterface(&cbody) && cbody != nullptr) {
        IRigidBody *body = mVehicle->GetSimable()->GetRigidBody();
        if (body != nullptr) {
            UMath::Vector3 p = cbody->GetCenterOfGravity();
            body->ConvertLocalToWorld(p, false);
            const UMath::Vector3 &pos = body->GetPosition();
            m.v3.x = p.z + pos.z;
            m.v3.y = -(p.x + pos.x);
            m.v3.z = p.y + pos.y;
        }
    }
    static const bVector3 kZero(0.0f, 0.0f, 0.0f);
    const bVector3 *vel = mTarget.GetVelocity() != nullptr ? mTarget.GetVelocity() : &kZero;
    mAnchor->Update(dT, m, *vel);
}

void CDActionIce::Update(float dT) {
    if (mPlayer == nullptr) {
        ReleaseCar();
        return;
    }
    AquireCar();
    if (mTarget.GetMatrix() == nullptr) {
        return;
    }
    mAnchor->mSlipAngle = mVehicle != nullptr ? mVehicle->GetSlipAngle() : 0.0f;
    mAnchor->mRPM = 0.0f;
    mAnchor->mIsNosEngaged = false;
    mAnchor->mNosPercentageLeft = 0.0f;
    IEngine *engine = nullptr;
    if (mVehicle != nullptr && mVehicle->QueryInterface(&engine) && engine != nullptr) {
        mAnchor->mRPM = engine->GetRPM();
        mAnchor->mIsNosEngaged = engine->IsNOSEngaged();
        mAnchor->mNosPercentageLeft = engine->GetNOSCapacity();
    }
    mAnchor->mForwardSlip = 0.0f;
    FeedAnchor(dT);
    // TODO(port): replay cameras advance here (0x483C90) when dword_91103A is set.
}

} // namespace

CameraAI::Action *ConstructCDActionIce(CameraAI::Director *director) {
    return CDActionIce::Construct(director);
}
