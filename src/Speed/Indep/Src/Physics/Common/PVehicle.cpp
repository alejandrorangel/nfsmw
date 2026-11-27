#include "Speed/Indep/Src/Physics/PVehicle.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/AI/AITarget.h"
#include "Speed/Indep/Src/Camera/CameraAI.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/Database/VehicleDB.hpp"
#include "Speed/Indep/Src/Generated/AttribSys/GenericAccessor.h"
#include "Speed/Indep/Src/Generated/Events/EPerfectLaunch.hpp"
#include "Speed/Indep/Src/Generated/Events/EPlayerAirborne.hpp"
#include "Speed/Indep/Src/Generated/Messages/MJumpCut.h"
#include "Speed/Indep/Src/Interfaces/SimActivities/INIS.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/Simables/IAI.h"
#include "Speed/Indep/Src/Interfaces/Simables/IArticulatedVehicle.h"
#include "Speed/Indep/Src/Interfaces/Simables/IAudible.h"
#include "Speed/Indep/Src/Interfaces/Simables/ICause.h"
#include "Speed/Indep/Src/Interfaces/Simables/ICollisionBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/IDamageable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IEngine.h"
#include "Speed/Indep/Src/Interfaces/Simables/IExplosion.h"
#include "Speed/Indep/Src/Interfaces/Simables/IINput.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRenderable.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRigidBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISuspension.h"
#include "Speed/Indep/Src/Interfaces/Simables/ITransmission.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"
#include "Speed/Indep/Src/Misc/Config.h"
#include "Speed/Indep/Src/Physics/Behavior.h"
#include "Speed/Indep/Src/Physics/Bounds.h"
#include "Speed/Indep/Src/Physics/PhysicsUpgrades.hpp"
#include "Speed/Indep/Src/Physics/VehicleBehaviors.h"
#include "Speed/Indep/Src/Sim/Simulation.h"
#include "Speed/Indep/Src/Sim/Util.h"
#include "Speed/Indep/Src/World/CarInfo.hpp"
#include "Speed/Indep/Src/World/WCollisionMgr.h"
#include "Speed/Indep/Src/World/WWorldPos.h"
#include "Speed/Indep/Src/World/WorldConn.h"

extern Attrib::StringKey BEHAVIOR_MECHANIC_AUDIO;
extern Attrib::StringKey BEHAVIOR_MECHANIC_EFFECTS;
extern Attrib::StringKey BEHAVIOR_MECHANIC_RESET;

PresetCar *FindFEPresetCar(unsigned int key);

namespace Sim {
bool CanSpawnRigidBody(const UMath::Vector3 &position, bool highPriority);
};
unsigned int CarInfo_GetResourcePool(bool needs_compositing);

namespace VehicleClass {

const UCrc32 CAR("CAR");
const UCrc32 SUBMARINE("SUBMARINE");
const UCrc32 CHOPPER("CHOPPER");
const UCrc32 BIKE("BIKE");
const UCrc32 BOAT("BOAT");
const UCrc32 SNOWMOBILE("SNOWMOBILE");
const UCrc32 HOVER("HOVER");
const UCrc32 PLANE("PLANE");
const UCrc32 TANK("TANK");
const UCrc32 TRAILER("TRAILER");
const UCrc32 TRAIN("TRAIN");
const UCrc32 TRANSPORT("TRANSPORT");
const UCrc32 RC("RC");
const UCrc32 TRACTOR("TRACTOR");

}; // namespace VehicleClass

BIND_PHYSICS_FACTORY(PVehicle);

bool PVehicle::mRunDyno = false;
bTList<PVehicle> PVehicle::mInstances;

// Parameters for the AI behaviour; it only carries the base parameter.
// total size: 0x10
struct AIParams : public Sim::Param {
    AIParams() : Sim::Param(UCrc32(UCRC32_BASE), this) {}

    static UCrc32 TypeName() {
        static UCrc32 value = "AIParams";
        return value;
    }
};

// AI behaviour per driver class (and vehicle class, kNull = any).
// total size: 0xC
struct AIBehaviorEntry {
    DriverClass driverclass;
    UCrc32 vehicleclass;
    UCrc32 behavior;
};

// speed.exe 1.3: 0x8F8890, filled by a static initialiser
static const AIBehaviorEntry ai_behaviors[] = {
    {DRIVER_NONE, UCrc32::kNull, UCrc32("AIVehicleEmpty")},
    {DRIVER_NIS, UCrc32::kNull, UCrc32("AIVehicleEmpty")},
    {DRIVER_RACER, UCrc32::kNull, UCrc32("AIVehicleRacecar")},
    {DRIVER_TRAFFIC, UCrc32::kNull, UCrc32("AIVehicleTraffic")},
    {DRIVER_COP, UCrc32("CHOPPER"), UCrc32("AIVehicleHelicopter")},
    {DRIVER_COP, UCrc32::kNull, UCrc32("AIVehicleCopCar")},
    {DRIVER_HUMAN, UCrc32::kNull, UCrc32("AIVehicleHuman")},
    {DRIVER_REMOTE, UCrc32::kNull, UCrc32("AIVehicleHuman")},
    {DRIVER_NONE, UCrc32::kNull, UCrc32::kNull},
};

static const bool Tweak_UseTweakerTunings = false;

#if defined(EA_PORT_NATIVE)
#include <cstdio>
#include <cstdlib>
// NFSMW_TRACE_VEHICLE=1: vehicle spawning and the player car's state each second
static bool TraceVehicle() {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = std::getenv("NFSMW_TRACE_VEHICLE") != nullptr;
    }
    return enabled != 0;
}
#define VEHICLE_TRACE(...)                                                                                                                           \
    do {                                                                                                                                             \
        if (TraceVehicle()) {                                                                                                                        \
            std::fprintf(stderr, "[vehicle] " __VA_ARGS__);                                                                                          \
        }                                                                                                                                            \
    } while (0)
#else
#define VEHICLE_TRACE(...)
#endif

static inline UCrc32 MechanicID(const Attrib::StringKey &mechanic) {
    return UCrc32(mechanic.GetHash32());
}

// Cache management: one node per live vehicle when MakeRoom has to evict.
// total size: 0x18
struct ManageNode {
    ManageNode(PVehicle *v, const PVehicle::Resource &r) : vehicle(v), resource(r), result(VCR_DONTCARE), instancecount(0) {}

    // speed.exe 1.3: 0x668EB0
    static bool sort_remove_resources(const ManageNode &a, const ManageNode &b) {
        if (b.resource.Type != a.resource.Type && (a.instancecount < b.instancecount || b.resource.Cost < a.resource.Cost)) {
            return true;
        }
        return a.resource.Type < b.resource.Type;
    }

    // speed.exe 1.3: 0x668EF0
    static bool sort_remove_instances(const ManageNode &a, const ManageNode &b) {
        if (b.resource.Type != a.resource.Type && (b.instancecount < a.instancecount || b.resource.Cost < a.resource.Cost)) {
            return true;
        }
        return a.resource.Type < b.resource.Type;
    }

    // speed.exe 1.3: 0x668F30: by type, wanted ones first
    static bool sort_by_keep(const ManageNode &a, const ManageNode &b) {
        if (b.resource.Type == a.resource.Type) {
            return a.result == VCR_WANT && b.result == VCR_DONTCARE;
        }
        return a.resource.Type < b.resource.Type;
    }

    // speed.exe 1.3: 0x668F70
    static bool is_kept(const ManageNode &n) {
        return n.result == VCR_WANT;
    }

    PVehicle *vehicle;                // offset 0x0, size 0x4
    PVehicle::Resource resource;      // offset 0x4, size 0xC
    eVehicleCacheResult result;       // offset 0x10, size 0x4
    unsigned int instancecount;       // offset 0x14, size 0x4
};

// Cop and traffic models share their car loader resources between instances
static inline bool IsSharedResource(CarType type) {
    CarUsageType usage = GetCarTypeInfo(type)->GetCarUsageType();
    return usage == CAR_USAGE_TYPE_COP || usage == CAR_USAGE_TYPE_TRAFFIC;
}

// speed.exe 1.3: 0x669080
PVehicle::Resource::Resource(const Attrib::Gen::pvehicle &pvehicle, bool spool, bool is_player) : Type(CARTYPE_NONE), Cost(0), Flags(0) {
    const char *model = pvehicle.MODEL().GetString();
    Type = CarPartDB.GetCarType(bStringHash(model));
    if (Type != CARTYPE_NONE && Type < NUM_CARTYPES) {
        if (CarInfo_IsSkinned(Type)) {
            Flags |= COMPOSITED;
        }
        Cost = CarInfo_GetResourceCost(Type, is_player, Sim::GetUserMode() == Sim::USER_SPLIT_SCREEN);
        if (spool) {
            Flags |= SPOOLED;
        }
        if (Cost != 0) {
            Flags |= VALID;
        }
    }
}

// speed.exe 1.3: 0x6701A0: frees vehicles that were killed
void PVehicle::CleanResources() {
    PVehicle *vehicle = mInstances.GetHead();
    while (vehicle != mInstances.EndOfList()) {
        PVehicle *next = vehicle->GetNext();
        if (vehicle->IsDirty()) {
            delete vehicle;
        }
        vehicle = next;
    }
}

// speed.exe 1.3: 0x683C80
unsigned int PVehicle::CountResources() {
    UTL::Std::list<CarType, _type_list> types;
    unsigned int count = 0;
    for (PVehicle *vehicle = mInstances.GetHead(); vehicle != mInstances.EndOfList(); vehicle = vehicle->GetNext()) {
        CarType type = vehicle->mResources.Type;
        bool found = std::find(types.begin(), types.end(), type) != types.end();
        if (!found || !IsSharedResource(type)) {
            types.push_back(type);
            count += vehicle->mResources.Cost;
        }
    }
    return count;
}

// speed.exe 1.3: 0x687710
// Makes car loader memory and vehicle slots for <resources>, evicting
// vehicles no vehicle cache wants if it has to.
bool PVehicle::MakeRoom(IVehicleCache *whosasking, const ResourceList &resources) {
    CleanResources();

    unsigned int newcount = resources.size();
    bool needs_compositing = false;
    unsigned int newcost = 0;
    for (ResourceList::const_iterator iter = resources.begin(); iter != resources.end(); ++iter) {
        if (iter->NeedsCompositing()) {
            needs_compositing = true;
        }
        unsigned int cost = iter->Cost;
        for (PVehicle *vehicle = mInstances.GetHead(); vehicle != mInstances.EndOfList(); vehicle = vehicle->GetNext()) {
            if (vehicle->mResources.Type == iter->Type) {
                if (IsSharedResource(iter->Type)) {
                    cost = 0;
                }
                break;
            }
        }
        newcost += cost;
    }

    unsigned int available = CarInfo_GetResourcePool(needs_compositing);
    unsigned int used = CountResources();
    unsigned int need_resources = 0;
    unsigned int need_instances = 0;
    if (used + newcost > available) {
        need_resources = used + newcost - available;
    }
    if (newcount + IVehicle::Count(VEHICLE_ALL) > 20) {
        need_instances = newcount + IVehicle::Count(VEHICLE_ALL) - 20;
    }
    if (need_resources == 0 && need_instances == 0) {
        return true;
    }
    if (!whosasking) {
        return false;
    }

    std::vector<ManageNode> list;
    list.reserve(20);
    for (PVehicle *vehicle = mInstances.GetHead(); vehicle != mInstances.EndOfList(); vehicle = vehicle->GetNext()) {
        list.push_back(ManageNode(vehicle, vehicle->mResources));
    }

    // whoever asks gets the first say, then every other cache can veto
    for (std::vector<ManageNode>::iterator node = list.begin(); node != list.end(); ++node) {
        if (node->result != VCR_WANT) {
            node->result = whosasking->OnQueryVehicleCache(node->vehicle, whosasking);
        }
    }
    const IVehicleCache::List &caches = IVehicleCache::GetList();
    for (std::vector<ManageNode>::iterator node = list.begin(); node != list.end(); ++node) {
        if (node->result == VCR_WANT) {
            continue;
        }
        for (IVehicleCache::List::const_iterator cache = caches.begin(); cache != caches.end(); ++cache) {
            if (!*cache || !UTL::COM::ComparePtr(*cache, whosasking)) {
                if ((*cache)->OnQueryVehicleCache(node->vehicle, whosasking) == VCR_WANT) {
                    node->result = VCR_WANT;
                    break;
                }
            }
        }
    }
    std::sort(list.begin(), list.end(), ManageNode::sort_by_keep);

    // never evict a model we're about to spawn
    for (ResourceList::const_iterator iter = resources.begin(); iter != resources.end(); ++iter) {
        for (std::vector<ManageNode>::iterator node = list.begin(); node != list.end(); ++node) {
            if (node->resource.Type == iter->Type) {
                node->result = VCR_WANT;
                break;
            }
        }
    }

    // freeing memory needs every instance of a model gone, so one wanted
    // instance keeps them all
    if (need_resources) {
        CarType type = CARTYPE_NONE;
        eVehicleCacheResult keep = VCR_DONTCARE;
        for (std::vector<ManageNode>::iterator node = list.begin(); node != list.end(); ++node) {
            if (node->resource.Type != type) {
                keep = node->result;
                type = node->resource.Type;
            }
            if (keep == VCR_WANT) {
                node->result = VCR_WANT;
            }
        }
    }

    list.erase(std::remove_if(list.begin(), list.end(), ManageNode::is_kept), list.end());
    if (list.empty()) {
        return false;
    }

    std::map<CarType, unsigned int> instances;
    for (std::vector<ManageNode>::iterator node = list.begin(); node != list.end(); ++node) {
        instances[node->resource.Type]++;
    }
    for (std::vector<ManageNode>::iterator node = list.begin(); node != list.end(); ++node) {
        node->instancecount = instances[node->resource.Type];
    }

    std::vector<ManageNode>::iterator remove_end = list.begin();
    unsigned int removed = 0;
    if (need_resources) {
        std::sort(list.begin(), list.end(), ManageNode::sort_remove_resources);
        unsigned int freed = 0;
        unsigned int pending = 0;
        CarType type = CARTYPE_NONE;
        for (; remove_end != list.end(); ++remove_end) {
            if (remove_end->resource.Type != type) {
                freed += pending;
                type = remove_end->resource.Type;
            }
            if (freed >= need_resources) {
                pending = 0;
                break;
            }
            pending = remove_end->resource.Cost;
            removed++;
        }
        if (freed + pending < need_resources) {
            return false;
        }
    }
    if (removed < need_instances) {
        std::sort(remove_end, list.end(), ManageNode::sort_remove_instances);
        for (; remove_end != list.end(); ++remove_end) {
            if (removed >= need_instances) {
                break;
            }
            removed++;
        }
        if (removed < need_instances) {
            return false;
        }
    }
    list.erase(remove_end, list.end());

    for (std::vector<ManageNode>::iterator node = list.begin(); node != list.end(); ++node) {
        PVehicle *vehicle = node->vehicle;
        for (IVehicleCache::List::const_iterator cache = caches.begin(); cache != caches.end(); ++cache) {
            (*cache)->OnRemovedVehicleCache(vehicle);
        }
        if (vehicle) {
            delete vehicle;
        }
    }
    return true;
}

// speed.exe 1.3: 0x689820
ISimable *PVehicle::Construct(Sim::Param params) {
    const VehicleParams &vp = params.Fetch<VehicleParams>(UCrc32(UCRC32_BASE));
    const UMath::Vector3 &initialVec = vp.initialVec;
    const UMath::Vector3 &initialPos = vp.initialPos;
    DriverClass carClass = vp.carClass;
    const FECustomizationRecord *customization = vp.customization;
    IVehicleCache *cache = vp.VehicleCache;
    const Physics::Info::Performance *matched = vp.matched;
    unsigned int flags = vp.Flags;

    Attrib::Gen::pvehicle attributes(vp.carType, 0, nullptr);
    VEHICLE_TRACE("Construct key %#x class %d flags %#x\n", vp.carType, carClass, flags);
    if (!attributes.IsValid()) {
        VEHICLE_TRACE("  no pvehicle attributes\n");
        return nullptr;
    }

    if (!customization) {
        const char *preset_name = attributes.DefaultPresetRide();
        if (preset_name) {
            PresetCar *preset = FindFEPresetCar(bStringHashUpper(preset_name));
            if (preset) {
                static FECustomizationRecord temp_record;
                temp_record.Default();
                temp_record.BecomePreset(preset);
                customization = &temp_record;
            }
        }
    }
    if (customization && !customization->WriteRecordIntoPhysics(attributes)) {
        VEHICLE_TRACE("  WriteRecordIntoPhysics failed\n");
        return nullptr;
    }

    if (matched && !Physics::Upgrades::MatchPerformance(attributes, *matched)) {
        return nullptr;
    }
    if (flags & 4) {
        Physics::Upgrades::RemoveJunkman(attributes, Physics::Upgrades::PUT_NOS);
        Physics::Upgrades::RemovePart(attributes, Physics::Upgrades::PUT_NOS);
    }
    if (GetMikeMannBuild()) {
        Physics::Upgrades::SetLevel(attributes, Physics::Upgrades::PUT_NOS, Physics::Upgrades::GetMaxLevel(attributes, Physics::Upgrades::PUT_NOS));
    }
    if ((flags & 0x10) && Physics::Upgrades::GetLevel(attributes, Physics::Upgrades::PUT_NOS) == 0 &&
        Physics::Upgrades::GetMaxLevel(attributes, Physics::Upgrades::PUT_NOS) > 0) {
        Physics::Upgrades::SetLevel(attributes, Physics::Upgrades::PUT_NOS, 1);
    }

    const CollisionGeometry::Collection *geometry = CollisionGeometry::Lookup(UCrc32(attributes.MODEL().GetHash32()));
    if (!geometry || !geometry->GetRoot()) {
        VEHICLE_TRACE("  no collision geometry for %s\n", attributes.MODEL().GetString());
        return nullptr;
    }

    Resource resource(attributes, (flags & 1) != 0, carClass == DRIVER_HUMAN);
    if (!resource.IsValid()) {
        VEHICLE_TRACE("  invalid resource (type %d cost %u)\n", resource.Type, resource.Cost);
        return nullptr;
    }

    // a tractor spawns its trailer too, so both have to fit
    ResourceList resources;
    resources.push_back(resource);
    const Attrib::Gen::GenericAccessor &generic = reinterpret_cast<const Attrib::Gen::GenericAccessor &>(attributes);
    Attrib::RefSpec trailer;
    if (generic.Trailer(trailer, 0) && trailer.GetCollectionKey() != 0) {
        Attrib::Gen::pvehicle trailer_attributes(trailer, 0, nullptr);
        resources.push_back(Resource(trailer_attributes, (flags & 1) != 0, false));
    }

    if (!MakeRoom(cache, resources)) {
        VEHICLE_TRACE("  MakeRoom failed (pool %u used %u)\n", CarInfo_GetResourcePool(false), CountResources());
        return nullptr;
    }

    Physics::Info::Performance performance;
    const Physics::Info::Performance *pperformance = matched;
    if (!matched && (flags & 8) && Physics::Info::EstimatePerformance(attributes, performance)) {
        pperformance = &performance;
    }

    if (!Sim::CanSpawnRigidBody(initialPos, true)) {
        VEHICLE_TRACE("  CanSpawnRigidBody failed\n");
        return nullptr;
    }

    PVehicle *vehicle = new PVehicle(carClass, attributes, initialVec, initialPos, geometry->GetRoot(), customization, resource, pperformance,
                                     cache ? cache->GetCacheName() : nullptr);
    if (flags & 2) {
        bool grounded = vehicle->SetVehicleOnGround(initialPos, initialVec);
        VEHICLE_TRACE("  SetVehicleOnGround -> %d\n", grounded);
    }
    VEHICLE_TRACE("  created %s (%s) at %.2f %.2f %.2f\n", vehicle->GetVehicleName(), attributes.MODEL().GetString(), initialPos.x,
                  initialPos.y, initialPos.z);
    return vehicle;
}

// speed.exe 1.3: 0x689020
PVehicle::PVehicle(DriverClass carClass, const Attrib::Gen::pvehicle &attributes, const UMath::Vector3 &initialVec, const UMath::Vector3 &initialPos,
                   const CollisionGeometry::Bounds *bounds, const FECustomizationRecord *customization, const Resource &resource,
                   const Physics::Info::Performance *performance, const char *cache_name)
    : PhysicsObject(attributes, SIMABLE_VEHICLE, 0, 4), //
      PVehicleIVehicle(this),                           //
      IContext(this),                                   //
      IExplodeable(this),                               //
      mAttributes(attributes),                          //
      mCustomization(nullptr),                          //
      mInput(nullptr),                                  //
      mCollisionBody(nullptr),                          //
      mSuspension(nullptr),                             //
      mEngine(nullptr),                                 //
      mDamage(nullptr),                                 //
      mTranny(nullptr),                                 //
      mAI(nullptr),                                     //
      mArticulation(nullptr),                           //
      mRenderable(nullptr),                             //
      mAudible(nullptr),                                //
      mSequencer(nullptr),                              //
      mTaskFX(nullptr),                                 //
      mClass(UCrc32::kNull),                            //
      mSpeed(0.0f),                                     //
      mAbsSpeed(0.0f),                                  //
      mSpeedometer(0.0f),                               //
      mTimeInAir(0.0f),                                 //
      mSlipAngle(0.0f),                                 //
      mWheelsOnGround(0),                               //
      mLocalVel(UMath::Vector3::kZero),                 //
      mDriverClass(carClass),                           //
      mDriverStyle(STYLE_RACING),                       //
      mGlareState(0),                                   //
      mStartingNOS(1.0f),                               //
      mBrakeTime(0.0f),                                 //
      mForceStop(0),                                    //
      mPhysicsMode(PHYSICS_MODE_SIMULATED),             //
      mAnimating(false),                                //
      mStaging(false),                                  //
      mOverrideDirty(false),                            //
      mBounds(bounds),                                  //
      mIsModeling(true),                                //
      mOffScreenTime(0.0f),                             //
      mOnScreenTime(0.0f),                              //
      mOffWorld(false),                                 //
      mHasDyno(false),                                  //
      mResources(resource),                             //
      mPerformanceValid(false),                         //
      mCacheName(cache_name) {
    if (performance) {
        mPerformance = *performance;
        mPerformanceValid = true;
    }
    mInstances.AddTail(this);
    AITarget::Register(this);

    if (customization) {
        mCustomization = static_cast<FECustomizationRecord *>(gFastMem.Alloc(sizeof(FECustomizationRecord), nullptr));
        bMemCpy(mCustomization, customization, sizeof(FECustomizationRecord));
    }
    mClass = UCrc32(mAttributes.CLASS().GetHash32());

#if defined(EA_PORT_NATIVE)
    // IVehicle's (inlined) constructor puts every vehicle in VEHICLE_ALL
    AddToList(VEHICLE_ALL);
#endif
    UpdateListing();

    float fxrate;
    if (mDriverClass == DRIVER_HUMAN) {
        fxrate = 1.0f;
    } else if (mDriverClass == DRIVER_TRAFFIC) {
        fxrate = 0.25f;
    } else {
        fxrate = 0.5f;
    }
    mTaskFX = AddTask(UCrc32("FX"), fxrate, 0.0f, Sim::TASK_FRAME_FIXED);

    Reset();
    if (mDamage) {
        mDamage->ResetDamage();
    }
    mGlareState = 0;

    UMath::Matrix4 initMat = Util_GenerateMatrix(initialVec, nullptr);
    LoadBehaviors(initialPos, initMat);

    // vehicles own themselves
    SetOwnerObject(this);

    const char *sequencer = mAttributes.EventSequencer().GetString();
    if (sequencer && *sequencer) {
        mSequencer = EventSequencer::Create(this, this, UCrc32(sequencer), Sim::GetTime(), 1.0f);
    }
    OnBeginMode(mPhysicsMode);
}

// speed.exe 1.3: 0x687E40
PVehicle::~PVehicle() {
    DetachEntity();
    DestroyAttachments();
    AITarget::UnRegister(this);
    if (mCustomization) {
        gFastMem.Free(mCustomization, sizeof(FECustomizationRecord), nullptr);
        mCustomization = nullptr;
    }
    ReleaseBehaviors();
    if (mTaskFX) {
        RemoveTask(mTaskFX);
        mTaskFX = nullptr;
    }
    if (mSequencer) {
        mSequencer->Release();
        mSequencer = nullptr;
    }
    Remove();
    CameraAI::RemoveAvoidable(this);
#if defined(EA_PORT_NATIVE)
    IVehicle::UnList();
#endif
}

// speed.exe 1.3: 0x67ECA0
void PVehicle::UpdateListing() {
    for (unsigned int i = VEHICLE_PLAYERS; i < VEHICLE_MAX; i++) {
        UnList(static_cast<eVehicleList>(i));
    }
    switch (mDriverClass) {
        case DRIVER_HUMAN:
            AddToList(VEHICLE_PLAYERS);
            AddToList(VEHICLE_RACERS);
            break;
        case DRIVER_TRAFFIC:
            AddToList(VEHICLE_AI);
            AddToList(VEHICLE_AITRAFFIC);
            break;
        case DRIVER_COP:
            AddToList(VEHICLE_AI);
            AddToList(VEHICLE_AICOPS);
            break;
        case DRIVER_RACER:
            AddToList(VEHICLE_AI);
            AddToList(VEHICLE_AIRACERS);
            AddToList(VEHICLE_RACERS);
            break;
        case DRIVER_REMOTE:
            AddToList(VEHICLE_PLAYERS);
            AddToList(VEHICLE_RACERS);
            AddToList(VEHICLE_REMOTE);
            break;
        default:
            break;
    }
    if (!IsActive()) {
        AddToList(VEHICLE_INACTIVE);
    }
    if (mClass == VehicleClass::TRAILER) {
        AddToList(VEHICLE_TRAILERS);
    }
}

// speed.exe 1.3: 0x67F4C0
void PVehicle::Reset() {
    PhysicsObject::Reset();
    mTimeInAir = 0.0f;
    mWheelsOnGround = 0;
    mBrakeTime = 0.0f;
}

// speed.exe 1.3: 0x6851D0
void PVehicle::Kill() {
    ReleaseGC();
    PhysicsObject::Kill();
    ReleaseBehavior(MechanicID(BEHAVIOR_MECHANIC_DRAW));
    ReleaseBehavior(MechanicID(BEHAVIOR_MECHANIC_AUDIO));
    mResources.Invalidate();
}

// speed.exe 1.3: 0x670620
void PVehicle::DebugObject() {
    GetRigidBody()->Debug();
}

// speed.exe 1.3: 0x6882D0
IModel *PVehicle::GetModel() {
    if (mRenderable) {
        return mRenderable->GetModel();
    }
    return nullptr;
}

// speed.exe 1.3: 0x6882B0
const IModel *PVehicle::GetModel() const {
    if (mRenderable) {
        return static_cast<const IRenderable *>(mRenderable)->GetModel();
    }
    return nullptr;
}

// speed.exe 1.3: 0x67EE20
bool PVehicle::OnTask(HSIMTASK htask, float dT) {
    if (htask == mTaskFX) {
        OnTaskFX(dT);
        return true;
    }
    return PhysicsObject::OnTask(htask, dT);
}

// speed.exe 1.3: 0x67EED0
void PVehicle::OnBehaviorChange(const UCrc32 &mechanic) {
    PhysicsObject::OnBehaviorChange(mechanic);

    if (mechanic == MechanicID(BEHAVIOR_MECHANIC_AI)) {
        ISimable::QueryInterface(&mAI);
    } else if (mechanic == MechanicID(BEHAVIOR_MECHANIC_INPUT)) {
        ISimable::QueryInterface(&mInput);
    } else if (mechanic == MechanicID(BEHAVIOR_MECHANIC_RIGIDBODY)) {
        ISimable::QueryInterface(&mCollisionBody);
        if (mCollisionBody) {
            mCollisionBody->SetAnimating(mAnimating);
        }
        ISimable::QueryInterface(&mArticulation);
    } else if (mechanic == MechanicID(BEHAVIOR_MECHANIC_DRAW)) {
        ISimable::QueryInterface(&mRenderable);
    } else if (mechanic == MechanicID(BEHAVIOR_MECHANIC_AUDIO)) {
        ISimable::QueryInterface(&mAudible);
    } else if (mechanic == MechanicID(BEHAVIOR_MECHANIC_SUSPENSION)) {
        if (ISimable::QueryInterface(&mSuspension) && mCollisionBody) {
            const IRigidBody *irb = GetRigidBody();
            mSuspension->MatchSpeed(UMath::Dot(mCollisionBody->GetForwardVector(), irb->GetLinearVelocity()));
        }
    } else if (mechanic == MechanicID(BEHAVIOR_MECHANIC_ENGINE)) {
        ISimable::QueryInterface(&mTranny);
        if (ISimable::QueryInterface(&mEngine)) {
            mEngine->ChargeNOS(mStartingNOS - mEngine->GetNOSCapacity());
            if (mCollisionBody) {
                const IRigidBody *irb = GetRigidBody();
                mEngine->MatchSpeed(UMath::Dot(mCollisionBody->GetForwardVector(), irb->GetLinearVelocity()));
            }
        }
    } else if (mechanic == MechanicID(BEHAVIOR_MECHANIC_DAMAGE)) {
        ISimable::QueryInterface(&mDamage);
    }
}

// speed.exe 1.3: 0x67F0D0
UCrc32 PVehicle::LookupBehaviorSignature(const Attrib::StringKey &mechanic) const {
    if (mechanic == BEHAVIOR_MECHANIC_AUDIO && !IsSoundEnabled) {
        return UCrc32::kNull;
    }

    BehaviorOverrides::const_iterator iter = mBehaviorOverrides.find(MechanicID(mechanic));
    if (iter != mBehaviorOverrides.end()) {
        return iter->second;
    }

    if (mAnimating) {
        if (mClass != VehicleClass::CHOPPER) {
            if (mechanic == BEHAVIOR_MECHANIC_SUSPENSION) {
                return UCrc32("SuspensionSpline");
            }
            if (mechanic == BEHAVIOR_MECHANIC_ENGINE) {
                return UCrc32("EngineSpline");
            }
        }
        if (mechanic == BEHAVIOR_MECHANIC_RESET) {
            return UCrc32::kNull;
        }
        if (mechanic == BEHAVIOR_MECHANIC_INPUT) {
            return UCrc32("InputNIS");
        }
    }

    if (mechanic == BEHAVIOR_MECHANIC_RIGIDBODY && mDriverClass == DRIVER_REMOTE) {
        return UCrc32("RBRemote");
    }
    if (mechanic == BEHAVIOR_MECHANIC_INPUT && mDriverClass == DRIVER_HUMAN) {
        if (mDriverStyle == STYLE_DRAG) {
            return UCrc32("InputPlayerDrag");
        }
        return UCrc32("InputPlayer");
    }
    if (mechanic == BEHAVIOR_MECHANIC_DAMAGE && mDriverStyle == STYLE_DRAG) {
        return UCrc32("DamageDragster");
    }
    if (mechanic == BEHAVIOR_MECHANIC_ENGINE && mClass == VehicleClass::CAR && mDriverStyle == STYLE_DRAG) {
        return UCrc32("EngineDragster");
    }
    if (mechanic == BEHAVIOR_MECHANIC_SUSPENSION && mClass == VehicleClass::CAR && mDriverClass > DRIVER_COP &&
        (mDriverClass < DRIVER_NIS || mDriverClass == DRIVER_REMOTE)) {
        return UCrc32("SuspensionSimple");
    }
    if (mechanic == BEHAVIOR_MECHANIC_AI) {
        const AIBehaviorEntry *entry = ai_behaviors;
        if (entry->behavior != UCrc32::kNull) {
            for (; mDriverClass != entry->driverclass || (mClass != entry->vehicleclass && entry->vehicleclass != UCrc32::kNull); entry++) {
                if (entry[1].behavior == UCrc32::kNull) {
                    return ai_behaviors[0].behavior;
                }
            }
        }
        return entry->behavior;
    }
    if (mechanic == BEHAVIOR_MECHANIC_EFFECTS && (mDriverClass == DRIVER_HUMAN || IsPlayer())) {
        return UCrc32("EffectsPlayer");
    }

    // the vehicle's attributes name the behaviour for every other mechanic
    Attrib::Attribute attrib = mAttributes.Get(mechanic.GetHash32());
    Attrib::StringKey signature;
    if (attrib.Get(0, signature)) {
        return UCrc32(signature.GetHash32());
    }
    return UCrc32::kNull;
}

// speed.exe 1.3: 0x685240
void PVehicle::LoadBehaviors(const UMath::Vector3 &initialPos, const UMath::Matrix4 &initMat) {
    if (IsDirty()) {
        return;
    }
    if (mEngine) {
        mStartingNOS = mEngine->GetNOSCapacity();
    }

    UMath::Vector3 dimension;
    mBounds->GetHalfDimensions(dimension);
    unsigned int collision_mask;
    switch (mDriverClass) {
        case DRIVER_HUMAN:
        case DRIVER_RACER:
        case DRIVER_REMOTE:
            collision_mask = 0;
            break;
        case DRIVER_COP:
            collision_mask = 0x80;
            break;
        default:
            collision_mask = 0x40;
            break;
    }
    float mass = mAttributes.MASS();
    UMath::Vector3 moment = Util_GenerateCarTensor(mass, dimension.x, dimension.y, dimension.z, UMath::Vector4To3(mAttributes.TENSOR_SCALE()));

    {
        RBComplexParams rbp(initialPos, UMath::Vector3::kZero, UMath::Vector3::kZero, initMat, mass, moment, dimension, true, mBounds,
                            collision_mask);
        LoadBehavior(MechanicID(BEHAVIOR_MECHANIC_RIGIDBODY), LookupBehaviorSignature(BEHAVIOR_MECHANIC_RIGIDBODY), rbp);
    }
    LoadBehavior(MechanicID(BEHAVIOR_MECHANIC_INPUT), LookupBehaviorSignature(BEHAVIOR_MECHANIC_INPUT), Sim::Param());
    {
        EngineParams ep;
        LoadBehavior(MechanicID(BEHAVIOR_MECHANIC_ENGINE), LookupBehaviorSignature(BEHAVIOR_MECHANIC_ENGINE), ep);
    }
    {
        SuspensionParams sp;
        LoadBehavior(MechanicID(BEHAVIOR_MECHANIC_SUSPENSION), LookupBehaviorSignature(BEHAVIOR_MECHANIC_SUSPENSION), sp);
    }
    {
        DamageParams dp;
        LoadBehavior(MechanicID(BEHAVIOR_MECHANIC_DAMAGE), LookupBehaviorSignature(BEHAVIOR_MECHANIC_DAMAGE), dp);
    }
    LoadBehavior(MechanicID(BEHAVIOR_MECHANIC_DRAW), LookupBehaviorSignature(BEHAVIOR_MECHANIC_DRAW), Sim::Param());
    LoadBehavior(MechanicID(BEHAVIOR_MECHANIC_AUDIO), LookupBehaviorSignature(BEHAVIOR_MECHANIC_AUDIO), Sim::Param());
    {
        AIParams ap;
        LoadBehavior(MechanicID(BEHAVIOR_MECHANIC_AI), LookupBehaviorSignature(BEHAVIOR_MECHANIC_AI), ap);
    }
    LoadBehavior(MechanicID(BEHAVIOR_MECHANIC_EFFECTS), LookupBehaviorSignature(BEHAVIOR_MECHANIC_EFFECTS), Sim::Param());
    LoadBehavior(MechanicID(BEHAVIOR_MECHANIC_RESET), LookupBehaviorSignature(BEHAVIOR_MECHANIC_RESET), Sim::Param());

    ResetBehavior(MechanicID(BEHAVIOR_MECHANIC_RIGIDBODY));
    ResetBehavior(MechanicID(BEHAVIOR_MECHANIC_INPUT));
    ResetBehavior(MechanicID(BEHAVIOR_MECHANIC_SUSPENSION));
    ResetBehavior(MechanicID(BEHAVIOR_MECHANIC_ENGINE));
    ResetBehavior(MechanicID(BEHAVIOR_MECHANIC_DAMAGE));
    ResetBehavior(MechanicID(BEHAVIOR_MECHANIC_EFFECTS));
}

// speed.exe 1.3: 0x6863B0
void PVehicle::ReloadBehaviors() {
    IRigidBody *irb = GetRigidBody();
    UMath::Vector3 position = irb->GetPosition();
    UMath::Matrix4 mat;
    irb->GetMatrix4(mat);
    LoadBehaviors(position, mat);
}

// speed.exe 1.3: 0x6857A0
void PVehicle::SetBehaviorOverride(UCrc32 mechanic, UCrc32 behavior) {
    Mechanics::iterator iter;
    Behavior *current = FindBehavior(mechanic);
    if (current && current->GetSignature() == behavior) {
        return;
    }
    mBehaviorOverrides[mechanic] = behavior;
    mOverrideDirty = true;
}

// speed.exe 1.3: 0x683500
void PVehicle::RemoveBehaviorOverride(UCrc32 mechanic) {
    BehaviorOverrides::iterator iter = mBehaviorOverrides.find(mechanic);
    if (iter != mBehaviorOverrides.end()) {
        mBehaviorOverrides.erase(iter);
        mOverrideDirty = true;
    }
}

// speed.exe 1.3: 0x688370
void PVehicle::CommitBehaviorOverrides() {
    if (mOverrideDirty) {
        mOverrideDirty = false;
        ReloadBehaviors();
    }
}

// speed.exe 1.3: 0x669050
void PVehicle::SetStaging(bool staging) {
    if (staging != mStaging) {
        mStaging = staging;
        if (staging) {
            SetSpeed(0.0f);
        }
    }
}

// speed.exe 1.3: 0x6700E0
void PVehicle::Launch() {
    if (mEngine && !mPerfectLaunch.IsSet()) {
        if (mDriverClass == DRIVER_HUMAN) {
            if (mPerfectLaunch.Amount > 0.0f) {
                mPerfectLaunch.Set(mPerfectLaunch.Amount);
                new EPerfectLaunch(ISimable::GetInstanceHandle(), mPerfectLaunch.Amount);
            }
        } else {
            mPerfectLaunch.Clear();
        }
    }
}

// speed.exe 1.3: 0x669020
float PVehicle::GetPerfectLaunch() const {
    if (!IsStaging() && mPerfectLaunch.IsSet()) {
        return mPerfectLaunch.Amount;
    }
    return 0.0f;
}

// speed.exe 1.3: 0x687690
void PVehicle::SetDriverStyle(DriverStyle style) {
    if (mDriverStyle != style) {
        mDriverStyle = style;
        ReloadBehaviors();
        if (mDriverStyle == STYLE_RACING && mEngine) {
            mEngine->MatchSpeed(mLocalVel.z);
        }
    }
}

// speed.exe 1.3: 0x6876E0
void PVehicle::SetDriverClass(DriverClass dc) {
    if (mDriverClass != dc) {
        mDriverClass = dc;
        UpdateListing();
        ReloadBehaviors();
    }
}

// speed.exe 1.3: 0x688390
void PVehicle::SetPhysicsMode(PhysicsMode mode) {
    if (mode != mPhysicsMode) {
        OnEndMode(mPhysicsMode);
        mPhysicsMode = mode;
        OnBeginMode(mode);
    }
    if (mArticulation) {
        IVehicle *trailer = mArticulation->GetTrailer();
        if (trailer) {
            trailer->SetPhysicsMode(mode);
        }
    }
}

// speed.exe 1.3: 0x6864A0
void PVehicle::OnBeginMode(PhysicsMode mode) {
    switch (mode) {
        case PHYSICS_MODE_INACTIVE:
            AddToList(VEHICLE_INACTIVE);
            if (mCollisionBody) {
                mCollisionBody->DisableTriggering();
            }
            Reset();
            break;
        case PHYSICS_MODE_SIMULATED:
            if (mCollisionBody) {
                mCollisionBody->EnableModeling();
            }
            CameraAI::AddAvoidable(this);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_DRAW), false);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_SUSPENSION), false);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_ENGINE), false);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_RIGIDBODY), false);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_EFFECTS), false);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_RESET), false);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_AUDIO), false);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_DAMAGE), false);
            break;
        case PHYSICS_MODE_EMULATED:
        default:
            break;
    }
}

// speed.exe 1.3: 0x6865D0
void PVehicle::OnEndMode(PhysicsMode mode) {
    switch (mode) {
        case PHYSICS_MODE_INACTIVE:
            if (mCollisionBody) {
                mCollisionBody->EnableTriggering();
            }
            Reset();
            UnList(VEHICLE_INACTIVE);
            break;
        case PHYSICS_MODE_SIMULATED:
            if (mCollisionBody) {
                mCollisionBody->DisableModeling();
            }
            CameraAI::RemoveAvoidable(this);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_DRAW), true);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_SUSPENSION), true);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_ENGINE), true);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_RIGIDBODY), true);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_EFFECTS), true);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_RESET), true);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_AUDIO), true);
            PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_DAMAGE), true);
            break;
        case PHYSICS_MODE_EMULATED:
        default:
            break;
    }
}

// speed.exe 1.3: 0x6693A0
void PVehicle::Activate() {
    if (mPhysicsMode == PHYSICS_MODE_INACTIVE) {
        SetPhysicsMode(PHYSICS_MODE_SIMULATED);
    }
}

// speed.exe 1.3: 0x6693C0
void PVehicle::Deactivate() {
    SetPhysicsMode(PHYSICS_MODE_INACTIVE);
}

// speed.exe 1.3: 0x6691D0
bool PVehicle::IsLoading() const {
    if (mRenderable && !mRenderable->IsRenderable()) {
        return true;
    }
    if (mAudible && !mAudible->IsAudible()) {
        return true;
    }
    if (mArticulation) {
        IVehicle *trailer = mArticulation->GetTrailer();
        if (trailer) {
            return trailer->IsLoading();
        }
    }
    return false;
}

// speed.exe 1.3: 0x669490
void PVehicle::ForceStopOn(char forceStopBits) {
    mForceStop |= forceStopBits;
    if (mInput) {
        mInput->ClearInput();
    }
}

// speed.exe 1.3: 0x6694C0
void PVehicle::ForceStopOff(char forceStopBits) {
    mForceStop &= ~forceStopBits;
    if (mInput) {
        mInput->ClearInput();
    }
}

// speed.exe 1.3: 0x688140
bool PVehicle::InShock() const {
    return mDamage && mDamage->InShock() > 0.0f;
}

// speed.exe 1.3: 0x688170
bool PVehicle::IsDestroyed() const {
    if (mDamage) {
        return mDamage->IsDestroyed();
    }
    return false;
}

// speed.exe 1.3: 0x670210
void PVehicle::SetSpeed(float speed) {
    mSpeed = speed;
    mPerfectLaunch.Clear();
    if (mCollisionBody) {
        UMath::Vector3 velocity;
        UMath::Scale(mCollisionBody->GetForwardVector(), speed, velocity);
        GetRigidBody()->SetLinearVelocity(velocity);
        if (mSuspension) {
            mSuspension->MatchSpeed(speed);
        }
        if (mEngine) {
            mEngine->MatchSpeed(speed);
        }
        UpdateLocalVelocities();
        if (mArticulation && mArticulation->GetTrailer()) {
            mArticulation->GetTrailer()->SetSpeed(speed);
        }
    }
}

// speed.exe 1.3: 0x669230
void PVehicle::UpdateLocalVelocities() {
    const IRigidBody *irb = GetRigidBody();
    if (irb && mCollisionBody) {
        mLocalVel = irb->GetLinearVelocity();
        irb->ConvertWorldToLocal(mLocalVel, false);
        mSlipAngle = VU0_Atan2(mLocalVel.x, UMath::Abs(mLocalVel.z));
        mSpeed = irb->GetSpeed();
        if (UMath::Dot(irb->GetLinearVelocity(), mCollisionBody->GetForwardVector()) < 0.0f) {
            mSpeed = -mSpeed;
        }
        mAbsSpeed = UMath::Abs(mSpeed);
    } else {
        mLocalVel = UMath::Vector3::kZero;
        mSlipAngle = 0.0f;
        mSpeed = 0.0f;
        mAbsSpeed = 0.0f;
    }
}

// speed.exe 1.3: 0x680840
void PVehicle::CheckOffWorld() {
    Behavior *suspension = FindBehavior(MechanicID(BEHAVIOR_MECHANIC_SUSPENSION));
    if (!suspension || suspension->IsPaused()) {
        float height = 0.0f;
        WCollisionMgr collision_mgr(0, 3);
        mOffWorld = !collision_mgr.GetWorldHeightAtPointRigorous(ISimable::GetPosition(), height, nullptr);
        return;
    }
    if (GetWPos().GetSurface() != SimSurface::kNull.GetConstCollection()) {
        mOffWorld = false;
        return;
    }
    if (mSuspension) {
        unsigned int offworld = 0;
        for (unsigned int i = 0; i < mSuspension->GetNumWheels(); i++) {
            if (mSuspension->GetWheelRoadSurface(i).GetConstCollection() == SimSurface::kNull.GetConstCollection()) {
                offworld++;
            }
        }
        mOffWorld = offworld >= 2;
        return;
    }
    mOffWorld = true;
}

// speed.exe 1.3: 0x6702D0: rates the launch while the car waits at the line
void PVehicle::DoStaging(float dT) {
    if (mPerfectLaunch.IsSet()) {
        return;
    }
    mPerfectLaunch.Amount = 0.0f;
    if (mEngine) {
        IRaceEngine *race_engine;
        if (mEngine->QueryInterface(&race_engine)) {
            float range = 0.0f;
            float lower = race_engine->GetPerfectLaunchRange(range);
            if (range > 0.0f && lower > 0.0f) {
                float over = mEngine->GetRPM() - lower;
                if (over < range && over > 0.0f) {
                    mPerfectLaunch.Amount = (over / range + 1.0f) * 0.5f;
                }
            }
        }
    }
}

// speed.exe 1.3: 0x686060
void PVehicle::OnTaskSimulate(float dT) {
#if defined(EA_PORT_NATIVE)
    if (TraceVehicle() && mDriverClass == DRIVER_HUMAN) {
        static float accum = 0.0f;
        accum += dT;
        if (accum >= 1.0f) {
            accum = 0.0f;
            const UMath::Vector3 &p = PhysicsObject::GetPosition();
            UMath::Vector3 v;
            GetLinearVelocity(v);
            const InputControls *in = mInput ? &mInput->GetControls() : nullptr;
            VEHICLE_TRACE("player pos %.2f %.2f %.2f vel %.2f %.2f %.2f speed %.2f wheels %u gas %.2f brake %.2f steer %.2f gear %d rpm %.0f\n",
                          p.x, p.y, p.z, v.x, v.y, v.z, mSpeed, mWheelsOnGround, in ? in->fGas : 0.0f, in ? in->fBrake : 0.0f,
                          in ? in->fSteering : 0.0f, mTranny ? (int)mTranny->GetGear() : -1, mEngine ? mEngine->GetRPM() : 0.0f);
        }
    }
#endif
    Behavior *draw = FindBehavior(MechanicID(BEHAVIOR_MECHANIC_DRAW));
    if (!draw || draw->IsPaused() || !mRenderable || !mRenderable->IsRenderable()) {
        mOffScreenTime = 0.0f;
        mOnScreenTime = 0.0f;
    } else if (!mRenderable->InView()) {
        mOffScreenTime += dT;
        mOnScreenTime = 0.0f;
    } else {
        mOffScreenTime = 0.0f;
        mOnScreenTime += dT;
    }

    CommitBehaviorOverrides();

    if (mCollisionBody) {
        bool modeling = mCollisionBody->IsModeling();
        if (modeling != mIsModeling) {
            mIsModeling = modeling;
            if (modeling) {
                OnEnableModeling();
            } else {
                OnDisableModeling();
            }
        }
    }

    if (mPhysicsMode != PHYSICS_MODE_INACTIVE) {
        UpdateLocalVelocities();
        CheckOffWorld();
    }

    if (mPhysicsMode != PHYSICS_MODE_SIMULATED) {
        mTimeInAir = 0.0f;
        if (mPhysicsMode != PHYSICS_MODE_EMULATED) {
            mWheelsOnGround = 0;
            mSpeedometer = 0.0f;
            return;
        }
        mWheelsOnGround = mSuspension ? mSuspension->GetNumWheels() : 0;
        mSpeedometer = mAbsSpeed;
        return;
    }

    // a resting wreck stops simulating its suspension
    PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_SUSPENSION), mCollisionBody->IsSleeping() && IsDestroyed());

    if (!mTranny) {
        mSpeedometer = mAbsSpeed;
    } else if (!mTranny->IsGearChanging()) {
        mSpeedometer = mTranny->GetSpeedometer();
    }

    if (mSuspension) {
        unsigned int wheels = mSuspension->GetNumWheelsOnGround();
        if (wheels == 0 && mWheelsOnGround != 0 && mDriverClass == DRIVER_HUMAN) {
            new EPlayerAirborne(ISimable::GetInstanceHandle());
        }
        mWheelsOnGround = wheels;
    }
    if (!mSuspension || mWheelsOnGround == 0) {
        mTimeInAir += dT;
    } else {
        mTimeInAir = 0.0f;
    }

    if (IsStaging()) {
        DoStaging(dT);
    } else if (mPerfectLaunch.Time > 0.0f && mTranny) {
        if (!mTranny->IsGearChanging()) {
            mPerfectLaunch.Tick(dT);
        } else if (mDriverStyle == STYLE_DRAG) {
            mPerfectLaunch.Clear();
        }
        if (GetSpeed() > 26.8218f) {
            mPerfectLaunch.Clear();
        }
    }
}

// speed.exe 1.3: GC 0x8021407C
void PVehicle::OnDisableModeling() {
    // TODO(port): the PC also tells one more interface (handle 0x668A90, slot 7,
    // not identified yet) that modeling stopped.
}

// speed.exe 1.3: 0x6703B0
void PVehicle::OnTaskFX(float dT) {
    GetRigidBody();
    if (!INIS::Exists()) {
        GlareOn(static_cast<VehicleFX::ID>(7));
    }
    bool braking = false;
    if (mInput && mInput->GetControls().fBrake > 0.0f) {
        braking = true;
    }
    if (!IsPlayer()) {
        if (braking) {
            mBrakeTime += dT;
        } else {
            mBrakeTime = 0.0f;
        }
        if (mBrakeTime > 0.5f) {
            GlareOn(static_cast<VehicleFX::ID>(0x38));
        } else {
            GlareOff(static_cast<VehicleFX::ID>(0x38));
        }
    } else if (braking) {
        GlareOn(static_cast<VehicleFX::ID>(0x38));
    } else {
        GlareOff(static_cast<VehicleFX::ID>(0x38));
    }
    if (mTranny && mTranny->IsReversing()) {
        GlareOn(static_cast<VehicleFX::ID>(0xC0));
    } else {
        GlareOff(static_cast<VehicleFX::ID>(0xC0));
    }
}

// speed.exe 1.3: 0x686410
void PVehicle::SetAnimating(bool animate) {
    if (animate != mAnimating) {
        mBehaviorOverrides.clear();
        mAnimating = animate;
        ReloadBehaviors();
        PauseBehavior(MechanicID(BEHAVIOR_MECHANIC_AI), animate);
        if (mCollisionBody) {
            mCollisionBody->SetAnimating(mAnimating);
        }
    }
}

// speed.exe 1.3: 0x670630
const Physics::Tunings *PVehicle::GetTunings() const {
    if (GetDriverClass() == DRIVER_HUMAN) {
        if (Tweak_UseTweakerTunings) {
            static Physics::Tunings tweaker_tunings;
            return &tweaker_tunings;
        }
        if (mCustomization) {
            return &mCustomization->GetActiveTunings();
        }
    }
    return nullptr;
}

// speed.exe 1.3: 0x6693D0
void PVehicle::SetTunings(const Physics::Tunings &tunings) {
    if (mCustomization) {
        mCustomization->GetActiveTunings() = tunings;
    }
}

// speed.exe 1.3: 0x688270
bool PVehicle::GetPerformance(Physics::Info::Performance &performance) const {
    performance = mPerformance;
    return mPerformanceValid;
}

// speed.exe 1.3: 0x670700
void PVehicle::ComputeHeading(UMath::Vector3 *v) {
    const IRigidBody *irb = GetRigidBody();
    float speed = irb->GetSpeed();
    UMath::Vector3 forward;
    irb->GetForwardVector(forward);
    UMath::Vector3 velocity = irb->GetLinearVelocity();
    if (speed > 0.01f) {
        UMath::Unit(velocity, velocity);
    }
    float blend = UMath::Clamp(speed, 0.0f, 1.0f);
    v->x = velocity.x * blend + forward.x * (1.0f - blend);
    v->y = velocity.y * blend + forward.y * (1.0f - blend);
    v->z = velocity.z * blend + forward.z * (1.0f - blend);
    UMath::Unit(*v, *v);
}

// speed.exe 1.3: 0x6704B0
bool PVehicle::SetDynamicData(const EventSequencer::System *system, EventDynamicData *data) {
    if (IsDirty()) {
        return false;
    }
    data->fhSimable = reinterpret_cast<uintptr_t>(ISimable::GetInstanceHandle());
    data->fWorldID = GetWorldID();
    const IRigidBody *irb = GetRigidBody();
    if (irb) {
        const UMath::Vector3 &position = irb->GetPosition();
        data->fPosition = UMath::Vector4Make(position, 0.0f);
        UMath::Vector3 forward;
        irb->GetForwardVector(forward);
        data->fVector = UMath::Vector4Make(forward, 0.0f);
        data->fVelocity = UMath::Vector4Make(irb->GetLinearVelocity(), 1.0f);
        data->fAngularVelocity = UMath::Vector4Make(irb->GetAngularVelocity(), 1.0f);
    }
    if (mRenderable) {
        data->fhModel = reinterpret_cast<uintptr_t>(mRenderable->GetModelHandle());
    }
    return true;
}

// speed.exe 1.3: 0x676E20
bool PVehicle::OnExplosion(const UMath::Vector3 &normal, const UMath::Vector3 &position, float dT, IExplosion *explosion) {
    unsigned int targets = explosion->GetTargets();
    if (!(targets & 2)) {
        return false;
    }
    if (IsPlayer() && !(targets & 4)) {
        return false;
    }
    if (!GetCausality() && explosion->GetCausality()) {
        ICause *cause = ICause::FindInstance(explosion->GetCausality());
        if (cause) {
            cause->OnCausedExplosion(explosion, this);
        }
    }

    IRigidBody *irb = GetRigidBody();
    float speed = explosion->GetExpansionSpeed();
    UMath::Vector3 pointvel;
    irb->GetPointVelocity(position, pointvel);
    float closing = UMath::Dot(pointvel, normal);
    if (closing < speed) {
        float impulse = (speed - closing) * irb->GetMass() / dT;
        UMath::Vector3 force;
        UMath::Scale(normal, impulse, force);
        irb->ResolveForce(force, position);
    }

    if (mSequencer) {
        mSequencer->ProcessStimulus(0xab556d39, Sim::GetTime(), nullptr, EventSequencer::QUEUE_ALLOW);
        if (explosion->HasDamage()) {
            mSequencer->ProcessStimulus(0xffcd8a63, Sim::GetTime(), nullptr, EventSequencer::QUEUE_ALLOW);
        }
    }
    return true;
}

// speed.exe 1.3: 0x685810
// Drops the car onto the road at <resetPos>, facing <initialVec>: each wheel
// is projected onto the ground at its resting compression and the body is
// fitted to the four contact points.
bool PVehicle::SetVehicleOnGround(const UMath::Vector3 &resetPos, const UMath::Vector3 &initialVec) {
    IRigidBody *irb = GetRigidBody();
    if (!irb || !mCollisionBody) {
        return false;
    }

    MJumpCut(GetWorldID()).Send(UCrc32("CameraMessagePort"));

    UMath::Vector3 dim;
    irb->GetDimension(dim);
    UMath::Vector3 position = resetPos;
    position.y += dim.y;
    UMath::Matrix4 mat = Util_GenerateMatrix(initialVec, nullptr);
    irb->SetLinearVelocity(UMath::Vector3::kZero);
    irb->SetAngularVelocity(UMath::Vector3::kZero);

    bool result = true;
    float weight = mCollisionBody->GetGravity() * irb->GetMass();
    float height = 0.0f;
    WCollisionMgr collision_mgr(0, 3);
    bool offworld = !collision_mgr.GetWorldHeightAtPointRigorous(position, height, nullptr);
    if (offworld) {
        position = resetPos;
        result = false;
    } else if (!mSuspension) {
        position = resetPos;
        position.y = height + dim.y;
    } else {
        WWorldPos wpos(0.1f);
        wpos.SetTolerance(dim.y);
        position.y = height;

        UMath::Vector4 center = UMath::Vector4::kZero;
        UMath::Vector4 origin = UMath::Vector4Make(position, 1.0f);
        UMath::Vector4 wheels[4];
        for (unsigned int i = 0; i < 4; i++) {
            UMath::Vector4 &wheel = wheels[i];
            const UMath::Vector3 &local = mSuspension->GetWheelLocalPos(i);
            wheel = UMath::Vector4Make(local, 0.0f);
            center.x += wheel.x * 0.25f;
            center.z += wheel.z * 0.25f;
            center.w += wheel.w * 0.25f;
            wheel.y = 0.0f;
            UMath::Vector4 rotated;
            rotated.x = wheel.x * mat.v0.x + wheel.y * mat.v1.x + wheel.z * mat.v2.x;
            rotated.y = wheel.x * mat.v0.y + wheel.y * mat.v1.y + wheel.z * mat.v2.y;
            rotated.z = wheel.x * mat.v0.z + wheel.y * mat.v1.z + wheel.z * mat.v2.z;
            rotated.w = wheel.w;
            wheel.x = rotated.x + origin.x;
            wheel.y = rotated.y + origin.y;
            wheel.z = rotated.z + origin.z;
            wheel.w = rotated.w + origin.w;
            wpos.FindClosestFace(UMath::Vector4To3(wheel), true);
            if (wpos.OnValidFace()) {
                float compression = mSuspension->GuessCompression(i, weight);
                float ride = mSuspension->GetRideHeight(i) - compression;
                wheel.y = wpos.HeightAtPoint(UMath::Vector4To3(wheel)) + ride + dim.y;
            } else {
                result = false;
            }
        }

        if (!result) {
            position = resetPos;
        } else {
            // front/rear and left/right axle midpoints
            UMath::Vector3 front, rear, left, right;
            UMath::Add(UMath::Vector4To3(wheels[0]), UMath::Vector4To3(wheels[1]), front);
            UMath::Scale(front, 0.5f, front);
            UMath::Add(UMath::Vector4To3(wheels[2]), UMath::Vector4To3(wheels[3]), rear);
            UMath::Scale(rear, 0.5f, rear);
            UMath::Add(UMath::Vector4To3(wheels[2]), UMath::Vector4To3(wheels[0]), left);
            UMath::Scale(left, 0.5f, left);
            UMath::Add(UMath::Vector4To3(wheels[3]), UMath::Vector4To3(wheels[1]), right);
            UMath::Scale(right, 0.5f, right);

            UMath::Vector4 forward, side, up, across;
            UMath::Sub(front, rear, UMath::Vector4To3(forward));
            forward.w = 0.0f;
            VU0_v4scalexyz(forward, 1.0f, forward);
            UMath::Unit(UMath::Vector4To3(forward), UMath::Vector4To3(forward));
            UMath::Sub(right, left, UMath::Vector4To3(across));
            across.w = 0.0f;
            UMath::Unit(UMath::Vector4To3(across), UMath::Vector4To3(across));
            UMath::Cross(UMath::Vector4To3(forward), UMath::Vector4To3(across), UMath::Vector4To3(up));
            UMath::Unit(UMath::Vector4To3(up), UMath::Vector4To3(up));
            UMath::Cross(UMath::Vector4To3(up), UMath::Vector4To3(forward), UMath::Vector4To3(side));
            UMath::Unit(UMath::Vector4To3(side), UMath::Vector4To3(side));
            mat.v0 = UMath::Vector4Make(UMath::Vector4To3(side), 0.0f);
            mat.v1 = UMath::Vector4Make(UMath::Vector4To3(up), 0.0f);
            mat.v2 = UMath::Vector4Make(UMath::Vector4To3(forward), 0.0f);

            // the wheels' centroid sits at the centre of the contact patch
            UMath::Vector3 offset;
            offset.x = center.x * mat.v0.x + center.y * mat.v1.x + center.z * mat.v2.x;
            offset.y = center.x * mat.v0.y + center.y * mat.v1.y + center.z * mat.v2.y;
            offset.z = center.x * mat.v0.z + center.y * mat.v1.z + center.z * mat.v2.z;

            UMath::Vector3 midpoint;
            midpoint.x = ((front.x + rear.x) * 0.5f + (left.x + right.x) * 0.5f) * 0.5f;
            midpoint.y = ((front.y + rear.y) * 0.5f + (left.y + right.y) * 0.5f) * 0.5f;
            midpoint.z = ((front.z + rear.z) * 0.5f + (left.z + right.z) * 0.5f) * 0.5f;
            UMath::Sub(midpoint, offset, position);
        }
    }

    irb->PlaceObject(mat, position);
    mat.v3 = UMath::Vector4Make(position, 1.0f);
    WorldConn::Pkt_Body_Send pkt(GetWorldID(), mat);
    Sim::SendService(UCrc32(UCRC32_WORLD), &pkt);

    // TODO(port): online play isn't ported; the PC also tells the player's
    // IOnlinePlayer (slot 3) here.

    if (mArticulation && !mArticulation->Pose()) {
        result = false;
    }
    ResetBehavior(MechanicID(BEHAVIOR_MECHANIC_SUSPENSION));
    ResetBehavior(MechanicID(BEHAVIOR_MECHANIC_ENGINE));
    ResetBehavior(MechanicID(BEHAVIOR_MECHANIC_AI));
    ResetBehavior(MechanicID(BEHAVIOR_MECHANIC_RESET));
    SetSpeed(0.0f);
    mOffWorld = !result;
    AITarget::Track(this);
    return result;
}
