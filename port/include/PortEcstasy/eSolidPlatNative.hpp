// Native platform record for eSolid (replaces Speed/PC/Src/Ecstasy/eSolidPlat.hpp,
// which is a PS3 Carbon guess).
//
// PC mesh chunks (speed.exe 1.3 mesh loader @ 0x6E3EE0), inside 0x80134100:
//   0x134900  mesh header: +0x10 group count, +0x2C index count
//   0x134B02  groups, 0x68 bytes each (data 16-byte aligned):
//               +0x00 AABB min, +0x0C AABB max
//               +0x18 texture slots (u8 each; slot 0 = diffuse)
//               +0x38 flags, +0x3C vertex count, +0x40 triangle count,
//               +0x44 first index, +0x5C index count
//   0x134B03  16-bit triangle-list indices (absolute into the vertex buffer)
//   0x134B01  vertices (data 16-byte aligned), 36 bytes for most meshes:
//               position (3 floats), normal (3 floats), D3DCOLOR, uv (2 floats)
#ifndef PORT_ESOLIDPLATNATIVE_HPP
#define PORT_ESOLIDPLATNATIVE_HPP

#include "Speed/Indep/bWare/Inc/bList.hpp"

#define CURRENT_ESOLID_PLAT_VERSION 18

class eSolid;

extern bTList<eSolid> SolidList;

struct eSolidPlatGroup {
    float AABBMin[3];
    float AABBMax[3];
    uint8 TextureSlots[6];
    uint32 Flags;
    uint32 NumVertices;
    uint32 FirstIndex;
    uint32 NumIndices;
};

class eSolidPlatInfo {
  public:
    void *VertexBuffer; // retained id<MTLBuffer>
    void *IndexBuffer;  // retained id<MTLBuffer>, 16-bit indices
    uint32 VertexStride;
    uint32 NumVertices;
    uint32 NumIndices;
    int NumGroups;
    eSolidPlatGroup *Groups;
};

#endif
