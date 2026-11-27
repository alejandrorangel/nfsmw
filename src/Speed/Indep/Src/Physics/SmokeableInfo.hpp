#ifndef PHYSICS_SMOKEABLEINFO_H
#define PHYSICS_SMOKEABLEINFO_H

#include "Speed/Indep/Libs/Support/Utility/UCrc.h"
#include "Speed/Indep/Libs/Support/Utility/UTypes.h"
#include "Speed/Indep/Tools/AttribSys/Runtime/Common/AttribPrivate.h"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

class SceneryModel;

// A smackable prop placed in a track section (BCHUNK_SPEED_SMOKEABLE_SPAWNER,
// loaded in place). While its section is loaded it owns a SceneryModel that
// stands in for the scenery instance it overrides.
// total size: 0x40
class SmokeableSpawner {
  public:
    static const Attrib::Collection *FindAttributes(UCrc32 name);

    static void Init();

    void EndianSwap();

    void OnUnload();

    const struct ModelHeirarchy *GetRenderHeirarchy() const;

    bHash32 GetRenderMesh() const;

    void ShowInstance() const;

    bool IsInstanceVisible() const;

    void HideInstance() const;

    void OnMoved();

    void OnLoad(unsigned int exclude_flags, bool hidden);

    UCrc32 GetModelName() const {
        return mModel;
    }

    UCrc32 GetCollisionName() const {
        return mCollisionName;
    }

    const UMath::Vector4 &GetOrientation() const {
        return mOrientation;
    }

    const UMath::Vector4 &GetPosition() const {
        return mPosition;
    }

    unsigned int GetUniqueID() const {
        return mUniqueID;
    }

    unsigned int GetExcludeFlags() const {
        return mExcludeFlags;
    }

    SceneryModel *GetSimModel() const {
        return mSimModel;
    }

    void SetSimModel(SceneryModel *model) {
        mSimModel = model;
    }

  private:
    UMath::Quaternion mOrientation;    // offset 0x0, size 0x10
    UMath::Vector4 mPosition;          // offset 0x10, size 0x10
    UCrc32 mModel;                     // offset 0x20, size 0x4
    UCrc32 mCollisionName;             // offset 0x24, size 0x4
    UCrc32 mAttributes;                // offset 0x28, size 0x4
    uint32 mSceneryOverrideInfoNumber; // offset 0x2C, size 0x4
    uint32 mUniqueID;                  // offset 0x30, size 0x4
    uint32 mExcludeFlags;              // offset 0x34, size 0x4
    PORT_PTR32(SceneryModel) mSimModel; // offset 0x38, size 0x4 (loaded in place)
    uint32 pad;                        // offset 0x3C, size 0x4
};

#if defined(EA_PORT_NATIVE)
static_assert(sizeof(SmokeableSpawner) == 0x40, "SmokeableSpawner is loaded in place");
#endif

void ResetPropTimers();

#endif
