#ifndef PHYSICSUPGRADES_HPP
#define PHYSICSUPGRADES_HPP

#include "Speed/Indep/Src/Generated/AttribSys/Classes/pvehicle.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/presetride.h"
#include "types.h"

namespace Physics {

namespace Info {
struct Performance;
}; // namespace Info

namespace Upgrades {

enum Type {
    PUT_TIRES = 0,
    PUT_BRAKES = 1,
    PUT_CHASSIS = 2,
    PUT_TRANSMISSION = 3,
    PUT_ENGINE = 4,
    PUT_INDUCTION = 5,
    PUT_NOS = 6,
    PUT_MAX = 7,
};

// total size: 0x20
struct Package {
    int Part[PUT_MAX]; // offset 0x0, size 0x1C
    int32 Junkman;     // offset 0x1C, size 0x4.

    Package() {
        bMemSet(this, 0, sizeof(*this));
        Junkman = 0; // in case bMemSet didn't work I guess?
    }

    void Default() {
        bMemSet(this, 0, sizeof(*this));
        Junkman = 0; // in case bMemSet didn't work I guess?
    }
};

const char *GetPartName(Type t);
int GetMaxLevel(const Attrib::Gen::pvehicle &vehicle, Type type);
int GetLevel(const Attrib::Gen::pvehicle &vehicle, Type type);
float GetPercent(const Attrib::Gen::pvehicle &vehicle, Type type);
bool SetLevel(Attrib::Gen::pvehicle &vehicle, Type type, int level);
void RemovePart(Attrib::Gen::pvehicle &vehicle, Type type);

bool ApplyPreset(Attrib::Gen::pvehicle &vehicle, const Attrib::Gen::presetride &preset);
void Clear(Attrib::Gen::pvehicle &vehicle);
void Flush();
bool SetPackage(Attrib::Gen::pvehicle &vehicle, const Package &package);
void GetPackage(const Attrib::Gen::pvehicle &vehicle, Package &package);
bool SetMaximum(Attrib::Gen::pvehicle &pvehicle);
bool MatchPerformance(Attrib::Gen::pvehicle &vehicle, const Physics::Info::Performance &matched_performance);

bool GetJunkman(const Attrib::Gen::pvehicle &vehicle, Type type);
bool CanInstallJunkman(const Attrib::Gen::pvehicle &pvehicle, Type type);
bool SetJunkman(Attrib::Gen::pvehicle &vehicle, Type type);
void RemoveJunkman(Attrib::Gen::pvehicle &vehicle, Type type);

float GetHeat(Attrib::Gen::pvehicle pvehicle, Type type, int level);

}; // namespace Upgrades

}; // namespace Physics

#endif
