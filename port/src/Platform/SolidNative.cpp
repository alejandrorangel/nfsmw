// Native eSolid loading for PC geometry packs (CARS\*\GEOMETRY.BIN, world and
// global bundles). The disk records are 32-bit and used in place on PC; here
// each solid gets a native eSolid copy, and its mesh goes into Metal buffers.
//
// Layout of the PC solid pack (chunk 0x80134010), from speed.exe 1.3's
// InternalLoaderSolidChunks @ 0x5070E0:
//   0x134011  eSolid (16-byte aligned), same field offsets as the 32-bit class
//   0x134012  texture table, 8-byte {name hash, TextureInfo*}
//   0x134013  light material table, 8-byte {name hash, eLightMaterial*}
//   0x13401A  position markers (16-byte aligned, 0x50 each)
//   0x134017-19, 0x13401B  normal smoother / damage data (not used yet)
//   0x80134100 mesh (see eSolidPlatNative.hpp)

#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Ecstasy/EcstasyData.hpp"
#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/Ecstasy/eLight.hpp"
#include "Speed/Indep/bWare/Inc/bChunk.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

#include "../Mac/MetalBridge.h"

#include <alloca.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

bTList<eSolid> SolidList;

namespace {

const uint32 kSolidPack = 0x80134010;
const uint32 kSolidInfo = 0x134011;
const uint32 kSolidTextures = 0x134012;
const uint32 kSolidLightMaterials = 0x134013;
const uint32 kSolidMarkers = 0x13401A;
const uint32 kMesh = 0x80134100;
const uint32 kMeshHeader = 0x134900;
const uint32 kMeshGroups = 0x134B02;
const uint32 kMeshIndices = 0x134B03;
const uint32 kMeshVertices = 0x134B01;

const int kDiskGroupSize = 0x68;
const int kDiskMarkerSize = 0x50;

template <typename T> T Read(const char *p) {
    T v;
    bMemCpy(&v, p, sizeof(v));
    return v;
}

// PC chunk data is aligned by 0x11 fill bytes after the chunk header (16
// bytes for most records, 128 for vertex buffers). Skip the fill rather than
// aligning the pointer, so the result doesn't depend on the load buffer's
// alignment.
char *SkipFill(bChunk *c, int *size) {
    char *p = c->GetData();
    char *end = p + c->GetSize();
    while (p + 4 <= end && (p - c->GetData()) < 0x80 && Read<uint32>(p) == 0x11111111) {
        p += 4;
    }
    *size = static_cast<int>(end - p);
    return p;
}

void Warn(const char *solid_name, const char *what) {
    static int count;
    if (count++ < 20) {
        std::fprintf(stderr, "[solid] %s: %s\n", solid_name, what);
    }
}

// NFSMW_DUMP_MESH=<dir>: writes each mesh chunk (header included) to
// <dir>/<solid>.mesh, for studying the format offline.
void DumpMesh(bChunk *mesh, const char *solid_name) {
    static const char *dir = std::getenv("NFSMW_DUMP_MESH");
    if (dir == nullptr) {
        return;
    }
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.mesh", dir, solid_name);
    for (char *p = path + std::strlen(dir) + 1; *p; p++) {
        if (*p == ' ' || *p == '/' || *p == '\\') {
            *p = '_';
        }
    }
    if (FILE *f = std::fopen(path, "wb")) {
        std::fwrite(mesh, 1, 8 + mesh->GetSize(), f);
        std::fclose(f);
    }
}

eSolidPlatInfo *LoadMesh(bChunk *mesh, const char *solid_name) {
    DumpMesh(mesh, solid_name);
    const char *groups = nullptr;
    int group_bytes = 0;
    const uint16 *indices = nullptr;
    int index_bytes = 0;
    int num_vertex_buffers = 0;
    for (bChunk *c = mesh->GetFirstChunk(); c < mesh->GetLastChunk(); c = c->GetNext()) {
        num_vertex_buffers += c->GetID() == kMeshVertices;
    }
    if (num_vertex_buffers == 0) {
        return nullptr;
    }
    const char **vertices = static_cast<const char **>(alloca(num_vertex_buffers * sizeof(char *)));
    int *vertex_bytes = static_cast<int *>(alloca(num_vertex_buffers * sizeof(int)));
    uint32 *buffer_vertices = static_cast<uint32 *>(alloca(num_vertex_buffers * sizeof(uint32)));
    std::memset(buffer_vertices, 0, num_vertex_buffers * sizeof(uint32));
    num_vertex_buffers = 0;

    for (bChunk *c = mesh->GetFirstChunk(); c < mesh->GetLastChunk(); c = c->GetNext()) {
        switch (c->GetID()) {
            case kMeshGroups:
                groups = SkipFill(c, &group_bytes);
                break;
            case kMeshIndices:
                indices = reinterpret_cast<const uint16 *>(SkipFill(c, &index_bytes));
                break;
            case kMeshVertices:
                vertices[num_vertex_buffers] = SkipFill(c, &vertex_bytes[num_vertex_buffers]);
                num_vertex_buffers++;
                break;
            default:
                break;
        }
    }
    int num_groups = groups ? group_bytes / kDiskGroupSize : 0;
    if (num_groups == 0 || !indices || num_vertex_buffers == 0) {
        return nullptr;
    }

    eSolidPlatInfo *info = static_cast<eSolidPlatInfo *>(bMalloc(sizeof(eSolidPlatInfo), "eSolidPlatInfo (native)", 0, 0));
    bMemSet(info, 0, sizeof(*info));
    info->Groups = static_cast<eSolidPlatGroup *>(bMalloc(num_groups * sizeof(eSolidPlatGroup), "eSolidPlatGroup (native)", 0, 0));
    info->NumGroups = num_groups;
    info->VertexBuffers = static_cast<void **>(bMalloc(num_vertex_buffers * sizeof(void *), "eSolidPlatInfo VBs", 0, 0));
    info->VertexStrides = static_cast<uint32 *>(bMalloc(num_vertex_buffers * sizeof(uint32), "eSolidPlatInfo strides", 0, 0));

    // Assign groups to vertex buffers and count each buffer's vertices.
    int vb = 0;
    uint32 total_indices = 0;
    bool ok = true;
    for (int i = 0; i < num_groups && ok; i++) {
        const char *g = groups + i * kDiskGroupSize;
        eSolidPlatGroup &out = info->Groups[i];
        bMemCpy(out.AABBMin, g + 0x00, 12);
        bMemCpy(out.AABBMax, g + 0x0C, 12);
        bMemCpy(out.TextureSlots, g + 0x18, 6);
        out.Effect = static_cast<uint16>(Read<uint32>(g + 0x30));
        {
            static const bool trace = std::getenv("NFSMW_TRACE_SOLID_EFFECTS") != nullptr;
            static int seen[64];
            if (trace && out.Effect < 64 && seen[out.Effect]++ < 6) {
                std::fprintf(stderr, "[solidfx] effect %u in %s group %d (stride? flags 0x%x)\n", out.Effect, solid_name, i, out.Flags);
            }
        }
        out.Flags = Read<uint32>(g + 0x38);
        out.NumVertices = Read<uint32>(g + 0x3C);
        out.FirstIndex = Read<uint32>(g + 0x44);
        out.NumIndices = Read<uint32>(g + 0x5C);
        if ((out.FirstIndex + out.NumIndices) * 2 > static_cast<uint32>(index_bytes)) {
            ok = false;
            break;
        }
        uint32 min_index = 0xFFFF;
        for (uint32 k = 0; k < out.NumIndices; k++) {
            min_index = indices[out.FirstIndex + k] < min_index ? indices[out.FirstIndex + k] : min_index;
        }
        if (min_index == 0 && buffer_vertices[vb] != 0) {
            vb++;
        }
        if (vb >= num_vertex_buffers) {
            ok = false;
            break;
        }
        out.VertexBuffer = static_cast<uint16>(vb);
        buffer_vertices[vb] += out.NumVertices;
        if (out.FirstIndex + out.NumIndices > total_indices) {
            total_indices = out.FirstIndex + out.NumIndices;
        }
    }
    if (ok && vb + 1 != num_vertex_buffers) {
        ok = false;
    }
    for (int b = 0; ok && b < num_vertex_buffers; b++) {
        if (buffer_vertices[b] == 0 || vertex_bytes[b] % buffer_vertices[b] != 0) {
            ok = false;
        } else {
            info->VertexStrides[b] = vertex_bytes[b] / buffer_vertices[b];
            ok = info->VertexStrides[b] >= 36 && info->VertexStrides[b] % 4 == 0;
        }
    }
    if (!ok) {
        Warn(solid_name, "mesh layout not understood; skipped");
        bFree(info->VertexBuffers);
        bFree(info->VertexStrides);
        bFree(info->Groups);
        bFree(info);
        return nullptr;
    }
    for (int b = 0; b < num_vertex_buffers; b++) {
        info->VertexBuffers[b] = PortMetalCreateBuffer(vertices[b], vertex_bytes[b], solid_name);
    }
    info->NumVertexBuffers = num_vertex_buffers;
    info->NumIndices = total_indices;
    info->IndexBuffer = PortMetalCreateBuffer(indices, total_indices * 2, solid_name);
    return info;
}

} // namespace

// The native copy of a solid pack's eSolid, remembered in the disk record.
eSolid *PortNativeSolid(bChunk *info_chunk) {
    int unused;
    return Read<eSolid *>(SkipFill(info_chunk, &unused));
}

// Builds the native eSolid for one solid pack chunk (registration with the
// solid lists is left to InternalLoaderSolidChunks).
eSolid *PortCreateSolid(bChunk *chunk) {
    if (chunk->GetID() != kSolidPack) {
        return nullptr;
    }
    eSolid *solid = nullptr;
    for (bChunk *c = chunk->GetFirstChunk(); c < chunk->GetLastChunk(); c = c->GetNext()) {
        uint32 id = c->GetID();
        if (id == kSolidInfo) {
            int unused;
            char *disk = SkipFill(c, &unused);
            solid = static_cast<eSolid *>(bMalloc(sizeof(eSolid), "eSolid (native)", 0, 0));
            bMemSet(solid, 0, sizeof(eSolid));
            bMemCpy(&solid->Version, disk + 0x0C, 0x14); // Version .. ReferencedFrameCounter
            bMemCpy(&solid->AABBMinX, disk + 0x20, 12);
            bMemCpy(&solid->AABBMaxX, disk + 0x30, 12);
            bMemCpy(&solid->PivotMatrix, disk + 0x40, sizeof(bMatrix4));
            bMemCpy(&solid->Volume, disk + 0x98, 8); // Volume, Density
            bMemCpy(solid->Name, disk + 0xA0, sizeof(solid->Name));
            solid->Name[sizeof(solid->Name) - 1] = 0;
            solid->ModelList.InitList();
            bMemCpy(disk, &solid, sizeof(solid));
        } else if (solid == nullptr) {
            continue;
        } else if (id == kSolidTextures) {
            int n = solid->NumTextureTableEntries;
            if (n > 0) {
                solid->pTextureTable = static_cast<eTextureEntry *>(bMalloc(n * sizeof(eTextureEntry), "eTextureEntry (native)", 0, 0));
                for (int i = 0; i < n; i++) {
                    solid->pTextureTable[i].NameHash = Read<uint32>(c->GetData() + i * 8);
                    solid->pTextureTable[i].pTextureInfo = nullptr;
                }
            }
        } else if (id == kSolidLightMaterials) {
            int n = solid->NumLightMaterials;
            if (n > 0) {
                solid->LightMaterialTable =
                    static_cast<eLightMaterialEntry *>(bMalloc(n * sizeof(eLightMaterialEntry), "eLightMaterialEntry (native)", 0, 0));
                for (int i = 0; i < n; i++) {
                    uint32 hash = Read<uint32>(c->GetData() + i * 8);
                    solid->LightMaterialTable[i].NameHash = hash;
                    solid->LightMaterialTable[i].LightMaterial = elGetLightMaterial(hash);
                }
            }
        } else if (id == kSolidMarkers) {
            // No pointers inside: used in place.
            int bytes;
            solid->PositionMarkerTable = reinterpret_cast<ePositionMarker *>(SkipFill(c, &bytes));
            solid->NumPositionMarkerTableEntries = static_cast<int8>(bytes / kDiskMarkerSize);
        } else if (id == kMesh) {
            solid->PortSetPlatInfo(LoadMesh(c, solid->Name));
        }
    }
    if (solid) {
        solid->EndianSwapped = 1;
    }
    return solid;
}

void PortFreeSolid(eSolid *solid) {
    if (solid->pTextureTable) {
        bFree(solid->pTextureTable);
    }
    if (solid->LightMaterialTable) {
        bFree(solid->LightMaterialTable);
    }
    bFree(solid);
}

int eSolidPlatInterface::UnloaderPlatChunks(bChunk *chunk) {
    eSolidPlatInfo *info = PlatInfo;
    if (info) {
        for (int b = 0; b < info->NumVertexBuffers; b++) {
            PortMetalReleaseBuffer(info->VertexBuffers[b]);
        }
        PortMetalReleaseBuffer(info->IndexBuffer);
        bFree(info->VertexBuffers);
        bFree(info->VertexStrides);
        bFree(info->Groups);
        bFree(info);
        PlatInfo = nullptr;
    }
    return 1;
}

// The mesh data lives in Metal buffers, so moving chunks needs no fix-ups.
int eSolidPlatInterface::FixPlatInfo() {
    return 1;
}

int eSolidPlatInterface::UnFixPlatInfo() {
    return 1;
}

// Solid list header platform chunks (0x80134008): empty on PC.
int eLoadSolidListPlatChunks(bChunk *chunk) {
    return 1;
}

int eUnloadSolidListPlatChunks(bChunk *chunk) {
    return 1;
}

// Texture entries are resolved on first use (PortSolidTexture).
TextureInfo *PortSolidTexture(eSolid *solid, int index) {
    if (index < 0 || index >= solid->NumTextureTableEntries || !solid->pTextureTable) {
        return nullptr;
    }
    eTextureEntry &entry = solid->pTextureTable[index];
    if (!entry.pTextureInfo) {
        entry.pTextureInfo = GetTextureInfo(entry.NameHash, 0, 0);
    }
    return entry.pTextureInfo;
}

// PC eSolidNotifyTextureLoading (0x506EF0) keeps every solid list's texture
// fix-up lists in sync as texture packs load and unload. Natively a solid's
// texture entries are resolved lazily (PortSolidTexture), so on unload it is
// enough to forget every entry that points into the unloading pack (or at the
// one texture being unloaded); the next draw re-resolves it. Without this a
// solid kept a dangling TextureInfo after its streamed pack went away.
extern bTList<eSolidListHeader> SolidListHeaderList; // eSolid.cpp

void eSolidNotifyTextureLoading(TexturePack *texture_pack, TextureInfo *texture_info, bool loading) {
    if (loading) {
        return;
    }
    for (eSolidListHeader *header = SolidListHeaderList.GetHead(); header != SolidListHeaderList.EndOfList(); header = header->GetNext()) {
        if (header->SolidIndexEntryTable == nullptr) {
            continue;
        }
        for (int i = 0; i < header->NumSolids; i++) {
            eSolid *solid = header->SolidIndexEntryTable[i].Solid;
            if (solid == nullptr || solid->pTextureTable == nullptr) {
                continue;
            }
            for (int t = 0; t < solid->NumTextureTableEntries; t++) {
                TextureInfo *&entry = solid->pTextureTable[t].pTextureInfo;
                if (entry != nullptr && (entry == texture_info || (texture_pack != nullptr && entry->pTexturePack == texture_pack))) {
                    entry = nullptr;
                }
            }
        }
    }
}

// PC NotifySolidUnloader (0x506B50): before a solid goes away, every model
// using it moves to another loaded solid of the same name when the solid's
// flag 0x100 says one may exist (eFindSolid), else to UnattachedModelList with
// no solid. Natively this was a stub, so streamed-out scenery models kept a
// pointer to the freed solid (crash in DrawSolid reading 0xEE fill).
extern bTList<eModel> UnattachedModelList; // eModel.cpp

void NotifySolidUnloader(eSolid *solid) {
    eSolid *replacement = nullptr;
    if ((solid->Flags & 0x100) != 0) {
        replacement = eFindSolid(solid->NameHash, nullptr);
        if (replacement == solid) {
            replacement = nullptr;
        }
    }
    while (!solid->ModelList.IsEmpty()) {
        eModel *model = solid->ModelList.GetHead();
        model->Remove();
        if (replacement != nullptr) {
            replacement->ModelList.AddTail(model);
        } else {
            UnattachedModelList.AddTail(model);
        }
        model->PortSetSolid(replacement);
    }
}
