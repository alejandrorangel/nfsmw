// The car environment map (IDI_CAR_FX EnvMapTex), as speed.exe 1.3 does it.
//
// Frontend: eInitFEEnvMapPlat (0x6BF690) streams six garage textures
// "<garage prefix>_<XPOS..ZNEG>" (names at 0x8F9314) and, once all are in,
// EnvMapTextureLoadedCallback (0x6BF5A0) copies them into a static cube
// (0x982A64). In game: eDisplayFrame (0x6E7220) calls
// UpdateEnvironmentMapCameras, which points six cameras (eEnvMap::
// UpdateCameras, 0x6DA350) along the axes of the player camera, from just
// above the player car; ___eDisplayFrame then draws the scenery into the
// faces of a render-target cube (0x982A30, see SceneNative). The car effect
// samples the cube with the reflection vector taken into the space of
// hack_man_matrix (a look-at from the camera to the car, also set by
// UpdateCameras) through eDynamicLightContext::EnvMapMatrix.
//
// The native views: the GameCube enum's env-map views 16..21 stand in for the
// PC's 18..23; Views[i] renders cube face i (PC eInitEnvMap 0x6CEFA0 gives
// Views[0..5] render targets 5..10, the faces +X, -X, +Y, -Y, +Z, -Z).

#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/EcstasyData.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Ecstasy/eEnvMap.hpp"
#include "Speed/Indep/Src/Ecstasy/eMath.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"
#include "Speed/PC/Src/PCGraphicsOptions.hpp"

#include "../Mac/MetalBridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

int EnableEnvMap = 1;       // 0x8F9334 (CarRender: env map on cars)
bMatrix4 hack_man_matrix;   // 0x987FC0

namespace {

eEnvMap *gEnvMap;

// EnableEnvMap__009017B8, the "Car Reflection Update Rate" option
// (PCGraphicsOptions::CarEnvironmentMapEnable): 0 off, 1..3 faces per frame by
// the table at 0x8F9028 (1: one face a frame; 2: +X and +Z every frame plus
// one of the others; 3: all six). NFSMW_ENVMAP=0..3 overrides it.
int EnvMapQuality() {
    static const int forced = [] {
        const char *e = std::getenv("NFSMW_ENVMAP");
        return e ? std::atoi(e) : -1;
    }();
    int q = forced >= 0 ? forced : gPCGraphicsOptions.CarEnvironmentMapEnable;
    return q < 0 ? 0 : q > 3 ? 3 : q;
}

// 0x8F9018: frames per cycle; 0x8F9028: faces drawn on each frame.
const unsigned kCycle[4] = {1, 6, 4, 1};
const unsigned char kFaceTable[13][6] = {
    {1, 0, 0, 0, 0, 0}, {0, 1, 0, 0, 0, 0}, {0, 0, 1, 0, 0, 0}, {0, 0, 0, 1, 0, 0}, {0, 0, 0, 0, 1, 0},
    {0, 0, 0, 0, 0, 1}, {1, 1, 0, 0, 1, 0}, {1, 0, 1, 0, 1, 0}, {1, 0, 0, 1, 1, 0}, {1, 0, 0, 0, 1, 1},
    {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1},
};

// Garage cube (0x982A64), its face names (0x8F9314) and loaded count (0x93E86C).
void *gFECube;
const char *const kFaceNames[6] = {"XPOS", "XNEG", "YPOS", "YNEG", "ZPOS", "ZNEG"};
int gFEFacesLoaded;
bool gEnvMapsAvailable = true; // byte_8F945B

unsigned FaceHash(eGarageType type, int face) {
    char name[64];
    std::snprintf(name, sizeof(name), "%s_%s", FEManager::Get()->GetGaragePrefixFromType(type), kFaceNames[face]);
    return bStringHash(name);
}

void EnvMapTextureLoadedCallback(void *) {
    if (++gFEFacesLoaded < 6) {
        return;
    }
    void *faces[6] = {};
    for (int i = 0; i < 6; i++) {
        TextureInfo *t = GetTextureInfo(FaceHash(FEManager::Get()->GetGarageType(), i), 1, 0);
        faces[i] = t && t->GetPlatInfo() ? t->GetPlatInfo()->MetalTexture : nullptr;
        if (faces[i] == nullptr) {
            std::fprintf(stderr, "[envmap] garage face %s missing\n", kFaceNames[i]);
            return;
        }
    }
    void *cube = PortMetalCreateCube(faces, "Garage env map");
    if (std::getenv("NFSMW_TRACE_ENVMAP")) {
        TextureInfo *t = GetTextureInfo(FaceHash(FEManager::Get()->GetGarageType(), 0), 1, 0);
        std::fprintf(stderr, "[envmap] garage cube %p from %s %dx%d\n", cube, t->DebugName, t->Width, t->Height);
    }
    if (cube) {
        if (gFECube) {
            PortMetalReleaseTexture(gFECube);
        }
        gFECube = cube;
    }
}

} // namespace

eEnvMap *eGetEnvMap() {
    return gEnvMap;
}

int eInitEnvMap() {
    // Raw storage, cameras constructed in place (Camera has no native
    // destructor for eEnvMap's implicit constructor to unwind with).
    gEnvMap = static_cast<eEnvMap *>(::operator new(sizeof(eEnvMap)));
    std::memset(static_cast<void *>(gEnvMap), 0, sizeof(eEnvMap));
    for (int i = 0; i < 6; i++) {
        new (&gEnvMap->Cameras[i]) Camera();
    }
    eView *player = eGetView(1, false);
    for (int i = 0; i < 6; i++) {
        eView *view = eGetView(EVIEW_FIRST_ENVMAP + i, false);
        gEnvMap->Views[i] = view;
        view->pCamera = &gEnvMap->Cameras[i];
        view->Active = 1;
        view->Precipitation = player ? player->Precipitation : nullptr;
    }
    return 1;
}

// 0x6DA350.
void eEnvMap::UpdateCameras(bVector3 *viewer_world_position, bVector3 *envmap_world_position) {
    static bVector3 up(0.0f, 0.0f, 1.0f); // 0x901760
    eCreateLookAtMatrix(&hack_man_matrix, *viewer_world_position, *envmap_world_position, up);
    hack_man_matrix.v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);

    int quality = EnvMapQuality();
    const unsigned char *active = kFaceTable[12];
    if (quality > 0) {
        active = kFaceTable[eFrameCounter % kCycle[quality] + (quality - 1) * 6];
    }
    Camera *player_camera = eGetView(1, false)->GetCamera();
    for (int i = 0; i < 6; i++) {
        eView *view = this->Views[i];
        if (view == nullptr) {
            continue;
        }
        view->Active = active[i] != 0;
        Camera *camera = view->GetCamera();
        if (camera == nullptr || player_camera == nullptr) {
            continue;
        }
        // The player camera's rotation, turned onto the face (D3D cube face
        // orientations, in camera space with y down: flip y between turns).
        bMatrix4 m = *player_camera->GetWorldToCameraMatrix();
        m.v3 = bVector4(0.0f, 0.0f, 0.0f, 1.0f);
        bMatrix4 flip;
        eIdentity(&flip);
        flip.v1.y = -1.0f;
        eRotateY(&m, &m, 0x8000);
        eMulMatrix(&m, &m, &flip);
        switch (i) {
            case 0: eRotateY(&m, &m, 0x4000); break;
            case 1: eRotateY(&m, &m, 0xC000); break;
            case 2: eRotateY(&m, &m, 0x8000); eRotateX(&m, &m, 0xC000); break;
            case 3: eRotateY(&m, &m, 0x8000); eRotateX(&m, &m, 0x4000); break;
            case 4: eRotateY(&m, &m, 0x8000); break;
            case 5: break;
        }
        // Translation: the env-map position into the face's camera space.
        const bVector3 &p = *envmap_world_position;
        m.v3.x = -(m.v0.x * p.x + m.v1.x * p.y + m.v2.x * p.z);
        m.v3.y = -(m.v0.y * p.x + m.v1.y * p.y + m.v2.y * p.z);
        m.v3.z = -(m.v0.z * p.x + m.v1.z * p.y + m.v2.z * p.z);
        camera->SetCameraMatrix(m, RealTimeElapsed);
        camera->SetRenderDash(0);
        if (!Camera::StopUpdating) {
            camera->SetFocalDistance(0.0f); // camera +0xB4 (guess: PC CameraParams layout)
        }
        camera->SetNearZ(0.5f);   // 0x8F9010
        camera->SetFarZ(100.0f);  // 0x8F9014
        if (!Camera::StopUpdating) {
            camera->SetFieldOfView(0x4000); // word_987DB8 (90 degrees)
        }
    }
}

// The face views drawn this frame (SceneNative).
int PortEnvMapQuality() {
    return gEnvMapsAvailable ? EnvMapQuality() : 0;
}

void *PortFEEnvMapCube() {
    return gFECube;
}

void eInitFEEnvMapPlat() {
    if (!gEnvMapsAvailable) {
        return;
    }
    gFEFacesLoaded = 0;
    for (int i = 0; i < 6; i++) {
        uint32 hash = FaceHash(FEManager::Get()->GetGarageType(), i);
        eLoadStreamingTexture(&hash, 1, EnvMapTextureLoadedCallback, nullptr, 0);
    }
}

// 0x6BF720: unloads the faces of the previous and of the current garage.
void eRemoveFEEnvMapPlat() {
    FEManager *fe = FEManager::Get();
    for (int i = 0; i < 6; i++) {
        eGarageType types[2] = {fe->GetPreviousGarageType(), fe->GetGarageType()};
        for (eGarageType type : types) {
            if (type == GARAGETYPE_NONE) {
                continue;
            }
            uint32 hash = FaceHash(type, i);
            if (GetTextureInfo(hash, 0, 0) != nullptr) {
                eUnloadStreamingTexture(&hash, 1);
            }
        }
    }
}
