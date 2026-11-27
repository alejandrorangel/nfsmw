// Physics::Upgrades, reconstructed from the PC 1.3 build (speed.exe).
//
// A car's performance parts are RefSpec arrays on its pvehicle ("tires",
// "engine", ...): [0] is the stock part and the last entry the fully upgraded
// one (a 3-entry array upgrades from [1]). Installing level L of N blends the
// two part collections at L/N into a new dynamic part collection, and points a
// dynamic copy of the pvehicle at it, recording the level in "<part>_current".
// Junkman parts scale individual attributes of the installed part by the
// factors in the car's junkman collection ("<part>_package") and set a bit in
// "junkman_current".

#include "PhysicsUpgrades.hpp"
#include "PhysicsInfo.hpp"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/Misc/MWAttribUserTypes.h"
#include "Speed/Indep/Tools/AttribSys/Runtime/AttribSys.h"

namespace Physics {
namespace Upgrades {

// total size: 0x1C
struct tPartMap {
    Type type;              // offset 0x0, size 0x4
    const char *name;       // offset 0x4, size 0x4
    Attrib::Key key;        // offset 0x8, size 0x4  pvehicle RefSpec[] of parts
    Attrib::Key countkey;   // offset 0xC, size 0x4  pvehicle <part>_upgrades
    Attrib::Key currentkey; // offset 0x10, size 0x4 pvehicle <part>_current
    Attrib::Key junkkey;    // offset 0x14, size 0x4 junkman <part>_package
    Attrib::Key presetkey;  // offset 0x18, size 0x4 presetride <part>
};

// PC: put_maps, 0x8F87A0
static const tPartMap put_maps[] = {
    {PUT_TIRES, "tires", 0xbd38d1ca, 0x75b0f76b, 0x34e73f83, 0xc5860f58, 0xbd38d1ca},
    {PUT_BRAKES, "brakes", 0x36350867, 0x4e168120, 0x552c3d22, 0x56c63b6f, 0x36350867},
    {PUT_CHASSIS, "chassis", 0xafa210f0, 0xf62a062d, 0x7845f9bc, 0xb6495c9e, 0xafa210f0},
    {PUT_TRANSMISSION, "transmission", 0x07a7a3e5, 0x860e4531, 0x170d5554, 0x25ae629a, 0x07a7a3e5},
    {PUT_ENGINE, "engine", 0xf1f5fbc7, 0x7708db25, 0xb12ccb69, 0x9206efd2, 0xf1f5fbc7},
    {PUT_INDUCTION, "induction", 0xc92a0142, 0x7202e66e, 0x7f440672, 0x7546359e, 0xc92a0142},
    {PUT_NOS, "nos", 0xb1669f64, 0x9dc66e81, 0x8f37beae, 0x452d2634, 0xb1669f64},
    {PUT_MAX, nullptr, 0, 0, 0, 0, 0},
};

static const Attrib::Key kJunkmanCurrent = 0xcdc136e8; // pvehicle junkman_current
static const Attrib::Type kType_float = 0x3c16ec5e;
static const Attrib::Type kType_AxlePair = 0x4cb36381;
static const Attrib::Type kType_int = 0x5763da41;
static const Attrib::Type kType_RefSpec = 0x2b936eb7;
static const Attrib::Type kType_JunkmanMod = 0x51ead18d;

namespace {

// Attribute::GetElementPointer is protected; the part code writes elements.
struct UpgradeAttribute : public Attrib::Attribute {
    void *Element(unsigned int index) const {
        return GetElementPointer(index);
    }
};

inline void *ElementPtr(const Attrib::Attribute &attrib, unsigned int index) {
    return static_cast<const UpgradeAttribute &>(attrib).Element(index);
}

// PC: Attrib::Instance::Add<T> (0x6760E0 for int, 0x6763C0 for RefSpec)
template <typename T> bool AddData(Attrib::Instance &instance, Attrib::Key key, const T *data, unsigned int count) {
    if (!instance.Add(key, count) && !instance.Contains(key)) {
        return false;
    }
    Attrib::Attribute attrib = instance.Get(key);
    for (unsigned int i = 0; i < count; ++i) {
        T *dst = reinterpret_cast<T *>(ElementPtr(attrib, i));
        if (dst != nullptr) {
            *dst = data[i];
        }
    }
    return true;
}

const Attrib::RefSpec &RefSpecAt(const Attrib::Attribute &attrib, unsigned int index) {
    const Attrib::RefSpec *ref = reinterpret_cast<const Attrib::RefSpec *>(ElementPtr(attrib, index));
    if (ref == nullptr) {
        ref = reinterpret_cast<const Attrib::RefSpec *>(Attrib::DefaultDataArea(sizeof(Attrib::RefSpec)));
    }
    return *ref;
}

int JunkmanBits(const Attrib::Gen::pvehicle &vehicle) {
    const int *bits = reinterpret_cast<const int *>(vehicle.GetAttributePointer(kJunkmanCurrent, 0));
    if (bits == nullptr) {
        bits = reinterpret_cast<const int *>(Attrib::DefaultDataArea(sizeof(int)));
    }
    return *bits;
}

int LookupInt(const Attrib::Gen::pvehicle &vehicle, Attrib::Key key) {
    Attrib::Attribute attrib;
    if (!vehicle.Lookup(key, attrib)) {
        return 0;
    }
    const int *value = reinterpret_cast<const int *>(ElementPtr(attrib, 0));
    if (value == nullptr) {
        value = reinterpret_cast<const int *>(Attrib::DefaultDataArea(sizeof(int)));
    }
    return *value;
}

// PC: ScalePart<float> 0x6761A0, <AxlePair> 0x672CA0, <int> 0x676210
template <typename T> void ScalePart(Attrib::Attribute &attrib, unsigned int index, float scale);

template <> void ScalePart<float>(Attrib::Attribute &attrib, unsigned int index, float scale) {
    float start_data = 0.0f;
    const float *src = reinterpret_cast<const float *>(ElementPtr(attrib, index));
    if (src != nullptr) {
        start_data = *src;
    }
    float *dst = reinterpret_cast<float *>(ElementPtr(attrib, index));
    if (dst != nullptr) {
        *dst = start_data * scale;
    }
}

template <> void ScalePart<AxlePair>(Attrib::Attribute &attrib, unsigned int index, float scale) {
    float front = 0.0f;
    float rear = 0.0f;
    const AxlePair *src = reinterpret_cast<const AxlePair *>(ElementPtr(attrib, index));
    if (src != nullptr) {
        front = src->Front;
        rear = src->Rear;
    }
    AxlePair *dst = reinterpret_cast<AxlePair *>(ElementPtr(attrib, index));
    if (dst != nullptr) {
        dst->Front = front * scale;
        dst->Rear = rear * scale;
    }
}

template <> void ScalePart<int>(Attrib::Attribute &attrib, unsigned int index, float scale) {
    int start_data = 0;
    const int *src = reinterpret_cast<const int *>(ElementPtr(attrib, index));
    if (src != nullptr) {
        start_data = *src;
    }
    int *dst = reinterpret_cast<int *>(ElementPtr(attrib, index));
    if (dst != nullptr) {
        *dst = static_cast<int>(static_cast<float>(start_data) * scale);
    }
}

// PC: BlendParts<float> 0x676280, <AxlePair> 0x672BC0, <int> 0x676320
template <typename T>
void BlendParts(const Attrib::Attribute &start, const Attrib::Attribute &end, unsigned int index, float weight, Attrib::Attribute &result);

template <>
void BlendParts<float>(const Attrib::Attribute &start, const Attrib::Attribute &end, unsigned int index, float weight, Attrib::Attribute &result) {
    float a = 0.0f;
    float b = 0.0f;
    const float *pa = reinterpret_cast<const float *>(ElementPtr(start, index));
    if (pa != nullptr) {
        a = *pa;
    }
    const float *pb = reinterpret_cast<const float *>(ElementPtr(end, index));
    if (pb != nullptr) {
        b = *pb;
    }
    float t = 1.0f - weight;
    float *dst = reinterpret_cast<float *>(ElementPtr(result, index));
    if (dst != nullptr) {
        *dst = b * weight + t * a;
    }
}

template <>
void BlendParts<AxlePair>(const Attrib::Attribute &start, const Attrib::Attribute &end, unsigned int index, float weight,
                          Attrib::Attribute &result) {
    AxlePair a;
    AxlePair b;
    a.Front = a.Rear = 0.0f;
    b.Front = b.Rear = 0.0f;
    const AxlePair *pa = reinterpret_cast<const AxlePair *>(ElementPtr(start, index));
    if (pa != nullptr) {
        a = *pa;
    }
    const AxlePair *pb = reinterpret_cast<const AxlePair *>(ElementPtr(end, index));
    if (pb != nullptr) {
        b = *pb;
    }
    float t = 1.0f - weight;
    AxlePair *dst = reinterpret_cast<AxlePair *>(ElementPtr(result, index));
    if (dst != nullptr) {
        dst->Front = b.Front * weight + a.Front * t;
        dst->Rear = b.Rear * weight + t * a.Rear;
    }
}

template <>
void BlendParts<int>(const Attrib::Attribute &start, const Attrib::Attribute &end, unsigned int index, float weight, Attrib::Attribute &result) {
    int a = 0;
    int b = 0;
    const int *pa = reinterpret_cast<const int *>(ElementPtr(start, index));
    if (pa != nullptr) {
        a = *pa;
    }
    const int *pb = reinterpret_cast<const int *>(ElementPtr(end, index));
    if (pb != nullptr) {
        b = *pb;
    }
    int *dst = reinterpret_cast<int *>(ElementPtr(result, index));
    if (dst != nullptr) {
        *dst = static_cast<int>((1.0f - weight) * static_cast<float>(a) + static_cast<float>(b) * weight);
    }
}

bool SameRef(const Attrib::RefSpec &a, const Attrib::RefSpec &b) {
    return a.GetClassKey() == b.GetClassKey() && a.GetCollectionKey() == b.GetCollectionKey();
}

// The part collection for an upgrade: collection0 blended toward collection1
// by weight (PC: PUPartNode::PUPartNode, 0x677CE0).
struct PUPartNode : public Attrib::Instance {
    PUPartNode(const Attrib::RefSpec &collection0, const Attrib::RefSpec &collection1, float weight) : Attrib::Instance(collection0, 0, nullptr) {
        if (weight >= 1.0f) {
            ChangeWithDefault(collection1);
            return;
        }
        if (weight <= 0.0f) {
            return;
        }
        ChangeWithDefault(collection0);
        Modify(GenerateUniqueKey("part_upgrade", false), 0);

        Attrib::Instance end_instance(collection1, 0, nullptr);
        Attrib::Instance start_instance(collection0, 0, nullptr);
        Attrib::AttributeIterator iter = Iterator();
        if (!iter.Valid()) {
            return;
        }
        do {
            Attrib::Key key = iter.GetKey();
            Attrib::Attribute end_attribute = end_instance.Get(key);
            Attrib::Attribute start_attribute = start_instance.Get(key);
            unsigned int count = start_attribute.GetLength();
            if (count < end_attribute.GetLength()) {
                count = end_attribute.GetLength();
            }
            if (start_attribute.GetType() == end_attribute.GetType() && count != 0) {
                Add(key, count);
                Attrib::Attribute new_attrib = Get(key);
                Attrib::Type type = new_attrib.GetType();
                for (unsigned int index = 0; index < count; ++index) {
                    if (type == kType_float) {
                        BlendParts<float>(start_attribute, end_attribute, index, weight, new_attrib);
                    } else if (type == kType_AxlePair) {
                        BlendParts<AxlePair>(start_attribute, end_attribute, index, weight, new_attrib);
                    } else if (type == kType_int) {
                        BlendParts<int>(start_attribute, end_attribute, index, weight, new_attrib);
                    }
                }
            }
        } while (iter.Advance());
    }
};

// The installed part with the junkman modifiers applied
// (PC: PUJunkNode::PUJunkNode, 0x677B20).
struct PUJunkNode : public Attrib::Instance {
    PUJunkNode(const Attrib::RefSpec &collection, const Attrib::Instance &junkman, Attrib::Key junkkey) : Attrib::Instance(collection, 0, nullptr) {
        Attrib::Attribute junk_attribute;
        if (!junkman.Lookup(junkkey, junk_attribute)) {
            return;
        }
        Modify(GenerateUniqueKey("junk_upgrade", false), 0);
        unsigned int junk_count = junk_attribute.GetLength();
        for (unsigned int junk_index = 0; junk_index < junk_count; ++junk_index) {
            const JunkmanMod *modifier = reinterpret_cast<const JunkmanMod *>(ElementPtr(junk_attribute, junk_index));
            if (modifier == nullptr) {
                continue;
            }
            Attrib::Key key = modifier->DefinitionKey;
            float scale = modifier->Scale;
            Attrib::Attribute start_attribute = Get(key);
            unsigned int count = start_attribute.GetLength();
            if (count == 0) {
                continue;
            }
            Add(key, count);
            Attrib::Attribute attribute = Get(key);
            Attrib::Type type = attribute.GetType();
            for (unsigned int index = 0; index < count; ++index) {
                if (type == kType_float) {
                    ScalePart<float>(attribute, index, scale);
                } else if (type == kType_AxlePair) {
                    ScalePart<AxlePair>(attribute, index, scale);
                } else if (type == kType_int) {
                    ScalePart<int>(attribute, index, scale);
                }
            }
        }
    }
};

// Makes vehicle (a copy being edited) dynamic if it isn't yet.
void ModifyVehicle(Attrib::Gen::pvehicle &vehicle) {
    vehicle.Modify(vehicle.GenerateUniqueKey(vehicle.CollectionName(), false), 0);
}

} // namespace

// PC: 0x66B850
static const tPartMap *FindPartMap(Type t) {
    for (const tPartMap *pm = put_maps; pm->key != 0; ++pm) {
        if (pm->type == t) {
            return pm;
        }
    }
    return nullptr;
}

const char *GetPartName(Type t) {
    const tPartMap *pm = FindPartMap(t);
    return pm != nullptr ? pm->name : nullptr;
}

// PC: 0x672D30
int GetLevel(const Attrib::Gen::pvehicle &vehicle, Type type) {
    const tPartMap *t = FindPartMap(type);
    if (t == nullptr) {
        return 0;
    }
    return LookupInt(vehicle, t->currentkey);
}

// PC: 0x672E80
int GetMaxLevel(const Attrib::Gen::pvehicle &vehicle, Type type) {
    const tPartMap *t = FindPartMap(type);
    if (t == nullptr) {
        return 0;
    }
    return LookupInt(vehicle, t->countkey);
}

// PC: 0x677F80
float GetPercent(const Attrib::Gen::pvehicle &vehicle, Type type) {
    int max_level = GetMaxLevel(vehicle, type);
    if (max_level == 0) {
        return 0.0f;
    }
    int cur_level = GetLevel(vehicle, type);
    if (cur_level == max_level) {
        return 1.0f;
    }
    return static_cast<float>(cur_level) / static_cast<float>(max_level);
}

// PC: 0x672E00
void GetPackage(const Attrib::Gen::pvehicle &vehicle, Package &package) {
    package.Default();
    for (int i = 0; i < PUT_MAX; ++i) {
        package.Part[i] = GetLevel(vehicle, static_cast<Type>(i));
        int mask = 1 << i;
        if (JunkmanBits(vehicle) & mask) {
            package.Junkman |= mask;
        }
    }
}

// PC: 0x66B8B0
bool GetJunkman(const Attrib::Gen::pvehicle &vehicle, Type type) {
    return (JunkmanBits(vehicle) & (1 << type)) != 0;
}

// PC: 0x677FE0
bool CanInstallJunkman(const Attrib::Gen::pvehicle &vehicle, Type type) {
    const tPartMap *p = FindPartMap(type);
    if (p == nullptr || p->junkkey == 0) {
        return false;
    }
    if (type == PUT_INDUCTION) {
        if (Physics::Info::InductionType(vehicle) == Physics::Info::INDUCTION_NONE) {
            return false;
        }
    } else if (type == PUT_NOS) {
        if (!Physics::Info::HasNos(vehicle)) {
            return false;
        }
    }
    Attrib::Instance junkman(vehicle.junkman(), 0, nullptr);
    Attrib::Attribute junk_attribute;
    if (!junkman.Lookup(p->junkkey, junk_attribute)) {
        return false;
    }
    if (junk_attribute.GetType() != kType_JunkmanMod) {
        return false;
    }
    return junk_attribute.GetLength() != 0;
}

// Drops the installed part (and its junkman bit) from a dynamic vehicle
// (PC: DowngradeInternal, 0x677A80).
static void DowngradeInternal(Attrib::Gen::pvehicle &vehicle, Type type) {
    if (!vehicle.IsDynamic()) {
        return;
    }
    const tPartMap *t = FindPartMap(type);
    if (t == nullptr) {
        return;
    }
    vehicle.Remove(t->key);
    vehicle.Remove(t->currentkey);
    int mask = 1 << type;
    int junkman_current = JunkmanBits(vehicle);
    if (junkman_current & mask) {
        vehicle.Remove(kJunkmanCurrent);
        int new_bits = junkman_current & ~mask;
        AddData<int>(vehicle, kJunkmanCurrent, &new_bits, 1);
    }
}

// PC: 0x678150
bool SetJunkman(Attrib::Gen::pvehicle &vehicle, Type type) {
    Attrib::Gen::pvehicle newvehicle(vehicle);
    int mask = 1 << type;
    if (JunkmanBits(vehicle) & mask) {
        return true;
    }
    if (!CanInstallJunkman(vehicle, type)) {
        return false;
    }
    const tPartMap *p = FindPartMap(type);
    if (p == nullptr) {
        return false;
    }
    Attrib::Key part_key = p->key;
    Attrib::Key junk_key = p->junkkey;
    if (part_key == 0 || junk_key == 0) {
        return false;
    }

    int junkman_current = newvehicle.junkman_current();
    if (newvehicle.IsDynamic()) {
        newvehicle.Remove(kJunkmanCurrent);
    }

    Attrib::Attribute part_attribute;
    if (!newvehicle.Lookup(part_key, part_attribute) || part_attribute.GetType() != kType_RefSpec) {
        return false;
    }
    Attrib::RefSpec basepart(RefSpecAt(part_attribute, 0));
    Attrib::Instance junkman(newvehicle.junkman(), 0, nullptr);
    PUJunkNode node(basepart, junkman, junk_key);
    if (node.GetConstCollection() == nullptr) {
        return false;
    }

    Attrib::RefSpec newref;
    newref.SetCollection(node.GetConstCollection());
    if (!SameRef(newref, basepart)) {
        if (!newvehicle.IsDynamic()) {
            ModifyVehicle(newvehicle);
        } else {
            newvehicle.Remove(part_key);
        }
        if (!AddData<Attrib::RefSpec>(newvehicle, part_key, &newref, 1)) {
            return false;
        }
    }

    int new_bits = junkman_current | mask;
    if (!AddData<int>(newvehicle, kJunkmanCurrent, &new_bits, 1)) {
        return false;
    }
    vehicle = newvehicle;
    return true;
}

// PC: 0x678530
void RemovePart(Attrib::Gen::pvehicle &vehicle, Type type) {
    if (!vehicle.IsDynamic() || GetLevel(vehicle, type) == 0) {
        return;
    }
    const tPartMap *t = FindPartMap(type);
    if (t == nullptr || t->key == 0) {
        return;
    }
    bool had_junkman = GetJunkman(vehicle, type);
    DowngradeInternal(vehicle, type);
    if (had_junkman) {
        SetJunkman(vehicle, type);
    }
}

// PC: 0x67C220
void RemoveJunkman(Attrib::Gen::pvehicle &vehicle, Type type) {
    if (!vehicle.IsDynamic()) {
        return;
    }
    const tPartMap *t = FindPartMap(type);
    if (t == nullptr || t->key == 0 || !GetJunkman(vehicle, type)) {
        return;
    }
    int old_upgrade_level = GetLevel(vehicle, type);
    DowngradeInternal(vehicle, type);
    if (old_upgrade_level > 0) {
        SetLevel(vehicle, type, old_upgrade_level);
    }
}

// Installs level of a part, the new part collection being the part's base and
// top collections blended by weight (PC: UpgradeInternal, 0x678730).
static bool UpgradeInternal(Attrib::Gen::pvehicle &vehicle, Type type, int level, float weight) {
    Attrib::Gen::pvehicle newvehicle(vehicle);
    if (weight <= 0.0f) {
        RemovePart(vehicle, type);
        return true;
    }
    if (GetMaxLevel(newvehicle, type) <= 0 || weight > 1.0f) {
        return false;
    }
    const tPartMap *p = FindPartMap(type);
    if (p == nullptr) {
        return false;
    }
    Attrib::Key part_key = p->key;
    if (part_key == 0) {
        return false;
    }

    bool had_junkman = GetJunkman(vehicle, type);
    DowngradeInternal(newvehicle, type);

    Attrib::Attribute part_attribute;
    if (!newvehicle.Lookup(part_key, part_attribute) || part_attribute.GetType() != kType_RefSpec || part_attribute.GetLength() < 2) {
        return false;
    }
    unsigned int base_index = 0;
    unsigned int top_index = 1;
    if (part_attribute.GetLength() == 3) {
        base_index = 1;
        top_index = 2;
    }
    Attrib::RefSpec basepart(RefSpecAt(part_attribute, base_index));
    Attrib::RefSpec endpart(RefSpecAt(part_attribute, top_index));
    if (basepart.GetClassKey() == 0 || endpart.GetClassKey() == 0 || basepart.GetCollectionKey() == 0 || endpart.GetCollectionKey() == 0) {
        return false;
    }

    PUPartNode node(basepart, endpart, weight);
    if (node.GetConstCollection() == nullptr) {
        return false;
    }
    Attrib::RefSpec newref;
    newref.SetCollection(node.GetConstCollection());
    if (SameRef(newref, basepart)) {
        return true;
    }
    if (!newvehicle.IsDynamic()) {
        ModifyVehicle(newvehicle);
    }
    if (!AddData<Attrib::RefSpec>(newvehicle, part_key, &newref, 1)) {
        return false;
    }
    if (!AddData<int>(newvehicle, p->currentkey, &level, 1)) {
        return false;
    }
    if (had_junkman && !SetJunkman(newvehicle, type)) {
        return false;
    }
    vehicle = newvehicle;
    return true;
}

// PC: 0x678AD0
bool SetLevel(Attrib::Gen::pvehicle &vehicle, Type type, int level) {
    Attrib::Gen::pvehicle newvehicle(vehicle);
    if (GetLevel(vehicle, type) == level) {
        return true;
    }
    if (level <= 0) {
        RemovePart(vehicle, type);
        return true;
    }
    int max_level = GetMaxLevel(newvehicle, type);
    if (max_level <= 0 || level > max_level) {
        return false;
    }
    if (!UpgradeInternal(newvehicle, type, level, static_cast<float>(level) / static_cast<float>(max_level))) {
        return false;
    }
    vehicle = newvehicle;
    return true;
}

// PC: 0x6785A0
static bool Validate(const Attrib::Gen::pvehicle &vehicle, Type type) {
    if ((JunkmanBits(vehicle) & (1 << type)) && !CanInstallJunkman(vehicle, type)) {
        return false;
    }
    int current = GetLevel(vehicle, type);
    int max_level = GetMaxLevel(vehicle, type);
    if (current == 0 && max_level != 0) {
        const tPartMap *t = FindPartMap(type);
        if (t == nullptr) {
            return false;
        }
        Attrib::Gen::pvehicle stock(vehicle);
        if (stock.IsDynamic()) {
            stock.Unmodify();
        }
        Attrib::Attribute attrib;
        if (!stock.Lookup(t->key, attrib) || attrib.GetLength() < 2) {
            return false;
        }
    }
    return current <= max_level;
}

// PC: 0x67C080
bool ApplyPreset(Attrib::Gen::pvehicle &vehicle, const Attrib::Gen::presetride &presetride) {
    if (!presetride.IsValid() || !vehicle.IsValid()) {
        return false;
    }
    Attrib::Gen::pvehicle newvehicle(vehicle);
    if (newvehicle.IsDynamic()) {
        newvehicle.Unmodify();
    }
    for (int i = 0; i < PUT_MAX; ++i) {
        Type type = static_cast<Type>(i);
        const tPartMap *part = FindPartMap(type);
        if (part == nullptr) {
            continue;
        }
        int max_level = GetMaxLevel(vehicle, type);
        if (max_level <= 0) {
            continue;
        }
        Attrib::Attribute attrib;
        if (presetride.Lookup(part->presetkey, attrib) && attrib.IsValid()) {
            const int *level = reinterpret_cast<const int *>(ElementPtr(attrib, 0));
            if (level != nullptr) {
                if (!SetLevel(newvehicle, type, *level < max_level ? *level : max_level)) {
                    return false;
                }
            }
        }
    }
    vehicle = newvehicle;
    return true;
}

// PC: 0x66B900
void Clear(Attrib::Gen::pvehicle &vehicle) {
    if (vehicle.IsDynamic()) {
        vehicle.Unmodify();
    }
}

// PC: 0x66B910
void Flush() {
    Attrib::Database::Get().CollectGarbage();
}

// PC: 0x67F800
bool SetPackage(Attrib::Gen::pvehicle &vehicle, const Package &package) {
    Attrib::Gen::pvehicle newvehicle(vehicle);
    if (newvehicle.IsDynamic()) {
        newvehicle.Unmodify();
    }
    for (int i = 0; i < PUT_MAX; ++i) {
        if (!Validate(newvehicle, static_cast<Type>(i))) {
            return false;
        }
    }
    for (int i = 0; i < PUT_MAX; ++i) {
        Type type = static_cast<Type>(i);
        if (!SetLevel(newvehicle, type, package.Part[i])) {
            return false;
        }
        if (package.Junkman & (1 << i)) {
            if (!SetJunkman(newvehicle, type)) {
                return false;
            }
        } else {
            RemoveJunkman(newvehicle, type);
        }
    }
    for (int i = 0; i < PUT_MAX; ++i) {
        if (!Validate(newvehicle, static_cast<Type>(i))) {
            return false;
        }
    }
    vehicle = newvehicle;
    return true;
}

// PC: 0x67F950
bool SetMaximum(Attrib::Gen::pvehicle &pvehicle) {
    Package package;
    for (int i = 0; i < PUT_MAX; ++i) {
        package.Part[i] = GetMaxLevel(pvehicle, static_cast<Type>(i));
    }
    return SetPackage(pvehicle, package);
}

// Upgrades the stock car's parts to approach matched_performance
// (PC: MatchPeformance, 0x67C310).
bool MatchPerformance(Attrib::Gen::pvehicle &vehicle, const Physics::Info::Performance &matched_performance) {
    Attrib::Gen::pvehicle newvehicle(vehicle);
    if (newvehicle.IsDynamic()) {
        newvehicle.Unmodify();
    }
    Physics::Info::Performance stock_performance;
    Physics::Info::Performance upgraded_performance;
    if (!Physics::Info::GetStockPerformance(newvehicle, stock_performance) ||
        !Physics::Info::GetMaximumPerformance(newvehicle, upgraded_performance)) {
        return false;
    }
    float accel = UMath::Ramp(matched_performance.Acceleration, stock_performance.Acceleration, upgraded_performance.Acceleration);
    float topspeed = UMath::Ramp(matched_performance.TopSpeed, stock_performance.TopSpeed, upgraded_performance.TopSpeed);
    float handling = UMath::Ramp(matched_performance.Handling, stock_performance.Handling, upgraded_performance.Handling);
    for (int i = 0; i < PUT_MAX; ++i) {
        Type type = static_cast<Type>(i);
        float weight;
        switch (type) {
            case PUT_TIRES:
            case PUT_CHASSIS:
                weight = (handling + topspeed) * 0.5f;
                break;
            case PUT_BRAKES:
                weight = handling;
                break;
            case PUT_TRANSMISSION:
            case PUT_ENGINE:
                weight = (topspeed + accel) * 0.5f;
                break;
            case PUT_INDUCTION:
            case PUT_NOS:
                weight = accel;
                break;
            default:
                weight = (handling + topspeed + accel) * 0.33333334f;
                break;
        }
        if (weight >= 1.0f) {
            weight = 1.0f;
        } else if (!(weight >= 0.0f)) {
            continue;
        }
        if (weight > 0.0f) {
            int max_level = GetMaxLevel(vehicle, type);
            if (max_level > 0) {
                int level = static_cast<int>(UMath::Ceil(static_cast<float>(max_level) * weight));
                if (!UpgradeInternal(newvehicle, type, level, weight)) {
                    return false;
                }
            }
        }
    }
    vehicle = newvehicle;
    return true;
}

} // namespace Upgrades
} // namespace Physics
