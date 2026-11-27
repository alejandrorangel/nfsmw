#include "eLight.hpp"
#include "Ecstasy.hpp"
#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#include "Speed/Indep/Src/Misc/SpeedChunks.hpp"
#include "Speed/Indep/Src/Misc/Timer.hpp"
#include "Speed/Indep/Src/World/VisibleSection.hpp"
#include "Speed/Indep/bWare/Inc/bChunk.hpp"
#include "Speed/Indep/bWare/Inc/bMath.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"
#include "eMath.hpp"

bTList<eLightMaterial> LightMaterialList;    // size: 0x8, address: 0x80460DDC
eLightMaterial DefaultLightMaterialData;     // size: 0xA8, address: 0x80460DE4
#if defined(EA_PORT_NATIVE) // in .data originally, so it had an initializer
static eLightMaterial *DefaultLightMaterial = &DefaultLightMaterialData;
#else
static eLightMaterial *DefaultLightMaterial; // size: 0x4, address: 0x8041A63C
#endif
#if defined(EA_PORT_NATIVE)
int32 DrawLightFlares = 1; // speed.exe 1.3 0x8F2918
#else
int32 DrawLightFlares;
#endif

#if defined(EA_PORT_NATIVE)
// PC (speed.exe 1.3 @ 0x501A10). A light material chunk is one 32-bit
// eLightMaterial record (bTNode + NameHash at 0xC, Version 0x10, Name 0x14,
// data 0x30); natively it gets a native copy, remembered in the record's link
// slots for the unloader. Light flare packs (0x80135100) come later.
int LoaderLights(bChunk *bchunk) {
    if (bchunk->GetID() != BCHUNK_LIGHT_MATERIALS) {
        return 1;
    }
    char *disk = bchunk->GetData();
    uint32 version;
    bMemCpy(&version, disk + 0x10, 4);
    if (version == 3) {
        eLightMaterial *light_material = static_cast<eLightMaterial *>(bMalloc(sizeof(eLightMaterial), "eLightMaterial (native)", 0, 0));
        bMemSet(light_material, 0, sizeof(*light_material));
        bMemCpy(&light_material->NameHash, disk + 0x0C, 4);
        light_material->Version = version;
        bMemCpy(light_material->Name, disk + 0x14, sizeof(light_material->Name));
        bMemCpy(&light_material->feData, disk + 0x30, sizeof(light_material->feData));
        bMemCpy(disk, &light_material, sizeof(light_material));
        LightMaterialList.AddTail(light_material);
        if (light_material->NameHash == STRINGHASH_DEFAULT) {
            DefaultLightMaterial = light_material;
        }
    }
    return 1;
}
#else
int LoaderLights(bChunk *bchunk /* r30 */) {}
#endif

// UNSOLVED but identical
int UnloaderLights(bChunk *bchunk) {
    if (bchunk->GetID() == BCHUNK_LIGHT_MATERIALS) {
#if defined(EA_PORT_NATIVE)
        uint32 version;
        bMemCpy(&version, bchunk->GetData() + 0x10, 4);
        if (version == 3) {
            eLightMaterial *light_material;
            bMemCpy(&light_material, bchunk->GetData(), sizeof(light_material));
            light_material->Remove();
            if (light_material->NameHash == STRINGHASH_DEFAULT) {
                DefaultLightMaterial = &DefaultLightMaterialData;
            }
            bFree(light_material);
        }
#else
        eLightMaterial *light_material = reinterpret_cast<eLightMaterial *>(bchunk->GetData());
        if (light_material->Version == 3) {
            light_material->Remove();
            if (light_material->NameHash == STRINGHASH_DEFAULT) {
                DefaultLightMaterial = &DefaultLightMaterialData;
            }
        }
#endif
    } else if (bchunk->GetID() == BCHUNK_LIGHT_FLARES_PACK) {
        bChunk *chunk = bchunk->GetFirstChunk();
        bChunk *last_chunk = bchunk->GetLastChunk();
        eLightFlarePackHeader *pack_header;
        while (chunk != last_chunk) {
            if (chunk->GetID() == BCHUNK_LIGHT_FLARE_PACK_HEADER) {
                pack_header = reinterpret_cast<eLightFlarePackHeader *>(chunk->GetAlignedData(16));
                if (pack_header->Version != 3) {
                    return 1;
                }
                pack_header->Remove();

                VisibleSectionUserInfo *user_info = TheVisibleSectionManager.GetUserInfo(pack_header->ScenerySectionNumber);
                user_info->pLightFlarePack = nullptr;
                TheVisibleSectionManager.UnallocateUserInfo(pack_header->ScenerySectionNumber);
                return 1;
            }
            chunk = chunk->GetNext();
        }
    } else if (bchunk->GetID() == BCHUNK_LIGHT_SOURCES_PACK) {
        bChunk *chunk = bchunk->GetFirstChunk();
        bChunk *last_chunk = bchunk->GetLastChunk();
        while (chunk != last_chunk) {
            if (chunk->GetID() == BCHUNK_LIGHT_SOURCE_PACK_HEADER) {
                eLightPack *light_pack = reinterpret_cast<eLightPack *>(chunk->GetAlignedData(16));
                if (light_pack->Version != 4) {
                    return 1;
                }
                light_pack->Remove();

                VisibleSectionUserInfo *user_info = TheVisibleSectionManager.GetUserInfo(light_pack->ScenerySectionNumber);
                user_info->pLightPack = nullptr;
                TheVisibleSectionManager.UnallocateUserInfo(light_pack->ScenerySectionNumber);
                return 1;
            }
        }
    } else {
        return 0;
    }
    return 1;
}

void SetSelectCarLighting(int, float, int) {}

void SphericalToCartesian(bVector3 *v, float theta, float phi, float radius) {
    float cos_theta;
    float sin_theta;
    float cos_phi;
    float sin_phi;

    bSinCos(&sin_theta, &cos_theta, bDegToAng(theta));
    bSinCos(&sin_phi, &cos_phi, bDegToAng(phi));
    v->x = radius * cos_theta * sin_phi;
    v->y = radius * sin_theta * sin_phi;
    v->z = radius * cos_phi;
}

void CartesianToSpherical(bVector3 *v, float x, float y, float z) {
    float r;
    float t;
    float p;

    r = bSqrt(x * x + y * y + z * z);
    t = bATan(x, y);
    p = bACos(z / r);
    v->x = t;
    v->y = p;
    v->z = r;
}

void elRotateLightContext(eDynamicLightContext *new_light_context, eDynamicLightContext *light_context, bMatrix4 *rotate_matrix) {
    bMatrix4 invrotate_matrix;
    bVector4 light_directions[3];

    *new_light_context = *light_context;

    eInvertRotationMatrix(&invrotate_matrix, rotate_matrix);
    light_directions[0].x = light_context->LocalDirectionMatrix.v0.x;
    light_directions[0].y = light_context->LocalDirectionMatrix.v1.x;
    light_directions[0].z = light_context->LocalDirectionMatrix.v2.x;

    light_directions[1].x = light_context->LocalDirectionMatrix.v0.y;
    light_directions[1].y = light_context->LocalDirectionMatrix.v1.y;
    light_directions[1].z = light_context->LocalDirectionMatrix.v2.y;

    light_directions[2].x = light_context->LocalDirectionMatrix.v0.z;
    light_directions[2].y = light_context->LocalDirectionMatrix.v1.z;
    light_directions[2].z = light_context->LocalDirectionMatrix.v2.z;

    light_directions[0].w = 0.0f;
    light_directions[1].w = 0.0f;
    light_directions[2].w = 0.0f;

    eMulVector(&light_directions[0], &invrotate_matrix, &light_directions[0]);
    eMulVector(&light_directions[1], &invrotate_matrix, &light_directions[1]);
    eMulVector(&light_directions[2], &invrotate_matrix, &light_directions[2]);

    new_light_context->LocalDirectionMatrix.v0.x = light_directions[0].x;
    new_light_context->LocalDirectionMatrix.v1.x = light_directions[0].y;
    new_light_context->LocalDirectionMatrix.v2.x = light_directions[0].z;

    new_light_context->LocalDirectionMatrix.v0.y = light_directions[1].x;
    new_light_context->LocalDirectionMatrix.v1.y = light_directions[1].y;
    new_light_context->LocalDirectionMatrix.v2.y = light_directions[1].z;

    new_light_context->LocalDirectionMatrix.v0.z = light_directions[2].x;
    new_light_context->LocalDirectionMatrix.v1.z = light_directions[2].y;
    new_light_context->LocalDirectionMatrix.v2.z = light_directions[2].z;
}

int elSetupEnvMap(eDynamicLightContext *light_context, bMatrix4 *local_world, bMatrix4 *world_view, bVector4 *camera_world_position) {
    bMatrix4 world_local;

    eInvertRotationMatrix(&world_local, local_world);
    if (camera_world_position) {
        bVector4 v;
        v.x = camera_world_position->x - local_world->v3.x;
        v.y = camera_world_position->y - local_world->v3.y;
        v.z = camera_world_position->z - local_world->v3.z;
        v.w = 1.0f;
        eMulVector(&v, &world_local, &v);

        bool in_front_end = IsGameFlowInFrontEnd();
        float distance_min = 2.78f;
        float distance_max = in_front_end ? 8.0f : 2.78f;
        float distance = bLength(&v);

        if (distance < distance_min) {
            distance = distance_min;
        } else if (distance > distance_max) {
            distance = distance_max;
        }

        v.w = 0.0f;
        bNormalize(&v, &v);
        bScale(&v, &v, distance);
        v.w = 1.0f;
        light_context->LocalEyePosition = v;
        light_context->LocalEyePosition.w = 1.0f;
    }
    if (world_view && camera_world_position) {
        bMatrix4 *envmap_matrix = &light_context->EnvMapMatrix;
        bVector4 *camera_local_space;
        eMulMatrix(envmap_matrix, local_world, world_view);

        envmap_matrix->v3.x = light_context->LocalEyePosition.x;
        envmap_matrix->v3.y = light_context->LocalEyePosition.y;
        envmap_matrix->v3.z = light_context->LocalEyePosition.z;

        envmap_matrix->v0.w = 0.0f;
        envmap_matrix->v1.w = 0.0f;
        envmap_matrix->v2.w = 0.0f;
        envmap_matrix->v3.w = 0.0f;
    }
    return 1;
}

void elResetLightContext(eDynamicLightContext *light_context) {
    bMemSet(light_context, 0, sizeof(*light_context));
}

#if defined(EA_PORT_NATIVE)
// PC (speed.exe 1.3 @ 0x502130): the rig's lights 1..3 become the context's
// three directional lights -- directions (unit, pointing at the light) in
// the columns of LocalDirectionMatrix, colours (rgb * scale) in the rows of
// LocalColourMatrix -- in the space of local_world when given, else world.
// Directions come from (theta + 180, 90 - phi) in degrees. Sun lights are
// offset by the time-of-day sun direction (TimeOfDayPointer +0x180, kept by
// SceneNative); camera-space lights are rotated out of the view.
bool PortGetSunDirection(float out[3]); // SceneNative.cpp

int elSetupLights(eDynamicLightContext *light_context, eShaperLightRig *shaper_lights, bVector3 *local_pos, bMatrix4 *local_world,
                  bMatrix4 *world_view, eView *view) {
    if (light_context == nullptr) {
        return -1;
    }
    if (shaper_lights == nullptr) {
        return 1;
    }
    float sun[3] = {0.3f, 0.35f, 0.88f};
    PortGetSunDirection(sun);
    bVector3 dirs[3];
    for (int i = 0; i < 3; i++) {
        eShaperLight &light = shaper_lights->Lights[i + 1];
        bVector3 &d = dirs[i];
        switch (light.CameraSpace) {
            case LIGHT_WORLD_POSITION:
                d = shaper_lights->position;
                if (local_pos) {
                    d -= *local_pos;
                }
                break;
            case LIGHT_SUN_DIRECTION:
            case LIGHT_OPPOSITE_SUN_DIRECTION: {
                bVector3 sun_angles;
                float sign = light.CameraSpace == LIGHT_SUN_DIRECTION ? 1.0f : -1.0f;
                CartesianToSpherical(&sun_angles, sign * sun[0], sign * sun[1], sign * sun[2]);
                // 0x5022F8: angles truncated to bAngle, in degrees.
                float theta = static_cast<float>(static_cast<uint16>(static_cast<int>(sun_angles.x))) * (360.0f / 65536.0f) + light.Theta;
                float phi = static_cast<float>(static_cast<uint16>(static_cast<int>(sun_angles.y))) * (360.0f / 65536.0f) + 90.0f + light.Phi;
                SphericalToCartesian(&d, theta + 180.0f, 90.0f - phi, 1.0f);
                break;
            }
            case LIGHT_CAMERA_SPACE: {
                bVector3 c;
                SphericalToCartesian(&c, light.Theta + 180.0f, 90.0f - light.Phi, 1.0f);
                // 0x502235: (x forward, y left, z up) -> camera (-y, -z, x),
                // then out of the view by the inverse world->view rotation
                // (v * transpose(R): dot with R's rows).
                bVector3 v(-c.y, -c.z, c.x);
                bMatrix4 *wc = (view && view->GetCamera()) ? view->GetCamera()->GetWorldToCameraMatrix() : world_view;
                if (wc) {
                    d.x = v.x * wc->v0.x + v.y * wc->v0.y + v.z * wc->v0.z;
                    d.y = v.x * wc->v1.x + v.y * wc->v1.y + v.z * wc->v1.z;
                    d.z = v.x * wc->v2.x + v.y * wc->v2.y + v.z * wc->v2.z;
                } else {
                    d = c;
                }
                break;
            }
            default:
                SphericalToCartesian(&d, light.Theta + 180.0f, 90.0f - light.Phi, 1.0f);
                break;
        }
    }
    bMatrix4 &dir_m = light_context->LocalDirectionMatrix;
    bMatrix4 &col_m = light_context->LocalColourMatrix;
    bMemSet(&dir_m, 0, sizeof(dir_m));
    bMemSet(&col_m, 0, sizeof(col_m));
    bVector4 *rows[3] = {&col_m.v0, &col_m.v1, &col_m.v2};
    for (int i = 0; i < 3; i++) {
        bVector3 d = dirs[i];
        if (local_world) {
            // world -> local for a rotation: dot with the matrix's rows.
            bVector3 w = d;
            d.x = w.x * local_world->v0.x + w.y * local_world->v0.y + w.z * local_world->v0.z;
            d.y = w.x * local_world->v1.x + w.y * local_world->v1.y + w.z * local_world->v1.z;
            d.z = w.x * local_world->v2.x + w.y * local_world->v2.y + w.z * local_world->v2.z;
        }
        float len = bSqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len > 0.0f) {
            d.x /= len, d.y /= len, d.z /= len;
        }
        float *m = reinterpret_cast<float *>(&dir_m);
        m[0 * 4 + i] = d.x; // column i
        m[1 * 4 + i] = d.y;
        m[2 * 4 + i] = d.z;
        eShaperLight &light = shaper_lights->Lights[i + 1];
        rows[i]->x = light.Red * light.Scale;
        rows[i]->y = light.Green * light.Scale;
        rows[i]->z = light.Blue * light.Scale;
        rows[i]->w = 0.0f;
    }
    light_context->NumLights = 3;
    return 1;
}
#else
int elSetupLights(eDynamicLightContext *light_context /* r27 */, eShaperLightRig *shaper_lights /* r26 */, bVector3 *local_pos /* r22 */,
                  bMatrix4 *local_world /* r1+0x4B8 */, bMatrix4 *world_view /* r4 */, eView *view /* r8 */) {}
#endif

int elSetupLightContext(eDynamicLightContext *light_context /* r31 */, eShaperLightRig *shaper_lights /* r28 */, bMatrix4 *local_world /* r30 */,
                        bMatrix4 *world_view /* r27 */, bVector4 *camera_world_position /* r29 */, eView *view /* r26 */) {
    // ProfileNode profile_node;

    if (!light_context || !local_world || !shaper_lights) {
        return 0;
    } else {
        elResetLightContext(light_context);
        elSetupEnvMap(light_context, local_world, world_view, camera_world_position);
        elSetupLights(light_context, shaper_lights, reinterpret_cast<bVector3 *>(&local_world->v3), local_world, world_view, view);
        return 1;
    }
}

int elCloneLightContext(eDynamicLightContext *light_context, bMatrix4 *local_world, bMatrix4 *world_view, bVector4 *camera_world_position,
                        eView *view, eDynamicLightContext *old_context) {
    elRotateLightContext(light_context, old_context, local_world);
    elSetupEnvMap(light_context, local_world, world_view, camera_world_position);
    return 1;
}

#if !defined(EA_PORT_NATIVE) // natively in port/src/Platform/LightFlaresNative.cpp (PC 0x502640)
void UpdateLightFlareParameters() {}
#endif

float indep_fpow(float n, float p) {
    return ePowf(n, p);
}

eLightFlare PoolLightFlareList[50]; // size: 0x960, address: 0x804615F4
uint32 PoolOfFlaresXcludeView[50];  // size: 0xC8, address: 0x8046B140
int ActivePoolIndex;                // size: 0x4, address: 0x8041AB30

eLightFlare *eGetNextLightFlareInPool(uint32 XcludeViewIDs /* r7 */) {
    if (ActivePoolIndex == 50) {
        return nullptr;
    }
    PoolOfFlaresXcludeView[ActivePoolIndex] = XcludeViewIDs;
    return &PoolLightFlareList[ActivePoolIndex++];
}

void eInitLightFlarePool() {
    static int done = false;
    int PoolIndex;

    if (done) {
        return;
    }
    done = true;
    PoolIndex = 0;
    for (int i = 0; i < 50; i++) {
        eLightFlare *light_flare = &PoolLightFlareList[PoolIndex];
        light_flare->Flags = 0;
        light_flare->DirectionX = 0.0f;
        light_flare->DirectionY = 0.0f;
        light_flare->DirectionZ = -1.0f;
        light_flare->ReflectPosZ = 999.0f;
        PoolIndex++;
    }
}

int blink[4]; // size: 0x10, address: 0x8041AB38

void eResestLightFlarePool() {
    static float time[3];

    time[0] += WorldTimeElapsed;
    time[1] += WorldTimeElapsed;
    time[2] += WorldTimeElapsed;
    if (time[0] > 0.5f) {
        time[0] = 0.0f;
        blink[0] = ~blink[0];
    }
    if (time[1] > 0.4f) {
        time[1] = 0.0f;
        blink[1] = ~blink[1];
    }
    if (time[2] > 0.45f) {
        time[2] = 0.0f;
        blink[2] = ~blink[2];
    }
    ActivePoolIndex = 0;
}

// UNSOLVED
void eRenderLightFlarePool(eView *view /* r30 */) {
    bMatrix4 *local_world = eGetIdentityMatrix();
    uint32 vid = 1 << view->GetID();
    for (int i = 0; i < ActivePoolIndex; i++) {
        float intensity_scale = 1.0f;
        eLightFlare *light_flare = &PoolLightFlareList[i];
        if ((light_flare->Type == 18) && (blink[0] == 0)) {
            continue;
        }
        int amBlink = bAbs((int)light_flare->PositionX % 3);

        if (((light_flare->Type != 17) || (blink[amBlink] != 0)) && (light_flare->Type != 19 || (blink[2] != 0))) {
            if (light_flare->Type == 21) {
                intensity_scale = GetSunIntensity(eGetView(1, false));
            }
            if ((vid & PoolOfFlaresXcludeView[i]) == 0) {
                if ((view->GetID() == EVIEW_FIRST_RVM) || (view->GetID() > EVIEW_SHADOWMATTE && (view->GetID() < NUM_EVIEWS))) {
                    eRenderLightFlare(view, light_flare, local_world, intensity_scale, REF_NONE, FLARE_ENV, 0.0f, 0, 1.0f);
                } else {
                    eRenderLightFlare(view, light_flare, local_world, 1.0f, REF_NONE,
                                      view->GetID() == EVIEW_FIRST_PLAYER ? FLARE_CAT_P1 : FLARE_CAT_P2, 0.0f, 0, 1.0f);
                }
            }
        }
    }
}

#if defined(EA_PORT_NATIVE)
uint32 eLightFlareTextureNameHashes[3] = {0x8868656f, 0x88d8e8a2, 0x3394fe62}; // speed.exe 1.3 0x8F2434
#else
uint32 eLightFlareTextureNameHashes[3];  // size: 0xC, address: 0x8041A69C
#endif
TextureInfo *eLightFlareTextureInfos[3]; // size: 0xC, address: 0x8046B134

void eLightUpdateTextures() {
    for (uint32 i = 0; i < 3; i++) {
        eLightFlareTextureInfos[i] = GetTextureInfo(eLightFlareTextureNameHashes[i], 0, 0);
    }
}

void eRenderWorldLightFlares(eView *view /* r26 */, flareType type /* r24 */) {
    bMatrix4 *local_world; // r23
    int num_visible;
    int num_scenery_culled;
    int total;
    DrivableScenerySection *scenery_section;
    bVector2 *camera_position;

    if (DrawLightFlares != 0 && IsGameFlowInGame()) {
        camera_position = reinterpret_cast<bVector2 *>(view->GetCamera()->GetPosition());
        local_world = eGetIdentityMatrix();
        scenery_section = TheVisibleSectionManager.FindDrivableSection(camera_position);
        if (!scenery_section) {
            return;
        }
        for (int i = 0; i < scenery_section->NumVisibleSections; i++) {
            VisibleSectionUserInfo *user_info = TheVisibleSectionManager.GetUserInfo(scenery_section->GetVisibleSection(i));
            if (user_info) {
                eLightFlarePackHeader *pack = user_info->pLightFlarePack;
                if (pack && view->GetVisibleState(&pack->BBoxMin, &pack->BBoxMax, nullptr) != EVISIBLESTATE_NOT) {
                    for (eLightFlare *light_flare = pack->LightFlareList.GetHead(); light_flare != pack->LightFlareList.EndOfList();
                         light_flare = light_flare->GetNext()) {
                        eRenderLightFlare(view, light_flare, local_world, 1.0f, REF_NONE, type, 0.0f, 0, 1.0f);
                    }
                }
            }
        }
    }
}

void RestoreShaperRig(eShaperLightRig *ShaperRigP, uint32 slot, eShaperLightRig *ShaperRigBP) {
    ShaperRigP->Lights[slot].CameraSpace = ShaperRigBP->Lights[slot].CameraSpace;
    ShaperRigP->Lights[slot].Red = ShaperRigBP->Lights[slot].Red;
    ShaperRigP->Lights[slot].Green = ShaperRigBP->Lights[slot].Green;
    ShaperRigP->Lights[slot].Blue = ShaperRigBP->Lights[slot].Blue;
    ShaperRigP->Lights[slot].Scale = ShaperRigBP->Lights[slot].Scale;
    ShaperRigP->Lights[slot].Theta = ShaperRigBP->Lights[slot].Theta;
    ShaperRigP->Lights[slot].Phi = ShaperRigBP->Lights[slot].Phi;
}

void AddQuickDynamicLight(eShaperLightRig *ShaperRigP, uint32 slot, float r, float g, float b, float intensity, bVector3 *position) {
    uint32 mask = 1 << (slot - 2);

    ShaperRigP->NumOverideSlots |= mask;
    ShaperRigP->position = *position;

    ShaperRigP->Lights[slot].CameraSpace = LIGHT_WORLD_POSITION;
    ShaperRigP->Lights[slot].Red = r;
    ShaperRigP->Lights[slot].Green = g;
    ShaperRigP->Lights[slot].Blue = b;
    ShaperRigP->Lights[slot].Scale = intensity;
}

void AdjustQuickDynamicLight(eShaperLightRig *ShaperRigP, bVector3 *MyPosition) {
    for (int i = 0; i < 0; i++) {
        uint32 slot;
        uint32 mask;
        if (0 == 0) {
            bVector3 direction = ShaperRigP->position - *MyPosition;
            bVector3 spherical_direction;
            bVector3 idk;
        }
    }
}

void eLightMaterial::BuildData() {
    this->UpdatePlatInfo();
}

eLightMaterial *elGetLightMaterial(uint32 name_hash /* r10 */) {
    for (eLightMaterial *elm = LightMaterialList.GetHead(); elm != LightMaterialList.EndOfList(); elm = elm->GetNext()) {
        if (elm->NameHash == name_hash) {
            return elm;
        }
    }
    return DefaultLightMaterial;
}

eLightMaterial *elGetDefaultLightMaterial() {
    return DefaultLightMaterial;
}

void elInit() {
    bMemSet(DefaultLightMaterial, 0, sizeof(*DefaultLightMaterial));
    elInitPlat();
    eInitLightFlarePool();
}

void elBeginFrame() {}
