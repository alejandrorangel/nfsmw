// Native platform record for eSolid (replaces Speed/PC/Src/Ecstasy/eSolidPlat.hpp,
// which is a PS3 Carbon guess).
//
// PC mesh chunks (speed.exe 1.3 mesh loader @ 0x6E3EE0), inside 0x80134100:
//   0x134900  mesh header: +0x10 group count, +0x2C index count
//   0x134B02  groups, 0x68 bytes each (data 16-byte aligned):
//               +0x00 AABB min, +0x0C AABB max
//               +0x18 texture slots (u8 each; slot 0 = diffuse)
//               +0x30 effect index into the PC effect table (speed.exe 1.3
//                     0x8F9BE8, 0x90 per entry): 0 WORLD, 1 WORLDREFLECT,
//                     2 WORLDBONE, 3 WORLDNORMALMAP, 4 CAR, 5 GLOSSYWINDOW,
//                     6 TREE, 7 WORLDMIN, 8 WORLDNOFOG, ... 17 WORLDPRELIT
//               +0x38 flags, +0x3C vertex count, +0x40 triangle count,
//               +0x44 first index, +0x5C index count
//   0x134B03  16-bit triangle-list indices (absolute into the vertex buffer)
//   0x134B01  vertex buffers (128-byte aligned), one or more per mesh. Every
//               format starts position (3 floats), normal (3 floats),
//               D3DCOLOR, uv (2 floats) = 36 bytes; 60-byte vertices add a
//               second uv and a tangent. Groups use the buffers in order: a
//               group whose indices restart at 0 starts the next buffer.
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
    uint16 VertexBuffer; // index into eSolidPlatInfo::VertexBuffers
    uint16 Effect;       // PORT_EFFECT_* (PC effect table index)
    uint32 Flags;
    uint32 NumVertices;
    uint32 FirstIndex;
    uint32 NumIndices;
};

enum {
    PORT_EFFECT_WORLD = 0,
    PORT_EFFECT_WORLDREFLECT = 1,
    PORT_EFFECT_WORLDBONE = 2,
    PORT_EFFECT_WORLDNORMALMAP = 3,
    PORT_EFFECT_CAR = 4,
    PORT_EFFECT_GLOSSYWINDOW = 5,
    PORT_EFFECT_TREE = 6,
    PORT_EFFECT_WORLDMIN = 7,
    PORT_EFFECT_WORLDNOFOG = 8,
    PORT_EFFECT_WORLDPRELIT = 17,
};

class eSolidPlatInfo {
  public:
    void **VertexBuffers;   // retained id<MTLBuffer> each
    uint32 *VertexStrides;
    int NumVertexBuffers;
    void *IndexBuffer; // retained id<MTLBuffer>, 16-bit indices
    uint32 NumIndices;
    int NumGroups;
    eSolidPlatGroup *Groups;
};

#endif
