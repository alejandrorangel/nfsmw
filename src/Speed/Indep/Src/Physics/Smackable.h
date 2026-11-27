#ifndef PHYSICS_SMACKABLE_H
#define PHYSICS_SMACKABLE_H

#include "Speed/Indep/Src/Interfaces/SimModels/IModel.h"
#include "Speed/Indep/Src/Sim/SimTypes.h"

// total size: 0x5C
struct SmackableParams : public Sim::Param {
    // speed.exe 1.3: 0x620FF0
    static UCrc32 TypeName() {
        static UCrc32 value = "SmackableParams";
        return value;
    }

    // Handed to the "Smackable" simable factory named UCRC32_BASE with itself as
    // the data (ESpawnSmackable 0x63B790, PlaceableScenery::Place 0x6776C0).
    SmackableParams(const UMath::Matrix4 &mat, bool virginspawn, IModel *scenery, bool simple_physics)
        : Sim::Param(UCrc32(0xa6b47fac), this), //
          fMatrix(mat),                        //
          fVirginSpawn(virginspawn),           //
          fScenery(scenery),                   //
          fSimplePhysics(simple_physics) {}

    struct UMath::Matrix4 fMatrix; // offset 0x10, size 0x40
    bool fVirginSpawn;             // offset 0x50, size 0x1
    IModel *fScenery;              // offset 0x54, size 0x4
    bool fSimplePhysics;           // offset 0x58, size 0x1
};

#endif
