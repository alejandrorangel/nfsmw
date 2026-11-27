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

#include <cstdio>

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

eSolidPlatInfo *LoadMesh(bChunk *mesh, const char *solid_name) {
    const char *groups = nullptr;
    int group_bytes = 0;
    const char *indices = nullptr;
    int index_bytes = 0;
    const char *vertices = nullptr;
    int vertex_bytes = 0;
    int num_vertex_buffers = 0;

    for (bChunk *c = mesh->GetFirstChunk(); c < mesh->GetLastChunk(); c = c->GetNext()) {
        switch (c->GetID()) {
            case kMeshGroups:
                groups = SkipFill(c, &group_bytes);
                break;
            case kMeshIndices:
                indices = SkipFill(c, &index_bytes);
                break;
            case kMeshVertices:
                if (num_vertex_buffers++ == 0) {
                    vertices = SkipFill(c, &vertex_bytes);
                }
                break;
            default:
                break;
        }
    }
    if (num_vertex_buffers > 1) {
        Warn(solid_name, "more than one vertex buffer; only the first is used");
    }
    int num_groups = groups ? group_bytes / kDiskGroupSize : 0;
    if (num_groups == 0 || !indices || !vertices) {
        return nullptr;
    }

    eSolidPlatInfo *info = static_cast<eSolidPlatInfo *>(bMalloc(sizeof(eSolidPlatInfo), "eSolidPlatInfo (native)", 0, 0));
    bMemSet(info, 0, sizeof(*info));
    info->Groups = static_cast<eSolidPlatGroup *>(bMalloc(num_groups * sizeof(eSolidPlatGroup), "eSolidPlatGroup (native)", 0, 0));
    info->NumGroups = num_groups;

    uint32 total_vertices = 0;
    uint32 total_indices = 0;
    for (int i = 0; i < num_groups; i++) {
        const char *g = groups + i * kDiskGroupSize;
        eSolidPlatGroup &out = info->Groups[i];
        bMemCpy(out.AABBMin, g + 0x00, 12);
        bMemCpy(out.AABBMax, g + 0x0C, 12);
        bMemCpy(out.TextureSlots, g + 0x18, 6);
        out.Flags = Read<uint32>(g + 0x38);
        out.NumVertices = Read<uint32>(g + 0x3C);
        out.FirstIndex = Read<uint32>(g + 0x44);
        out.NumIndices = Read<uint32>(g + 0x5C);
        total_vertices += out.NumVertices;
        if (out.FirstIndex + out.NumIndices > total_indices) {
            total_indices = out.FirstIndex + out.NumIndices;
        }
    }
    if (total_vertices == 0 || vertex_bytes % total_vertices != 0 || total_indices * 2 > static_cast<uint32>(index_bytes)) {
        Warn(solid_name, "mesh sizes don't add up; skipped");
        bFree(info->Groups);
        bFree(info);
        return nullptr;
    }
    info->NumVertices = total_vertices;
    info->NumIndices = total_indices;
    info->VertexStride = vertex_bytes / total_vertices;
    if (info->VertexStride != 36) {
        Warn(solid_name, "vertex format other than 36 bytes (not drawn yet)");
    }
    info->VertexBuffer = PortMetalCreateBuffer(vertices, vertex_bytes, solid_name);
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
        PortMetalReleaseBuffer(info->VertexBuffer);
        PortMetalReleaseBuffer(info->IndexBuffer);
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
