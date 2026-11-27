// Particles and the other immediate-mode world quads on Metal: native
// counterparts of the PC platform's particle renderer (speed.exe 1.3
// PlatStartParticleRender 0x6CEEE0, PlatAddParticle 0x6C1160,
// PlatEndParticleRender 0x6DA2F0 / flush sub_6C1010, PlatGetViewVectors
// 0x6BCC00) and of eViewPlatInterface::Render(ePoly *) (0x6BE750 ->
// 0x6D1A40, the skid marks and light flares), drawn by MetalPrelit.mm.
//
// Also the emitter chunk loaders PC registers at 0x886340 / 0x886360
// (EmitterSystem::Loader 0x50CC60 / Unloader 0x5099C0 for chunks 0x3BB00 and
// 0x3BC00), with the world-effect libraries copied out of their 32-bit
// on-disk layout.

#include "Speed/Indep/Src/Camera/Camera.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/EcstasyData.hpp"
#include "Speed/Indep/Src/Ecstasy/EmitterSystem.h"
#include "Speed/Indep/Src/Ecstasy/eStrip.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Misc/SpeedChunks.hpp"
#include "Speed/Indep/bWare/Inc/bChunk.hpp"

#include "../Mac/MetalBridge.h"
#include "../Mac/MetalPrelitBridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

void *MetalTextureOf(TextureInfo *t) {
    if (t == nullptr || t->GetPlatInfo() == nullptr) {
        return nullptr;
    }
    return t->GetPlatInfo()->MetalTexture;
}

// sub_6C68B0: the texture's blend state. AlphaBlendType 0 draws unblended
// with depth writes; the others blend without them (ZWRITEENABLE 0 outside
// the shadow view); alpha test (ref 0x5F) with a punch-through alpha.
PortPrelitBlend BlendFor(TextureInfo *t, uint32_t *flags) {
    *flags = 0;
    if (t == nullptr) {
        return PORT_PRELIT_ALPHA;
    }
    if (t->AlphaUsageType == TEXUSAGE_PUNCHTHRU) {
        *flags |= PORT_PRELIT_ALPHA_TEST;
    }
    switch (t->AlphaBlendType) {
    case TEXBLEND_SRCCOPY:
        *flags |= PORT_PRELIT_DEPTH_WRITE;
        return PORT_PRELIT_OPAQUE;
    case TEXBLEND_ADDATIVE:
        return PORT_PRELIT_ADDITIVE;
    case TEXBLEND_SUBTRACTIVE:
        return PORT_PRELIT_SUBTRACTIVE;
    case TEXBLEND_DEST_BLEND:
        return PORT_PRELIT_DEST_ALPHA;
    case TEXBLEND_DEST_ADDATIVE:
        return PORT_PRELIT_DEST_ADDITIVE;
    default: // BLEND, and 4/7/8 take the switch's default (5, 6)
        return PORT_PRELIT_ALPHA;
    }
}

// The views whose prelit quads reach the frame natively: the player views.
bool DrawsPrelit(eView *view) {
    int id = view->GetID();
    return id == EVIEW_FLAILER || (id >= EVIEW_FIRST_PLAYER && id <= EVIEW_LAST_PLAYER);
}

// ePoly quads also go to the road reflection (view 4) and the env-map faces,
// where the PC draws their light flares (PortRenderViewFlares).
bool DrawsPolys(eView *view) {
    int id = view->GetID();
    return DrawsPrelit(view) || id == EVIEW_PLAYER1_GLOW || (id >= EVIEW_FIRST_ENVMAP && id <= EVIEW_LAST_ENVMAP);
}

// The batch between PlatStartParticleRender and PlatEndParticleRender (the
// PC's locked vertex buffer 0x93CAF4: 4 vertices of 0x18 bytes per particle).
struct ParticleBatch {
    bool active = false;
    void *texture = nullptr;
    PortPrelitBlend blend = PORT_PRELIT_ALPHA;
    uint32_t flags = 0;
    std::vector<PortPrelitVertex> vertices;
};
ParticleBatch gParticles;
int gPolysThisFrame; // NFSMW_TRACE_PARTICLES: ePoly quads drawn this frame

// The decomp's (GameCube) EmitterParticle colour: r << 24 | g << 16 | b << 8 | a
// (PC UpdateParticles 0x50917E packs D3DCOLOR a r g b).
uint32_t ParticleColour(uint32_t c) {
    return (c & 0xFFu) << 24 | (c >> 8);
}

void SetVertex(PortPrelitVertex &v, float x, float y, float z, uint32_t colour, float u, float tv) {
    v.x = x;
    v.y = y;
    v.z = z;
    v.colour = colour;
    v.u = u;
    v.v = tv;
}

} // namespace

// The view's world -> camera matrix columns: right, up, forward.
void PlatGetViewVectors(eView *view, UMath::Vector3 &rightVec, UMath::Vector3 &upVec, UMath::Vector3 &fwdVec) {
    const bMatrix4 *m = view->GetCamera()->GetWorldToCameraMatrix();
    rightVec.x = m->v0.x;
    rightVec.y = m->v1.x;
    rightVec.z = m->v2.x;
    upVec.x = m->v0.y;
    upVec.y = m->v1.y;
    upVec.z = m->v2.y;
    fwdVec.x = m->v0.z;
    fwdVec.y = m->v1.z;
    fwdVec.z = m->v2.z;
}

// ZWRITEENABLE 0, World identity, the texture's state (sub_6C6CC0, which
// turns depth writes back on for unblended textures), the vertex buffer
// locked for num_particles quads.
//
// The effect's technique is the one sub_6D7500 picks (the highest shadow
// level up to 0x982C3C that the effect has: #1, shadowLevel 1, on the NVIDIA
// path; the table built at 0x6D5EDE drops #2's level 4, the 0x982C08 == 2
// path's), and sub_6C8000 sets ShadowTransform = identity x the shadow
// texture matrix ([0x919C70] + 0x40) here, after the effect's Begin
// (sub_6D6AA0, ShadowMap and ShadowMapScale with Shadow Detail on in the
// player view). PORT_PRELIT_SHADOWED; MetalRenderer gives the player view's
// shadow map. Port deviation: with no shadow map drawn (Shadow Detail off)
// the batches draw with technique #0; PC keeps #1 with its ShadowMap unset.
bool PlatStartParticleRender(eView *view, TextureInfo *texture_info, uint32 num_particles) {
    gParticles.active = DrawsPrelit(view);
    gParticles.texture = MetalTextureOf(texture_info);
    gParticles.blend = BlendFor(texture_info, &gParticles.flags);
    gParticles.flags |= PORT_PRELIT_SHADOWED;
    gParticles.vertices.clear();
    return true;
}

// Four corners around the particle: p + a - b, p + a + b, p - a + b, p - a - b
// (a, b the rotated and scaled right / up vectors), or from the axis
// constraint's bases (sub_4FE220). The PC's UVs are 8.8 fixed point (times
// 1/256); the decomp's are 16-bit fractions (GetAnimatedUVs): u in the high
// half, v in the low.
void PlatAddParticle(const EmitterParticle &particle, const UMath::Vector3 &a, const UMath::Vector3 &b, unsigned int hack_flags,
                     bVector4 *x_basis, bVector4 *y_basis) {
    if (!gParticles.active) {
        return;
    }
    const float px = particle.mPosX, py = particle.mPosY, pz = particle.mPosZ;
    float corners[4][3];
    if (x_basis == nullptr || y_basis == nullptr) {
        const float s[4][2] = {{1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
        for (int i = 0; i < 4; i++) {
            corners[i][0] = px + s[i][0] * a.x + s[i][1] * b.x;
            corners[i][1] = py + s[i][0] * a.y + s[i][1] * b.y;
            corners[i][2] = pz + s[i][0] * a.z + s[i][1] * b.z;
        }
    } else {
        const bVector4 &x = *x_basis, &y = *y_basis;
        const float s[4][2] = {{-1, 1}, {-1, -1}, {1, -1}, {1, 1}};
        for (int i = 0; i < 4; i++) {
            corners[i][0] = px + s[i][0] * x.x + s[i][1] * y.x;
            corners[i][1] = py + s[i][0] * x.y + s[i][1] * y.y;
            corners[i][2] = pz + s[i][0] * x.z + s[i][1] * y.z;
        }
    }
    const float k = 1.0f / 65535.0f;
    const float u0 = (particle.mUVStart >> 16) * k, v0 = (particle.mUVStart & 0xFFFF) * k;
    const float u1 = (particle.mUVEnd >> 16) * k, v1 = (particle.mUVEnd & 0xFFFF) * k;
    const float uv[4][2] = {{u1, v0}, {u1, v1}, {u0, v1}, {u0, v0}};
    const uint32_t colour = ParticleColour(particle.mColour);
    size_t n = gParticles.vertices.size();
    gParticles.vertices.resize(n + 4);
    for (int i = 0; i < 4; i++) {
        SetVertex(gParticles.vertices[n + i], corners[i][0], corners[i][1], corners[i][2], colour, uv[i][0], uv[i][1]);
    }
    // TODO(port): hack flag 1 (byte 0x93CB10: SrcAlpha / One at the flush)
    // is never passed by EmitterSystem::Render.
}

void PlatEndParticleRender() {
    if (gParticles.active && !gParticles.vertices.empty()) {
        PortMetalPrelitAddQuads(gParticles.texture, gParticles.blend, gParticles.flags, gParticles.vertices.data(),
                                (uint32_t)(gParticles.vertices.size() / 4));
    }
    gParticles.active = false;
    gParticles.vertices.clear();
}

// Debug statistics only (PC 0x6C6770 stores them for an overlay).
void SetParticleSystemStats(int num_particles, int max_particles, int num_textures, int max_textures, int num_emitters, int max_emitters,
                            int num_groups, int max_groups) {
    static const bool trace = std::getenv("NFSMW_TRACE_PARTICLES") != nullptr;
    static int frame;
    if (trace && frame++ % 120 == 0) {
        std::fprintf(stderr, "[particles] %d/%d particles, %d textures, %d/%d emitters, %d/%d groups\n", num_particles, max_particles,
                     num_textures, num_emitters, max_emitters, num_groups, max_groups);
    }
}

int GetNumParticleTextures() {
    return 0;
}

// The Xenon spark emitters: speed.exe has none (its SpawnParticles never
// calls AddXenonEffect and EmitterSystem::Update / Render no Xenon updates
// or draws), so they stay off natively.
void AddXenonEffect(EmitterGroup *piggyback_fx, const Attrib::Collection *spec, const UMath::Matrix4 *mat, const UMath::Vector4 *vel) {}
void UpdateXenonEmitters(float dt) {}
void DrawXenonEmitters(eView *view) {}

// 0x6D1A40: the quad as a triangle fan (DrawPrimitiveUP, 0x20-byte vertices:
// position, D3DCOLOR from Colours (R, G, B, A bytes), uv) with WorldViewProj
// = local_to_world x the view's, in the effect the caller began.
void eViewPlatInterface::Render(ePoly *poly, TextureInfo *texture_info, bMatrix4 *local_to_world, int use_previous_data, float bbRad) {
    eView *view = static_cast<eView *>(this);
    if (!DrawsPolys(view)) {
        return;
    }
    uint32_t flags;
    PortPrelitBlend blend = BlendFor(texture_info, &flags);
    PortPrelitVertex quad[4];
    for (int i = 0; i < 4; i++) {
        bVector3 p = poly->Vertices[i];
        if (local_to_world != nullptr) {
            const bMatrix4 &m = *local_to_world;
            bVector3 q = p;
            p.x = q.x * m.v0.x + q.y * m.v1.x + q.z * m.v2.x + m.v3.x;
            p.y = q.x * m.v0.y + q.y * m.v1.y + q.z * m.v2.y + m.v3.y;
            p.z = q.x * m.v0.z + q.y * m.v1.z + q.z * m.v2.z + m.v3.z;
        }
        const uint8 *c = poly->Colours[i];
        uint32_t colour = (uint32_t)c[3] << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
        SetVertex(quad[i], p.x, p.y, p.z, colour, poly->UVs[i][0], poly->UVs[i][1]);
    }
    PortMetalPrelitAddQuads(MetalTextureOf(texture_info), blend, flags, quad, 1);
    gPolysThisFrame++;
}

void NotifyLibOfDeletion(void *lib, EmitterGroup *grp); // EmitterSystem.cpp

// ---------------------------------------------------------------------------
// Emitter chunks. 0x3BB00 (emitter groups) is accepted and ignored, as on PC.
// 0x3BC00 holds EmitterSystem's world-effect libraries: a header, then per
// library 0x50 bytes (key, section, trigger count, a 32-bit group slot, the
// local-world matrix) followed by its triggers (0x30 bytes, starting with
// 32-bit list links and a library pointer). The libraries stay in place
// (mGroup is a PORT_PTR32); the triggers are copied into native
// WorldFXTrigger nodes.
// ---------------------------------------------------------------------------
namespace {

const int kDiskLibrarySize = 0x50;
const int kDiskTriggerSize = 0x30;
static_assert(sizeof(EmitterLibrary) == kDiskLibrarySize, "EmitterLibrary must keep its on-disk layout");

struct DiskTrigger {
    uint32_t next, prev, lib;
    float trigger_radius;
    float world_pos[4];
    float reset_time;
    uint16_t probability;
    uint8_t state;
    uint8_t flags;
    float last_end;
    uint32_t pad;
};
static_assert(sizeof(DiskTrigger) == kDiskTriggerSize, "WorldFXTrigger on-disk layout");

void bPlatEndianSwapAll(DiskTrigger *t) {
    bPlatEndianSwap(&t->trigger_radius);
    for (float &f : t->world_pos) {
        bPlatEndianSwap(&f);
    }
    bPlatEndianSwap(&t->reset_time);
    bPlatEndianSwap(&t->probability);
}

template <typename F> void ForEachLibrary(EmitterLibraryHeader *header, F f) {
    uint8_t *p = reinterpret_cast<uint8_t *>(header + 1);
    for (int i = 0; i < header->NumEmitterLibraries; i++) {
        EmitterLibrary *lib = reinterpret_cast<EmitterLibrary *>(p);
        DiskTrigger *triggers = reinterpret_cast<DiskTrigger *>(p + kDiskLibrarySize);
        f(lib, triggers);
        p += kDiskLibrarySize + lib->mNumTriggers * kDiskTriggerSize;
    }
}

} // namespace

int32 EmitterSystem::Loader(bChunk *bchunk) {
    if (bchunk->GetID() == BCHUNK_SPEED_EMITTER_GROUP) {
        return 1;
    }
    if (bchunk->GetID() != BCHUNK_SPEED_EMITTER_LIBRARY) {
        return 0;
    }
    EmitterLibraryHeader *header = reinterpret_cast<EmitterLibraryHeader *>(bchunk->GetAlignedData(16));
    const bool swap = !header->EndianSwapped;
    if (swap) {
        bPlatEndianSwap(&header->Version);
        bPlatEndianSwap(&header->NumEmitterLibraries);
        bPlatEndianSwap(&header->SectionNumber);
    }
    ForEachLibrary(header, [&](EmitterLibrary *lib, DiskTrigger *triggers) {
        if (swap) {
            lib->EndianSwap();
            for (int j = 0; j < lib->mNumTriggers; j++) {
                bPlatEndianSwapAll(&triggers[j]);
            }
        }
        bPlatEndianSwap(&lib->GroupKey);
        lib->mGroup = nullptr;
        gEmitterSystem.AddLibrary(lib);
        if (lib->SectionNumber == 0) {
            return;
        }
        if (lib->mNumTriggers == 0) {
            // Section 0xA28 (2600): created right away; the others when the
            // camera nears them (ServiceWorldEffects).
            if (lib->SectionNumber == 0xA28) {
                EmitterGroup *group = gEmitterSystem.CreateEmitterGroup(lib->GroupKey, 0x8020000);
                if (group != nullptr) {
                    group->SubscribeToDeletion(lib, NotifyLibOfDeletion);
                    lib->mGroup = group;
                    group->SetLocalWorld(&lib->LocalWorld);
                    group->SetAutoUpdate(true);
                }
            }
            return;
        }
        for (int j = 0; j < lib->mNumTriggers; j++) {
            const DiskTrigger &d = triggers[j];
            WorldFXTrigger *trig = new WorldFXTrigger;
            trig->mLib = lib;
            trig->mTriggerRadius = d.trigger_radius;
            trig->mWorldPos = bVector3(d.world_pos[0], d.world_pos[1], d.world_pos[2]);
            trig->mResetTime = d.reset_time;
            trig->mProbability = d.probability;
            trig->mState = 1;
            trig->mFlags = d.flags;
            trig->mLastEnd = d.last_end;
            trig->pad = 0;
            gEmitterSystem.AddTrigger(trig);
        }
    });
    header->EndianSwapped = 1;
    return 1;
}

int32 EmitterSystem::Unloader(bChunk *bchunk) {
    if (bchunk->GetID() == BCHUNK_SPEED_EMITTER_GROUP) {
        return 1;
    }
    if (bchunk->GetID() != BCHUNK_SPEED_EMITTER_LIBRARY) {
        return 0;
    }
    EmitterLibraryHeader *header = reinterpret_cast<EmitterLibraryHeader *>(bchunk->GetAlignedData(16));
    ForEachLibrary(header, [&](EmitterLibrary *lib, DiskTrigger *) {
        EmitterGroup *group = lib->mGroup;
        delete group;
        bTList<WorldFXTrigger> &trigs = gEmitterSystem.GetTriggers();
        for (WorldFXTrigger *trig = trigs.GetHead(); trig != trigs.EndOfList();) {
            WorldFXTrigger *next = trig->GetNext();
            if (trig->mLib == lib) {
                gEmitterSystem.KillTrigger(trig);
                delete trig;
            }
            trig = next;
        }
        gEmitterSystem.RemoveLibrary(lib);
    });
    return 1;
}

bChunkLoader bChunkLoaderEmitterGroups(BCHUNK_SPEED_EMITTER_GROUP, EmitterSystem::Loader, EmitterSystem::Unloader);
bChunkLoader bChunkLoaderEmitterLibraries(BCHUNK_SPEED_EMITTER_LIBRARY, EmitterSystem::Loader, EmitterSystem::Unloader);

// ---------------------------------------------------------------------------
// Scene hooks (ParticlesNative.h).
// ---------------------------------------------------------------------------
#include "Speed/Indep/Src/Ecstasy/eLight.hpp"
#include "Speed/Indep/Src/World/Clans.hpp"
#include "Speed/PC/Src/PCGraphicsOptions.hpp"

void RenderSunAsFlare();                                                 // Sun.cpp
void eRenderWorldLightFlares(eView *view, flareType type);               // eLight.cpp
void eRenderLightFlarePool(eView *view);                                 // eLight.cpp
void RenderVehicleFlares(eView *view, int reflection, int renderFlareFlags); // CarRender.cpp

void PortRenderSkids(eView *view) {
    RenderClans(view);
}

// ___eDisplayFrame's flares in the other views, with 0x9017E4 set, under
// IDI_WORLDPRELIT_FX: the road reflection (view 4, 0x6DE57B:
// eRenderWorldLightFlares type 2, RenderVehicleFlares(view, 1, 0),
// eRenderLightFlarePool) and each env-map face (0x6DE7F4: type 1, the pool,
// RenderVehicleFlares(view, 0, 1)), into that view's prelit part.
void PortRenderViewFlares(eView *view, int part) {
    if (gPCGraphicsOptions.Unknown34 == 0) {
        return;
    }
    PortMetalPrelitSetPart(part);
    if (part == PORT_PRELIT_PART_REFLECTION) {
        eRenderWorldLightFlares(view, FLARE_REFLECT);
        RenderVehicleFlares(view, 1, 0);
        eRenderLightFlarePool(view);
    } else {
        eRenderWorldLightFlares(view, FLARE_ENV);
        eRenderLightFlarePool(view);
        RenderVehicleFlares(view, 0, 1);
    }
    PortMetalPrelitSetPart(0);
}

void PortRenderWorldEffects(eView *view) {
    // (ePreDisplay, at the start of the frame, added the sun's flares to the
    // pool and read the flare attributes: FERenderMetal.cpp.)
    // 0x6DF2A3: with 0x9017E4 set, under IDI_WORLDPRELIT_FX with
    // ZWRITEENABLE 0, the world, car and pool flares; with
    // g_X360EffectsEnable (0x9017E0) sub_6DAE20 draws them into its
    // persistent flare target instead (PortMetal3DBeginX360Flares).
    if (gPCGraphicsOptions.Unknown34 != 0) {
        const bool x360 = gPCGraphicsOptions.X360EffectsEnable != 0;
        if (x360) {
            PortMetal3DBeginX360Flares();
        }
        eRenderWorldLightFlares(view, FLARE_NORM);
        RenderVehicleFlares(view, 0, 0);
        eRenderLightFlarePool(view);
        if (x360) {
            PortMetal3DEndX360Flares();
        }
    }
    // 0x6DF472: the particles (g_ParticleSystemEnable), IDI_PARTICLES_FX.
    if (gPCGraphicsOptions.ParticleSystemEnable != 0) {
        gEmitterSystem.Render(view);
    }
    static const bool trace = std::getenv("NFSMW_TRACE_PARTICLES") != nullptr;
    static int frame;
    if (trace && frame++ % 120 == 0) {
        extern int ActivePoolIndex;
        std::fprintf(stderr, "[prelit] %d polys (skids, flares), %d pool flares\n", gPolysThisFrame, ActivePoolIndex);
    }
    gPolysThisFrame = 0;
    // (ePostDisplay resets the flare pool at the end of the frame.)
}

// ---------------------------------------------------------------------------
// Immediate strips (car drop shadows: DrawAmbientShadow / DrawKeithProjShadow,
// and any other eBeginStrip users). PC 0x6BEA90 / 0x6BED50 begin a strip in a
// slot (vertices x,y,z, D3DCOLOR, u,v) with the texture and local -> world
// matrix (identity when null); the add functions fill it; 0x6CF930 /
// 0x6CF970 end it by queueing it (sub_6E0770) in effect dword_93DEBC
// (WORLDPRELIT, 17) with the texture's blend state. eAddColour/exAddColour
// swap bytes 0 and 2 of the colour (the decomp's 0xAABBGGRR to D3DCOLOR).
// Natively the strip goes to the prelit pass as quads, two strip triangles
// at a time (a lone last triangle as a degenerate quad).
namespace {
struct StripState {
    TextureInfo *texture = nullptr;
    bMatrix4 matrix;
    bool have_matrix = false;
    std::vector<PortPrelitVertex> verts;
    int num_pos = 0, num_col = 0, num_uv = 0;
};
StripState gStrip;

bool BeginStripCommon(TextureInfo *texture, int num_verts, bMatrix4 *matrix) {
    gStrip.texture = texture;
    gStrip.have_matrix = matrix != nullptr;
    if (matrix != nullptr) {
        gStrip.matrix = *matrix;
    }
    gStrip.verts.assign(num_verts > 0 ? num_verts : 0, PortPrelitVertex{});
    gStrip.num_pos = gStrip.num_col = gStrip.num_uv = 0;
    return true;
}

PortPrelitVertex &StripSlot(int &counter) {
    if (counter >= static_cast<int>(gStrip.verts.size())) {
        gStrip.verts.resize(counter + 1, PortPrelitVertex{});
    }
    return gStrip.verts[counter++];
}

void AddVertexCommon(const bVector3 &v) {
    bVector3 p = v;
    if (gStrip.have_matrix) {
        const bMatrix4 &m = gStrip.matrix;
        p.x = v.x * m.v0.x + v.y * m.v1.x + v.z * m.v2.x + m.v3.x;
        p.y = v.x * m.v0.y + v.y * m.v1.y + v.z * m.v2.y + m.v3.y;
        p.z = v.x * m.v0.z + v.y * m.v1.z + v.z * m.v2.z + m.v3.z;
    }
    PortPrelitVertex &s = StripSlot(gStrip.num_pos);
    s.x = p.x;
    s.y = p.y;
    s.z = p.z;
}

void AddColourCommon(unsigned int colour) {
    StripSlot(gStrip.num_col).colour = (colour & 0xFF00FF00) | ((colour & 0xFF) << 16) | ((colour >> 16) & 0xFF);
}

void AddUVCommon(float u, float v) {
    PortPrelitVertex &s = StripSlot(gStrip.num_uv);
    s.u = u;
    s.v = v;
}

bool EndStripCommon(eView *view) {
    const int n = gStrip.num_pos;
    if (n < 3 || !DrawsPrelit(view)) {
        return true;
    }
    uint32_t flags;
    PortPrelitBlend blend = BlendFor(gStrip.texture, &flags);
    std::vector<PortPrelitVertex> quads;
    const std::vector<PortPrelitVertex> &v = gStrip.verts;
    for (int i = 0; i + 2 < n; i += 2) {
        // Strip triangles (i, i+1, i+2) and (i+1, i+3, i+2) form the quad
        // i, i+1, i+3, i+2 around its perimeter.
        quads.push_back(v[i]);
        quads.push_back(v[i + 1]);
        if (i + 3 < n) {
            quads.push_back(v[i + 3]);
        } else {
            quads.push_back(v[i + 2]);
        }
        quads.push_back(v[i + 2]);
    }
    PortMetalPrelitAddQuads(MetalTextureOf(gStrip.texture), blend, flags, quads.data(), static_cast<uint32_t>(quads.size() / 4));
    return true;
}
} // namespace

bool eBeginStrip(TextureInfo *texture, int num_verts, bMatrix4 *matrix) {
    return BeginStripCommon(texture, num_verts, matrix);
}
bool exBeginStrip(TextureInfo *texture, int num_verts, bMatrix4 *matrix) {
    return BeginStripCommon(texture, num_verts, matrix);
}
void eAddVertex(const bVector3 &v) {
    AddVertexCommon(v);
}
void exAddVertex(const bVector3 &v) {
    AddVertexCommon(v);
}
void eAddColour(unsigned int colour) {
    AddColourCommon(colour);
}
void exAddColour(unsigned int colour) {
    AddColourCommon(colour);
}
void eAddUV(float u, float v) {
    AddUVCommon(u, v);
}
void exAddUV(float u, float v) {
    AddUVCommon(u, v);
}
bool eEndStrip(eView *view) {
    return EndStripCommon(view);
}
bool exEndStrip(eView *view) {
    return EndStripCommon(view);
}
