// PC shadow-map meshes (speed.exe 1.3): when a scenery section connects
// (SectionConnectionCallback 0x9A3A40 = 0x6DC3F0 -> sub_6D7C40, installed by
// the list's constructor 0x6DC430), the opaque casters among its instances are
// merged into one position-only mesh, transformed into world space. The
// shadow pass (sub_6E44C0 0x6E535B, with 0x8FAECC = 1) draws every connected
// section's mesh under IDI_SHADOW_MAP_MESH_FX (effect 20; 29, the depth
// variant, without depth-texture shadows), and those instances are flagged
// 0x4000000 so they aren't stuffed into the shadow view one by one
// (Scenery.cpp). SectionDisconnectionCallback (0x6DC410 -> sub_6D7F10)
// frees a section's mesh.
//
// sub_6D7C40, per instance: skipped when its LOD 0 solid is one of
// SimpleModelAnim's (sub_73D4E0), when flagged 0x40000000, or when not a
// 0x300000 caster; merged when every group of the solid is WORLD,
// WORLDREFLECT, WORLDNORMALMAP, GLOSSYWINDOW or TREE with a diffuse texture
// that isn't AlphaUsageType 1. Solids named SHADOW* (shadow-only proxies)
// get the instance flag 0x10, which every view's cull excludes. The mesh is
// only made with Shadow Detail on.

#include "Speed/PC/Src/PCGraphicsOptions.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/World/Scenery.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"
#include "PortEcstasy/eSolidPlatNative.hpp"

#include "../Mac/MetalBridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <strings.h>
#include <vector>

TextureInfo *PortSolidTexture(eSolid *solid, int index); // SolidNative.cpp
const void *PortMetalBufferContents(void *buffer);       // MetalRenderer.mm

extern void (*SectionConnectionCallback)(ScenerySectionHeader *);
extern void (*SectionDisconnectionCallback)(ScenerySectionHeader *);

namespace {

// One draw of a section's mesh: 16-bit indices, so a section is split into
// chunks of at most 65536 vertices.
struct Chunk {
    void *vertices; // retained id<MTLBuffer>, float3 positions
    void *indices;  // retained id<MTLBuffer>, uint16
    uint32 num_indices;
};

std::map<ScenerySectionHeader *, std::vector<Chunk>> gMeshes;

bool IsSimpleModelAnim(uint32 hash) {
    // gSimpleSolidHashList (SimpleModelAnim::Init).
    static const uint32 kHashes[2] = {bStringHash("XO_WINDMILL_BLADE_1B_JG_00"), bStringHash("XB_DONUTSIGNB_1B_BK_00")};
    return hash == kHashes[0] || hash == kHashes[1];
}

bool Mergeable(eSolid *solid) {
    eSolidPlatInfo *info = solid->GetPlatInfo();
    if (info == nullptr || info->NumGroups <= 0) {
        return false;
    }
    for (int g = 0; g < info->NumGroups; g++) {
        const eSolidPlatGroup &group = info->Groups[g];
        switch (group.Effect) {
        case PORT_EFFECT_WORLD:
        case PORT_EFFECT_WORLDREFLECT:
        case PORT_EFFECT_WORLDNORMALMAP:
        case PORT_EFFECT_GLOSSYWINDOW:
        case PORT_EFFECT_TREE:
            break;
        default:
            return false;
        }
        TextureInfo *texture = PortSolidTexture(solid, group.TextureSlots[0]);
        if (texture == nullptr || texture->AlphaUsageType == 1) {
            return false;
        }
    }
    return true;
}

struct Builder {
    std::vector<float> positions;
    std::vector<uint16> indices;
    std::vector<Chunk> chunks;

    void Flush() {
        if (!indices.empty()) {
            Chunk c;
            c.vertices = PortMetalCreateBuffer(positions.data(), positions.size() * sizeof(float), "Shadow-map mesh vertices");
            c.indices = PortMetalCreateBuffer(indices.data(), indices.size() * sizeof(uint16), "Shadow-map mesh indices");
            c.num_indices = static_cast<uint32>(indices.size());
            if (c.vertices && c.indices) {
                chunks.push_back(c);
            }
        }
        positions.clear();
        indices.clear();
    }

    // sub_6C9860: the solid's groups, positions times the instance matrix.
    void Add(eSolid *solid, const bMatrix4 &m) {
        eSolidPlatInfo *info = solid->GetPlatInfo();
        const uint16 *src_indices = static_cast<const uint16 *>(PortMetalBufferContents(info->IndexBuffer));
        if (src_indices == nullptr) {
            return;
        }
        for (int g = 0; g < info->NumGroups; g++) {
            const eSolidPlatGroup &group = info->Groups[g];
            const char *vb = static_cast<const char *>(PortMetalBufferContents(info->VertexBuffers[group.VertexBuffer]));
            const uint32 stride = info->VertexStrides[group.VertexBuffer];
            if (vb == nullptr || group.NumIndices == 0) {
                continue;
            }
            // The group's vertex range.
            uint32 lo = 0xFFFF, hi = 0;
            for (uint32 k = 0; k < group.NumIndices; k++) {
                uint32 i = src_indices[group.FirstIndex + k];
                lo = i < lo ? i : lo;
                hi = i > hi ? i : hi;
            }
            const uint32 count = hi - lo + 1;
            if (positions.size() / 3 + count > 0x10000) {
                Flush();
            }
            const uint32 base = static_cast<uint32>(positions.size() / 3);
            for (uint32 v = lo; v <= hi; v++) {
                const float *p = reinterpret_cast<const float *>(vb + static_cast<size_t>(v) * stride);
                positions.push_back(p[0] * m.v0.x + p[1] * m.v1.x + p[2] * m.v2.x + m.v3.x);
                positions.push_back(p[0] * m.v0.y + p[1] * m.v1.y + p[2] * m.v2.y + m.v3.y);
                positions.push_back(p[0] * m.v0.z + p[1] * m.v1.z + p[2] * m.v2.z + m.v3.z);
            }
            for (uint32 k = 0; k < group.NumIndices; k++) {
                indices.push_back(static_cast<uint16>(src_indices[group.FirstIndex + k] - lo + base));
            }
        }
    }
};

void Release(std::vector<Chunk> &chunks) {
    for (Chunk &c : chunks) {
        PortMetalReleaseBuffer(c.vertices);
        PortMetalReleaseBuffer(c.indices);
    }
    chunks.clear();
}

// sub_6D7C40.
void SectionConnected(ScenerySectionHeader *section) {
    Builder builder;
    int merged = 0;
    for (int n = 0; n < section->NumSceneryInstances; n++) {
        SceneryInstance *instance = section->GetSceneryInstance(n);
        SceneryInfo *info = section->GetSceneryInfo(instance->SceneryInfoNumber);
        if (IsSimpleModelAnim(info->NameHash[0]) || (instance->ExcludeFlags & 0x40000000) != 0 || (instance->ExcludeFlags & 0x300000) == 0) {
            continue;
        }
        eModel *model = info->pModel[0];
        eSolid *solid = model != nullptr ? model->GetSolid() : nullptr;
        if (solid == nullptr) {
            continue;
        }
        if (Mergeable(solid) && gPCGraphicsOptions.ShadowDetail > 0) {
            bMatrix4 m;
            instance->GetMatrix(&m);
            builder.Add(solid, m);
            instance->ExcludeFlags |= 0x4000000;
            merged++;
        }
        if (strncasecmp(solid->Name, "SHADOW", 6) == 0) {
            instance->ExcludeFlags |= 0x10;
        }
    }
    builder.Flush();
    static const bool trace = std::getenv("NFSMW_TRACE_SHADOW_MESH") != nullptr;
    if (trace) {
        size_t indices = 0;
        for (const Chunk &c : builder.chunks) {
            indices += c.num_indices;
        }
        std::fprintf(stderr, "[shadowmesh] section %d: %d of %d instances merged, %zu chunks, %zu triangles\n", section->GetSectionNumber(), merged,
                     section->NumSceneryInstances, builder.chunks.size(), indices / 3);
    }
    if (!builder.chunks.empty()) {
        Release(gMeshes[section]);
        gMeshes[section] = std::move(builder.chunks);
    }
}

// sub_6D7F10.
void SectionDisconnected(ScenerySectionHeader *section) {
    auto it = gMeshes.find(section);
    if (it != gMeshes.end()) {
        Release(it->second);
        gMeshes.erase(it);
    }
}

// 0x6DC430: installs the callbacks unless something else did.
struct Install {
    Install() {
        if (SectionConnectionCallback == nullptr) {
            SectionConnectionCallback = SectionConnected;
        }
        if (SectionDisconnectionCallback == nullptr) {
            SectionDisconnectionCallback = SectionDisconnected;
        }
    }
} gInstall;

} // namespace

// sub_6D7F70, in the shadow pass (between PortMetal3DBeginShadow and End):
// every connected section's mesh, identity world matrix.
void PortRenderShadowMapMeshes() {
    static const bool off = std::getenv("NFSMW_NO_SHADOW_MESH") != nullptr;
    if (off) {
        return;
    }
    for (auto &entry : gMeshes) {
        for (const Chunk &c : entry.second) {
            Port3DDraw d = {};
            d.vertex_buffer = c.vertices;
            d.vertex_stride = 12;
            d.index_buffer = c.indices;
            d.num_indices = c.num_indices;
            d.effect = 20; // IDI_SHADOW_MAP_MESH_FX
            d.world[0] = d.world[5] = d.world[10] = d.world[15] = 1.0f;
            PortMetal3DAdd(&d);
        }
    }
}
