#ifndef GMARKER_H__
#define GMARKER_H__

#include "Speed/Indep/Src/Gameplay/GRuntimeInstance.h"

// A position and heading in the world (spawn points, shortcuts...).
// total size: 0x40
class GMarker : public GRuntimeInstance {
  public:
    enum { kType = kGameplayObjType_Marker };

    GMarker(const unsigned int &markerKey);
    ~GMarker() override;

    GameplayObjType GetType() const override {
        return kGameplayObjType_Marker;
    }

    const UMath::Vector3 &GetPosition() const {
        return mPosition;
    }
    const UMath::Vector3 &GetDirection() const {
        return mDirection;
    }

    void CalcTransform(UMath::Matrix4 &mat) const;

  private:
    UMath::Vector3 mPosition;  // offset 0x28, size 0xC
    UMath::Vector3 mDirection; // offset 0x34, size 0xC
};

#endif
