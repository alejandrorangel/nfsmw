// Smackable props in the world: the spawners each track section places
// (BCHUNK_SPEED_SMOKEABLE_SPAWNER), the models standing in for them
// (SceneryModel, PlaceableScenery and their base HeirarchyModel) and the
// knocked-over prop timers. Reconstructed from speed.exe 1.3; the console
// layouts come from the PS2 symbols and GOWE69 function order.
//
// A loaded spawner owns a SceneryModel. The model puts a SmackableTrigger
// around the prop; when a rigid body hits it the trigger's ESpawnSmackable
// event attaches a Smackable simable, the model opens a SmackableRenderConn
// to draw the moving prop and hides the static scenery instance.

#include "Speed/Indep/Src/Physics/SmokeableInfo.hpp"

#include "Speed/Indep/Libs/Support/Utility/FastMem.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Src/AI/AIAvoidable.h"
#include "Speed/Indep/Src/Camera/CameraAI.hpp"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/smackable.h"
#include "Speed/Indep/Src/Generated/Hash.hpp"
#include "Speed/Indep/Src/Interfaces/IBody.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/Src/Interfaces/SimModels/IPlaceableScenery.h"
#include "Speed/Indep/Src/Interfaces/SimModels/ISceneryModel.h"
#include "Speed/Indep/Src/Interfaces/SimModels/ITriggerableModel.h"
#include "Speed/Indep/Src/Interfaces/Simables/IRigidBody.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Misc/ResourceLoader.hpp"
#include "Speed/Indep/Src/Misc/SpeedChunks.hpp"
#include "Speed/Indep/Src/Physics/Bounds.h"
#include "Speed/Indep/Src/Physics/Smackable.h"
#include "Speed/Indep/Src/Physics/SmackableTrigger.h"
#include "Speed/Indep/Src/Render/RenderConn.h"
#include "Speed/Indep/Src/Sim/SimModel.h"
#include "Speed/Indep/Src/Sim/Simulation.h"
#include "Speed/Indep/Src/Sim/Util.h"
#include "Speed/Indep/Src/World/Scenery.hpp"
#include "Speed/Indep/Src/World/WCollisionMgr.h"
#include "Speed/Indep/bWare/Inc/bChunk.hpp"
#include <cstdio>
#include <cstdlib>
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#if defined(EA_PLATFORM_WIN32)
#include "Speed/PC/Src/PCGraphicsOptions.hpp"
#endif

ScenerySectionHeader *GetScenerySectionHeader(int section_number);
extern SceneryOverrideInfo *SceneryOverrideInfoTable;
extern int NumSceneryOverrideInfos;
ModelHeirarchy *FindSceneryHeirarchyByName(unsigned int name_hash);

namespace {

// Scenery instances an override turns off (SceneryOverrideInfo::ExcludeFlags):
// excluded from every scenery cull (SetupSceneryCullInfo always sets 0x10).
const unsigned short kSceneryOverrideHidden = 0x10;

// The bHash32 of the "missing model" mesh (speed.exe 1.3: 0x66B730, 0x681410).
const unsigned int kMissingRenderMesh = 0xc7395a8;

} // namespace

class HeirarchyModel;

// speed.exe 1.3 (FastMem 0x1C): what AI avoidance sees of a prop
// (AI_AVOIDABLE). Positions come from the model (OnUpdateAvoidable).
class SmackableAvoidable : public AIAvoidable {
  public:
    USE_FASTALLOC(SmackableAvoidable);

    explicit SmackableAvoidable(HeirarchyModel *model);
    ~SmackableAvoidable() override {}

    void SetBody(UTL::COM::IUnknown *body) {
        SetAvoidableObject(body);
    }

  protected:
    bool OnUpdateAvoidable(UMath::Vector3 &pos, float &sweep) override;

  private:
    HeirarchyModel *mModel; // offset 0x18, size 0x4
};

// speed.exe 1.3 (0xE4 bytes): a prop model, or one node of a prop's model
// heirarchy. Lies in the world behind a SmackableTrigger until something
// smacks it; then a Smackable simable attaches and a SmackableRenderConn
// draws it (Pkt_Smackable_Open).
class HeirarchyModel : public Sim::Model, public IBody, public ITriggerableModel {
  public:
    HeirarchyModel(bHash32 rendermesh, const CollisionGeometry::Bounds *geometry, UCrc32 rendernode, HeirarchyModel *parent,
                   const Attrib::Collection *attribs, const ModelHeirarchy *heirarchy, uint32 heirarchynode, bool visible);
    ~HeirarchyModel() override;

    // IModel
    void OnProcessFrame(float dT) override;
    WUID GetWorldID() const override {
        return Sim::Model::GetWorldID();
    }
    const Attrib::Instance &GetAttributes() const override {
        return mAttributes;
    }
    void GetTransform(UMath::Matrix4 &matrix) const override;
    void GetLinearVelocity(UMath::Vector3 &velocity) const override {
        Sim::Model::GetLinearVelocity(velocity);
    }
    void GetAngularVelocity(UMath::Vector3 &velocity) const override;
    IModel *SpawnModel(UCrc32 rendernode, UCrc32 collisionnode, UCrc32 attributes) override;
    void HidePart(const UCrc32 &nodename) override;
    void ShowPart(const UCrc32 &nodename) override;
    bool IsPartVisible(const UCrc32 &nodename) const override;

    // IBody
    void GetDimension(UMath::Vector3 &dim) const override;

    // ITriggerableModel
    void PlaceTrigger(const UMath::Matrix4 &matrix, bool enable) override;

    virtual bool OnUpdateAvoidable(UMath::Vector3 &pos, float &sweep);

  protected:
    virtual bool OnRemoveOffScreen(float dT);

    // Sim::Model
    void OnBeginSimulation() override;
    void OnEndSimulation() override;
    void OnBeginDraw() override;
    bool OnDraw(Sim::Packet *service) override;
    void OnEndDraw() override;

    void SetTrigger(const UMath::Matrix4 &matrix, bool virgin);
    void RemoveTrigger();
    void SetAvoidable(bool enable);
    void SetCameraAvoidable();
    void RemoveCameraAvoidable();
    int FindHeirarchyChild(const UCrc32 &nodename) const;

    void StartAttribSequencer() {
        StartSequencer(UCrc32(mAttributes.EventSequencer().GetHash32()));
    }

    SmackableTrigger *GetTrigger() const {
        return mTrigger;
    }

    void SetOffScreenTimer(float t) {
        mOffScreenTimer = t;
    }

    Attrib::Gen::smackable mAttributes; // offset 0xA4, size 0x14
    SmackableAvoidable *mAvoidable;     // offset 0xE0, size 0x4

  private:
    enum Flags {
        F_CAMERA_AVOIDABLE = 1,
    };

    UMath::Vector4 mTriggerAvoid;     // offset 0xB8, size 0x10
    const ModelHeirarchy *mHeirarchy; // offset 0xC8, size 0x4
    bHash32 mRenderMesh;              // offset 0xCC, size 0x4
    SmackableTrigger *mTrigger;       // offset 0xD0, size 0x4
    float mOffScreenTimer;            // offset 0xD4, size 0x4
    uint16 mHeirarchyNode;            // offset 0xD8, size 0x2
    uint16 mFlags;                    // offset 0xDA, size 0x2
    uint32 mChildVisibility;          // offset 0xDC, size 0x4
};

// speed.exe 1.3: 0x66B400
SmackableAvoidable::SmackableAvoidable(HeirarchyModel *model) : AIAvoidable(static_cast<IModel *>(model)), mModel(model) {
    SetAvoidableObject(model ? static_cast<IBody *>(model) : nullptr);
}

// speed.exe 1.3: 0x66B450
bool SmackableAvoidable::OnUpdateAvoidable(UMath::Vector3 &pos, float &sweep) {
    return mModel->OnUpdateAvoidable(pos, sweep);
}

// speed.exe 1.3: 0x6722C0
HeirarchyModel::HeirarchyModel(bHash32 rendermesh, const CollisionGeometry::Bounds *geometry, UCrc32 rendernode, HeirarchyModel *parent,
                               const Attrib::Collection *attribs, const ModelHeirarchy *heirarchy, uint32 heirarchynode, bool visible)
    : Sim::Model(parent, geometry, rendernode, 3), //
      IBody(this),                                 //
      ITriggerableModel(this),                     //
      mAttributes(attribs, 0, nullptr),            //
      mAvoidable(nullptr),                         //
      mTriggerAvoid(UMath::Vector4::kZero),        //
      mHeirarchy(heirarchy),                       //
      mRenderMesh(rendermesh),                     //
      mTrigger(nullptr),                           //
      mOffScreenTimer(0.0f),                       //
      mHeirarchyNode(static_cast<uint16>(heirarchynode)),
      mFlags(0),                                   //
      mChildVisibility(0xFFFFFFFF) {
    Attrib::Gen::smackable attributes(attribs, 0, nullptr);
    if (visible) {
        RenderConn::Pkt_Smackable_Open pkt(mRenderMesh, GetWorldID(), GetCollisionGeometry(), mHeirarchy, mHeirarchyNode);
        BeginDraw(UCrc32(UCRC32_ECSTASY), &pkt);
    }
    if (attributes.AI_AVOIDABLE()) {
        SetAvoidable(true);
    }
    if (attributes.CAMERA_AVOIDABLE()) {
        SetCameraAvoidable();
    }
}

// speed.exe 1.3: 0x67B970
HeirarchyModel::~HeirarchyModel() {
    EndSimulation();
    RemoveTrigger();
    ReleaseChildModels();
    if (mAvoidable != nullptr) {
        delete mAvoidable;
        mAvoidable = nullptr;
    }
    RemoveCameraAvoidable();
}

// speed.exe 1.3: 0x672220
void HeirarchyModel::SetAvoidable(bool enable) {
    if ((mAvoidable != nullptr) == enable) {
        return;
    }
    if (!enable) {
        delete mAvoidable;
        mAvoidable = nullptr;
    } else {
        mAvoidable = new SmackableAvoidable(this);
    }
}

// speed.exe 1.3: inlined (0x6722C0, 0x6778F0)
void HeirarchyModel::SetCameraAvoidable() {
    if ((mFlags & F_CAMERA_AVOIDABLE) == 0 && mAttributes.CAMERA_AVOIDABLE()) {
        CameraAI::AddAvoidable(this);
        mFlags |= F_CAMERA_AVOIDABLE;
    }
}

void HeirarchyModel::RemoveCameraAvoidable() {
    if ((mFlags & F_CAMERA_AVOIDABLE) != 0) {
        CameraAI::RemoveAvoidable(this);
        mFlags &= ~F_CAMERA_AVOIDABLE;
    }
}

// speed.exe 1.3: 0x66AF60. Releases a prop that has been out of view for
// KILL_OFF_SCREEN seconds.
bool HeirarchyModel::OnRemoveOffScreen(float dT) {
    if (mOffScreenTimer > 0.0f) {
        if (!InView()) {
            mOffScreenTimer -= dT;
            if (mOffScreenTimer < 0.0f) {
                mOffScreenTimer = 0.0f;
                return true;
            }
        } else {
            mOffScreenTimer = mAttributes.KILL_OFF_SCREEN();
        }
    }
    return false;
}

#if defined(EA_PORT_NATIVE)
// NFSMW_DEBUG_SMACK=<seconds>: from that sim time on, every 6 seconds, puts
// the local player's car 15 m in front of the nearest resting scenery prop,
// heading for it at 20 m/s (to check props get knocked over).
static void PortDebugAimAtProp() {
    static float start = -2.0f;
    if (start == -2.0f) {
        const char *env = std::getenv("NFSMW_DEBUG_SMACK");
        start = env ? static_cast<float>(std::atof(env)) : -1.0f;
    }
    if (start < 0.0f) {
        return;
    }
    static float last_time = -1.0f;
    static float next_time = 0.0f;
    float now = Sim::GetTime();
    if (now == last_time) {
        return;
    }
    last_time = now;
    if (now < start || now < next_time) {
        return;
    }
    next_time = now + 6.0f;

    IPlayer *player = IPlayer::First(PLAYER_LOCAL);
    ISimable *car = player ? player->GetSimable() : nullptr;
    IRigidBody *irb = car ? car->GetRigidBody() : nullptr;
    if (irb == nullptr) {
        return;
    }
    const UMath::Vector3 carpos = irb->GetPosition();
    float best = 1.0e9f;
    UMath::Matrix4 target;
    for (IModel::List::const_iterator iter = IModel::GetList().begin(); iter != IModel::GetList().end(); ++iter) {
        IModel *imodel = *iter;
        ISceneryModel *iscenery;
        UMath::Matrix4 matrix;
        if (imodel->GetSimable() != nullptr || !imodel->QueryInterface(&iscenery) || !iscenery->GetSceneryTransform(matrix)) {
            continue;
        }
        float d = UMath::Distance(UMath::Vector4To3(matrix.v3), carpos);
        if (d > 10.0f && d < best) {
            best = d;
            target = matrix;
        }
    }
    if (best > 300.0f) {
        std::fprintf(stderr, "[smack] debug: no prop near the player\n");
        return;
    }
    UMath::Vector3 dir;
    UMath::Sub(UMath::Vector4To3(target.v3), carpos, dir);
    dir.y = 0.0f;
    UMath::Normalize(dir);
    UMath::Vector3 pos;
    UMath::ScaleAdd(dir, -15.0f, UMath::Vector4To3(target.v3), pos);
    float height = pos.y;
    WCollisionMgr collisionmgr(0, 3);
    if (collisionmgr.GetWorldHeightAtPointRigorous(pos, height, nullptr)) {
        pos.y = height + 0.6f;
    }
    UMath::Matrix4 orient = Util_GenerateMatrix(dir, nullptr);
    irb->PlaceObject(orient, pos);
    UMath::Vector3 vel;
    UMath::Scale(dir, 20.0f, vel);
    irb->SetLinearVelocity(vel);
    irb->SetAngularVelocity(UMath::Vector3::kZero);
    std::fprintf(stderr, "[smack] debug: aimed at the prop at %.1f %.1f %.1f (%.0f m away)\n", target.v3.x, target.v3.y, target.v3.z, best);
}
#endif

// speed.exe 1.3: 0x66AFE0
void HeirarchyModel::OnProcessFrame(float dT) {
#if defined(EA_PORT_NATIVE)
    PortDebugAimAtProp();
#endif
    if (OnRemoveOffScreen(dT)) {
        ReleaseModel();
    }
}

// speed.exe 1.3: 0x66B020
int HeirarchyModel::FindHeirarchyChild(const UCrc32 &nodename) const {
    if (mHeirarchy == nullptr) {
        return -1;
    }
    const ModelHeirarchy::Node *nodes = mHeirarchy->GetNodes();
    const ModelHeirarchy::Node &node = nodes[mHeirarchyNode];
    for (unsigned int i = 0; i < node.mNumChildren; i++) {
        if (nodes[node.mChildIndex + i].mNodeName == nodename) {
            return node.mChildIndex + i;
        }
    }
    return -1;
}

// speed.exe 1.3: 0x672760
void HeirarchyModel::HidePart(const UCrc32 &nodename) {
    int child = FindHeirarchyChild(nodename);
    if (child >= 0) {
        mChildVisibility &= ~(1u << (child & 31));
    }
}

// speed.exe 1.3: 0x6727A0
void HeirarchyModel::ShowPart(const UCrc32 &nodename) {
    int child = FindHeirarchyChild(nodename);
    if (child >= 0) {
        mChildVisibility |= 1u << (child & 31);
    }
}

// speed.exe 1.3: 0x6727D0
bool HeirarchyModel::IsPartVisible(const UCrc32 &nodename) const {
    int child = FindHeirarchyChild(nodename);
    if (child < 0) {
        return false;
    }
    return (mChildVisibility & (1u << (child & 31))) != 0;
}

// speed.exe 1.3: 0x677540. A node of the heirarchy breaking off as its own
// prop (it is drawn straight away).
IModel *HeirarchyModel::SpawnModel(UCrc32 rendernode, UCrc32 collisionnode, UCrc32 attributes) {
    if (mHeirarchy == nullptr || IsDirty() || IModel::Count() > IModel::InstanceLimit) {
        return nullptr;
    }
    int child = FindHeirarchyChild(rendernode);
    if (child < 0) {
        return nullptr;
    }
    const CollisionGeometry::Bounds *geometry = GetCollisionGeometry();
    const CollisionGeometry::Bounds *childgeom = geometry->fCollection->GetChild(geometry, collisionnode);
    if (childgeom == nullptr) {
        return nullptr;
    }
    const Attrib::Collection *spec = SmokeableSpawner::FindAttributes(attributes);
    if (spec == nullptr) {
        return nullptr;
    }
    const eModel *model = mHeirarchy->GetNodes()[child].mModel;
    if (model == nullptr) {
        return nullptr;
    }
    return new HeirarchyModel(bHash32(const_cast<eModel *>(model)->GetNameHash()), childgeom, rendernode, this, spec, mHeirarchy, child, true);
}

// speed.exe 1.3: 0x66B100
void HeirarchyModel::GetTransform(UMath::Matrix4 &matrix) const {
    ISimable *isimable = GetSimable();
    if (isimable != nullptr) {
        isimable->GetTransform(matrix);
    } else if (mTrigger != nullptr) {
        mTrigger->GetObjectMatrix(matrix);
    } else {
        matrix = UMath::Matrix4::kIdentity;
    }
}

// speed.exe 1.3: 0x66B150
void HeirarchyModel::GetAngularVelocity(UMath::Vector3 &velocity) const {
    ISimable *isimable = GetSimable();
    if (isimable != nullptr) {
        velocity = isimable->GetRigidBody()->GetAngularVelocity();
    } else {
        velocity = UMath::Vector3::kZero;
    }
}

// speed.exe 1.3: 0x672630
void HeirarchyModel::GetDimension(UMath::Vector3 &dim) const {
    GetCollisionGeometry()->GetHalfDimensions(dim);
}

// speed.exe 1.3: 0x672810 (inlined)
void HeirarchyModel::RemoveTrigger() {
    if (mTrigger != nullptr) {
        delete mTrigger;
        mTrigger = nullptr;
    }
}

// speed.exe 1.3: 0x672840. The trigger box is the collision geometry's; AI
// avoidance uses its footprint radius in the ground plane.
void HeirarchyModel::SetTrigger(const UMath::Matrix4 &matrix, bool virgin) {
    UMath::Vector3 dim;
    GetCollisionGeometry()->GetHalfDimensions(dim);
    if (mTrigger == nullptr) {
        mTrigger = new SmackableTrigger(IModel::GetInstanceHandle(), virgin, matrix, dim, 0);
    } else {
        mTrigger->Move(matrix, dim, virgin);
        mTrigger->Enable();
    }
    mTriggerAvoid = matrix.v3;
    float x = dim.x * matrix.v0.x + dim.y * matrix.v1.x + dim.z * matrix.v2.x;
    float z = dim.x * matrix.v0.z + dim.y * matrix.v1.z + dim.z * matrix.v2.z;
    mTriggerAvoid.w = UMath::Sqrt(x * x + z * z);
}

// speed.exe 1.3: 0x6729B0
void HeirarchyModel::PlaceTrigger(const UMath::Matrix4 &matrix, bool enable) {
    SetTrigger(matrix, false);
    if (!enable && mTrigger != nullptr) {
        mTrigger->Disable();
    }
}

// speed.exe 1.3: 0x66B1D0. A resting prop is avoided by its trigger, a moving
// one by its rigid body.
bool HeirarchyModel::OnUpdateAvoidable(UMath::Vector3 &pos, float &sweep) {
    if (mTrigger == nullptr || !mTrigger->IsEnabled()) {
        ISimable *isimable = GetSimable();
        if (isimable != nullptr) {
            IRigidBody *irb = isimable->GetRigidBody();
            if (irb != nullptr) {
                sweep = irb->GetRadius();
                pos = irb->GetPosition();
                return true;
            }
        }
        return false;
    }
    if (mTriggerAvoid.w > 0.0f) {
        pos = UMath::Vector4To3(mTriggerAvoid);
        sweep = mTriggerAvoid.w;
        return true;
    }
    return false;
}

// speed.exe 1.3: 0x66B2A0. Smacked: the trigger goes quiet and the model
// starts drawing as a moving prop.
void HeirarchyModel::OnBeginSimulation() {
    if (mTrigger != nullptr) {
        mTrigger->Disable();
    }
    if (mAvoidable != nullptr) {
        mAvoidable->SetBody(GetSimable());
    }
    RenderConn::Pkt_Smackable_Open pkt(mRenderMesh, GetWorldID(), GetCollisionGeometry(), mHeirarchy, mHeirarchyNode);
    BeginDraw(UCrc32(UCRC32_ECSTASY), &pkt);
}

// speed.exe 1.3: 0x66B280
void HeirarchyModel::OnEndSimulation() {
    if (mAvoidable != nullptr) {
        mAvoidable->SetBody(static_cast<IBody *>(this));
    }
}

// speed.exe 1.3: 0x66B080
void HeirarchyModel::OnBeginDraw() {
    StartAttribSequencer();
    mOffScreenTimer = mAttributes.KILL_OFF_SCREEN();
}

// speed.exe 1.3: 0x66B380
bool HeirarchyModel::OnDraw(Sim::Packet *service) {
    RenderConn::Pkt_Smackable_Service *pkt = static_cast<RenderConn::Pkt_Smackable_Service *>(service);
    UpdateVisibility(pkt->InView(), pkt->DistanceToView());
    pkt->SetChildVisibility(mChildVisibility);
    return true;
}

// speed.exe 1.3: 0x66B0D0
void HeirarchyModel::OnEndDraw() {
    mOffScreenTimer = 0.0f;
    RemoveCameraAvoidable();
}

// speed.exe 1.3 (0xEC bytes): a prop placed by gameplay code (road block
// sawhorses, spike strips; AICopManager). It becomes a persistent Smackable
// as soon as it is placed.
class PlaceableScenery : public HeirarchyModel, public IPlaceableScenery {
  public:
    PlaceableScenery(bHash32 rendermesh, const CollisionGeometry::Bounds *geometry, const Attrib::Collection *attribs,
                     const ModelHeirarchy *heirarchy);
    ~PlaceableScenery() override {}

    static PlaceableScenery *Construct(const char *name, Attrib::Key attributes);

    // IPlaceableScenery
    void PickUp() override;
    bool Place(const UMath::Matrix4 &transform, bool snap_to_ground) override;
    void Destroy() override {
        Sim::Model::ReleaseModel();
    }

    // IModel
    void ReleaseModel() override {
        PickUp();
    }

  protected:
    bool OnRemoveOffScreen(float dT) override {
        return false;
    }
};

// speed.exe 1.3: 0x67BAA0
PlaceableScenery::PlaceableScenery(bHash32 rendermesh, const CollisionGeometry::Bounds *geometry, const Attrib::Collection *attribs,
                                   const ModelHeirarchy *heirarchy)
    : HeirarchyModel(rendermesh, geometry, UCrc32(0x9756df79), nullptr, attribs, heirarchy, 0, false), //
      IPlaceableScenery(this) {}

// speed.exe 1.3: 0x681410
PlaceableScenery *PlaceableScenery::Construct(const char *name, Attrib::Key attributes) {
    if (IModel::Count() > IModel::InstanceLimit || IPlaceableScenery::Count() > 12) {
        return nullptr;
    }
    unsigned int meshhash = bStringHashUpper(name);
    UCrc32 geomname(name);
    const ModelHeirarchy *heirarchy = FindSceneryHeirarchyByName(meshhash);
    const CollisionGeometry::Collection *geoms = CollisionGeometry::Lookup(geomname);
    if (geoms == nullptr || geoms->fNumBounds < 1 || geoms->GetRoot() == nullptr) {
        return nullptr;
    }
    eModel model;
    model.Init(meshhash);
    bHash32 rendermesh(model.HasSolid() ? meshhash : kMissingRenderMesh);
    Attrib::Gen::smackable attribs(attributes, 0, nullptr);
    return new PlaceableScenery(rendermesh, geoms->GetRoot(), attribs.GetConstCollection(), heirarchy);
}

// speed.exe 1.3: 0x66B3B0
void PlaceableScenery::PickUp() {
    ReleaseChildModels();
    StopEffects();
    EndDraw();
    EndSimulation();
    ReleaseSequencer();
    if (GetTrigger() != nullptr) {
        GetTrigger()->Disable();
    }
}

// speed.exe 1.3: 0x6776C0
bool PlaceableScenery::Place(const UMath::Matrix4 &transform, bool snap_to_ground) {
    PickUp();
    UMath::Matrix4 matrix = transform;
    if (snap_to_ground) {
        float height = 0.0f;
        WCollisionMgr collisionmgr(0, 3);
        if (!collisionmgr.GetWorldHeightAtPointRigorous(UMath::Vector4To3(matrix.v3), height, nullptr)) {
            return false;
        }
        UMath::Vector3 dim;
        GetDimension(dim);
        matrix.v3.y = height + dim.y;
    }
    PlaceTrigger(matrix, false);

    SmackableParams params(matrix, true, static_cast<IModel *>(this), false);
    if (ISimable::CreateInstance(UCrc32("Smackable"), params) != nullptr) {
        return true;
    }
    PickUp();
    return false;
}

// speed.exe 1.3: 0x6816A0
IPlaceableScenery *IPlaceableScenery::CreateInstance(const char *name, Attrib::Key attributes) {
    PlaceableScenery *scenery = PlaceableScenery::Construct(name, attributes);
    if (scenery != nullptr) {
        return scenery;
    }
    return nullptr;
}

// speed.exe 1.3 (0xF4 bytes): the model of a loaded SmokeableSpawner. Until
// smacked the scenery instance it overrides draws the prop.
class SceneryModel : public HeirarchyModel, public ISceneryModel {
  public:
    SceneryModel(SmokeableSpawner *spawner, const CollisionGeometry::Bounds *geometry, const Attrib::Collection *attribs, bool hidden);
    ~SceneryModel() override;

    static SceneryModel *Construct(SmokeableSpawner *data, const Attrib::Collection *attributes, bool hidden);

    static int SceneryCount() {
        return mSceneryCount;
    }

    void SetSpawner(SmokeableSpawner *spawner) {
        mSpawner = spawner;
    }

    // ISceneryModel
    bool GetSceneryTransform(UMath::Matrix4 &matrix) const override;
    void RestoreScene() override {
        InitScene();
    }
    unsigned int GetSpawnerID() const override {
        return mSpawner ? mSpawner->GetUniqueID() : 0xFFFFFFFF;
    }
    void WakeUp() override;
    bool IsExcluded(unsigned int scenery_exclusion_flag) const override {
        return mSpawner ? (mSpawner->GetExcludeFlags() & scenery_exclusion_flag) != 0 : false;
    }

    // IModel
    bool IsHidden() const override {
        return !mInstanceVisible && Sim::Model::IsHidden();
    }
    void HideModel() override;
    void GetTransform(UMath::Matrix4 &matrix) const override;
    void ReleaseModel() override;

  protected:
    void OnEndSimulation() override;
    void OnBeginDraw() override;

  private:
    void InitScene();
    void ShowInstance(bool show);

    bool mInstanceVisible;     // offset 0xEC, size 0x1
    SmokeableSpawner *mSpawner; // offset 0xF0, size 0x4

    static int mSceneryCount;
};

int SceneryModel::mSceneryCount = 0;

// speed.exe 1.3: 0x67BC20. A prop that was knocked over while its section was
// last loaded comes back hidden.
SceneryModel::SceneryModel(SmokeableSpawner *spawner, const CollisionGeometry::Bounds *geometry, const Attrib::Collection *attribs, bool hidden)
    : HeirarchyModel(spawner->GetRenderMesh(), geometry, UCrc32(0x9756df79), nullptr, attribs, spawner->GetRenderHeirarchy(), 0, false), //
      ISceneryModel(this),                                                                                                             //
      mInstanceVisible(true),                                                                                                          //
      mSpawner(spawner) {
    if (!hidden) {
        InitScene();
    } else {
        spawner->HideInstance();
        mInstanceVisible = false;
    }
    mSceneryCount++;
}

// speed.exe 1.3: 0x67BE90
SceneryModel::~SceneryModel() {
    if (mSpawner != nullptr) {
        mSpawner->SetSimModel(nullptr);
        ShowInstance(true);
    }
    mSceneryCount--;
}

// speed.exe 1.3: 0x681540
SceneryModel *SceneryModel::Construct(SmokeableSpawner *data, const Attrib::Collection *attributes, bool hidden) {
    if (mSceneryCount >= 0x800) {
        return nullptr;
    }
    const CollisionGeometry::Collection *geoms = CollisionGeometry::Lookup(data->GetCollisionName());
    if (geoms == nullptr || geoms->fNumBounds < 1 || attributes == nullptr) {
        return nullptr;
    }
    const CollisionGeometry::Bounds *root = geoms->GetRoot();
    if (root == nullptr) {
        return nullptr;
    }
    UMath::Vector3 dim;
    root->GetHalfDimensions(dim);
    if (UMath::LengthSquare(dim) <= 0.0f) {
        return nullptr;
    }
    return new SceneryModel(data, root, attributes, hidden);
}

// speed.exe 1.3: 0x672A30
void SceneryModel::ShowInstance(bool show) {
    if (mSpawner == nullptr || show == mInstanceVisible) {
        return;
    }
    if (show) {
        mSpawner->ShowInstance();
    } else {
        mSpawner->HideInstance();
    }
    mInstanceVisible = show;
}

// speed.exe 1.3: 0x6778F0. Back to resting in place: the scenery instance
// draws the prop again and the trigger waits around it.
void SceneryModel::InitScene() {
    ReleaseChildModels();
    StopEffects();
    EndDraw();
    EndSimulation();
    ReleaseSequencer();
    ShowInstance(true);
    SetCausality(nullptr, 0.0f);

    UMath::Matrix4 matrix;
    if (GetSceneryTransform(matrix)) {
        SetTrigger(matrix, true);
    } else {
        RemoveTrigger();
    }

    bool start_sequencer = mAttributes.start_sequencer();
    if (mAttributes.no_trigger()) {
        if (GetTrigger() != nullptr) {
            GetTrigger()->Disable();
        }
        StartAttribSequencer();
    } else if (start_sequencer) {
        StartAttribSequencer();
    }
    SetCameraAvoidable();
}

// speed.exe 1.3: 0x66B520. The spawner's placement, moved to the geometry's
// pivot.
bool SceneryModel::GetSceneryTransform(UMath::Matrix4 &matrix) const {
    const CollisionGeometry::Bounds *geometry = GetCollisionGeometry();
    if (geometry == nullptr) {
        return false;
    }
    UMath::Vector3 pivot;
    geometry->GetPivot(pivot);
    UMath::QuaternionToMatrix4(mSpawner->GetOrientation(), matrix);
    UMath::Vector3 offset;
    UMath::Rotate(pivot, matrix, offset);
    const UMath::Vector4 &position = mSpawner->GetPosition();
    matrix.v3.x = offset.x + position.x;
    matrix.v3.y = offset.y + position.y;
    matrix.v3.z = offset.z + position.z;
    return true;
}

// speed.exe 1.3: 0x66B600. Sets the prop off as if something hit it.
void SceneryModel::WakeUp() {
    if (GetTrigger() != nullptr && Sim::Model::IsHidden() && GetSimable() == nullptr) {
        GetTrigger()->Fire();
        GetTrigger()->Disable();
    }
}

// speed.exe 1.3: 0x6778B0
void SceneryModel::HideModel() {
    Sim::Model::HideModel();
    ShowInstance(false);
}

// speed.exe 1.3: 0x66B4A0
void SceneryModel::GetTransform(UMath::Matrix4 &matrix) const {
    if (!mInstanceVisible) {
        HeirarchyModel::GetTransform(matrix);
    } else if (!GetSceneryTransform(matrix)) {
        matrix = UMath::Matrix4::kIdentity;
    }
}

// speed.exe 1.3: 0x66B630. Scenery models live as long as their section; a
// released one just stops drawing and simulating (the instance stays hidden).
void SceneryModel::ReleaseModel() {
    EndDraw();
    EndSimulation();
    StopEffects();
    if (GetTrigger() != nullptr) {
        GetTrigger()->Disable();
    }
}

// speed.exe 1.3: 0x66B5E0
void SceneryModel::OnEndSimulation() {
    if (mAvoidable != nullptr) {
        mAvoidable->SetBody(static_cast<IBody *>(this));
    }
}

// speed.exe 1.3: 0x677830. Drawn by its render conn from now on.
void SceneryModel::OnBeginDraw() {
    StartAttribSequencer();
    SetOffScreenTimer(mAttributes.KILL_OFF_SCREEN());
    ShowInstance(false);
}

// Spawners

namespace {

const Attrib::Class *TheSmackableClass = nullptr;

SceneryOverrideInfo *SpawnerOverrideInfo(unsigned int override_info_number) {
    if (SceneryOverrideInfoTable == nullptr || static_cast<int>(override_info_number) >= NumSceneryOverrideInfos) {
        return nullptr;
    }
    return GetSceneryOverrideInfo(override_info_number);
}

const SceneryInfo *SpawnerSceneryInfo(unsigned int override_info_number) {
    SceneryOverrideInfo *override_info = SpawnerOverrideInfo(override_info_number);
    if (override_info == nullptr) {
        return nullptr;
    }
    ScenerySectionHeader *section_header = GetScenerySectionHeader(override_info->SectionNumber);
    if (section_header == nullptr) {
        return nullptr;
    }
    SceneryInstance *instance = section_header->GetSceneryInstance(override_info->InstanceNumber);
    if (instance == nullptr) {
        return nullptr;
    }
    return section_header->GetSceneryInfo(instance->SceneryInfoNumber);
}

} // namespace

// speed.exe 1.3: 0x66B690
void SmokeableSpawner::Init() {
    TheSmackableClass = Attrib::Database::Get().GetClass(Attrib::Gen::smackable::ClassKey());
}

// speed.exe 1.3: 0x66B670
const Attrib::Collection *SmokeableSpawner::FindAttributes(UCrc32 name) {
    if (TheSmackableClass == nullptr) {
        Init();
    }
    return TheSmackableClass ? TheSmackableClass->GetCollection(name.GetValue()) : nullptr;
}

void SmokeableSpawner::EndianSwap() {}

// speed.exe 1.3: 0x66B6E0
const ModelHeirarchy *SmokeableSpawner::GetRenderHeirarchy() const {
    const SceneryInfo *info = SpawnerSceneryInfo(mSceneryOverrideInfoNumber);
    return info ? static_cast<const ModelHeirarchy *>(info->mHeirarchy) : nullptr;
}

// speed.exe 1.3: 0x66B730. The first level of detail the scenery has.
bHash32 SmokeableSpawner::GetRenderMesh() const {
    const SceneryInfo *info = SpawnerSceneryInfo(mSceneryOverrideInfoNumber);
    if (info != nullptr) {
        for (int i = 0; i < 4; i++) {
            if (info->NameHash[i] != 0) {
                return bHash32(info->NameHash[i]);
            }
        }
    }
    return bHash32(kMissingRenderMesh);
}

// speed.exe 1.3: 0x66B7B0
void SmokeableSpawner::ShowInstance() const {
    SceneryOverrideInfo *override_info = SpawnerOverrideInfo(mSceneryOverrideInfoNumber);
    if (override_info != nullptr) {
        override_info->ExcludeFlags &= ~kSceneryOverrideHidden;
        override_info->AssignOverrides();
    }
}

// speed.exe 1.3: 0x66B7D0
bool SmokeableSpawner::IsInstanceVisible() const {
    SceneryOverrideInfo *override_info = SpawnerOverrideInfo(mSceneryOverrideInfoNumber);
    if (override_info != nullptr) {
        return (override_info->ExcludeFlags & kSceneryOverrideHidden) == 0;
    }
    return false;
}

void SmokeableSpawner::HideInstance() const {
    SceneryOverrideInfo *override_info = SpawnerOverrideInfo(mSceneryOverrideInfoNumber);
    if (override_info != nullptr) {
        override_info->ExcludeFlags |= kSceneryOverrideHidden;
        override_info->AssignOverrides();
    }
}

// speed.exe 1.3: inlined into 0x672B90 (the pack moved in memory)
void SmokeableSpawner::OnMoved() {
    if (mSimModel != nullptr) {
        mSimModel->SetSpawner(this);
    }
}

// speed.exe 1.3: 0x681620. Spawners excluded by the game mode or the world
// detail leave their prop out altogether.
void SmokeableSpawner::OnLoad(unsigned int exclude_flags, bool hidden) {
    if ((mExcludeFlags & exclude_flags) == 0) {
        ShowInstance();
        const Attrib::Collection *attribs = FindAttributes(mAttributes);
        mSimModel = attribs ? SceneryModel::Construct(this, attribs, hidden) : nullptr;
    }
    if (mSimModel == nullptr) {
        HideInstance();
    }
}

void SmokeableSpawner::OnUnload() {
    if (mSimModel != nullptr) {
        delete static_cast<SceneryModel *>(mSimModel);
        mSimModel = nullptr;
    }
}

// Prop timers: which props of a section were knocked over when it was last
// unloaded, so they stay down when it streams back in within 180 seconds (or
// at any time during a race).

namespace {

const float kPropRespawnTime = 180.0f; // speed.exe 1.3: 0x8A9330
const int kMaxSmokeableSections = 96;  // speed.exe 1.3: 0x6743A0
const unsigned int kSmokeableBits = 0x800;

// total size: 0x108
struct SmokeableSection {
    float mTime;
    int mSectionNumber;
    uint32 mBits[kSmokeableBits / 32];

    void Clear() {
        bMemSet(mBits, 0, sizeof(mBits));
    }

    bool Any() const {
        for (unsigned int i = 0; i < kSmokeableBits / 32; i++) {
            if (mBits[i] != 0) {
                return true;
            }
        }
        return false;
    }

    bool Test(unsigned int i) const {
        return i < kSmokeableBits && (mBits[i >> 5] & (1u << (i & 31))) != 0;
    }

    void Set(unsigned int i, bool on) {
        if (i >= kSmokeableBits) {
            return;
        }
        if (on) {
            mBits[i >> 5] |= 1u << (i & 31);
        } else {
            mBits[i >> 5] &= ~(1u << (i & 31));
        }
    }
};

// speed.exe 1.3: TheSmokeableSections (0x9261C0), a ring of 96 sections,
// newest last.
// TODO(port): the PC's FindOrAdd (0x67B890) pops the oldest entry when it
// finds one (looks like it means to move the found one to the front); this
// moves the found entry instead.
class SmokeableSections {
  public:
    void Reset() {
        mCount = 0;
    }

    SmokeableSection *Find(int section_number) {
        for (int i = mCount - 1; i >= 0; i--) {
            if (mEntries[i].mSectionNumber == section_number) {
                return &mEntries[i];
            }
        }
        return nullptr;
    }

    SmokeableSection *FindOrAdd(int section_number) {
        SmokeableSection entry;
        SmokeableSection *found = Find(section_number);
        if (found != nullptr) {
            entry = *found;
            Remove(static_cast<int>(found - mEntries));
        } else {
            entry.mTime = 0.0f;
            entry.mSectionNumber = section_number;
            entry.Clear();
            if (mCount == kMaxSmokeableSections) {
                Remove(0);
            }
        }
        mEntries[mCount] = entry;
        return &mEntries[mCount++];
    }

  private:
    void Remove(int index) {
        for (int i = index; i + 1 < mCount; i++) {
            mEntries[i] = mEntries[i + 1];
        }
        mCount--;
    }

    int mCount = 0;
    SmokeableSection mEntries[kMaxSmokeableSections];
};

SmokeableSections TheSmokeableSections;

// total size: 0x8010 (loaded in place)
struct SmokeableSpawnerPack {
    uint32 mNode[2];               // offset 0x0, size 0x8 (bTNode in the file)
    int16 ScenerySectionNumber;    // offset 0x8, size 0x2
    int16 FirstSmokeableSpawnerID; // offset 0xA, size 0x2
    int16 NumSmokeableSpawners;    // offset 0xC, size 0x2
    int8 EndianSwapped;            // offset 0xE, size 0x1
    int8 Pad;                      // offset 0xF, size 0x1
    SmokeableSpawner SmokeableSpawners[1]; // offset 0x10

    void OnLoad(unsigned int exclude_flags);
    void OnUnload();
    void OnMoved();
};

#if defined(EA_PORT_NATIVE)
static_assert(offsetof(SmokeableSpawnerPack, SmokeableSpawners) == 0x10, "SmokeableSpawnerPack is loaded in place");
#endif

// speed.exe 1.3: 0x6828D0
void SmokeableSpawnerPack::OnLoad(unsigned int exclude_flags) {
    SmokeableSection *section = TheSmokeableSections.Find(ScenerySectionNumber);
    bool racing = GRaceStatus::Exists() && GRaceStatus::Get().GetActivelyRacing();
    if (!racing && section != nullptr && section->mTime + kPropRespawnTime < Sim::GetTime()) {
        section->Clear();
        section = nullptr;
    }
    if (section == nullptr || !section->Any()) {
#ifndef EA_BUILD_A124
        if (GManager::Exists()) {
            GManager::Get().RestorePursuitBreakerIcons(ScenerySectionNumber);
        }
#endif
    }
    for (int i = 0; i < NumSmokeableSpawners; i++) {
        bool hidden = section != nullptr && section->Test(i);
        SmokeableSpawners[i].OnLoad(exclude_flags, hidden);
    }
}

// speed.exe 1.3: 0x67BF80
void SmokeableSpawnerPack::OnUnload() {
    SmokeableSection *section = TheSmokeableSections.FindOrAdd(ScenerySectionNumber);
    section->mTime = Sim::GetTime();
    for (int i = 0; i < NumSmokeableSpawners; i++) {
        SmokeableSpawner &spawner = SmokeableSpawners[i];
        section->Set(i, !spawner.IsInstanceVisible());
        spawner.OnUnload();
    }
}

// speed.exe 1.3: 0x672B90
void SmokeableSpawnerPack::OnMoved() {
    for (int i = 0; i < NumSmokeableSpawners; i++) {
        SmokeableSpawners[i].OnMoved();
    }
}

// speed.exe 1.3: 0x6829D0. The exclude flags drop spawners that don't belong
// in split screen (1), a race (4) or the current world detail (0x80000 low,
// 0x40000 high; see the scenery cull in Scenery.cpp).
int LoaderSmokeableSpawners(bChunk *chunk) {
    if (chunk->GetID() != BCHUNK_SPEED_SMOKEABLE_SPAWNER) {
        return 0;
    }
    SmokeableSpawnerPack *pack = reinterpret_cast<SmokeableSpawnerPack *>(chunk->GetAlignedData(16));
    if (GetChunkMovementOffset() != 0) {
        pack->OnMoved();
        return 1;
    }
    if (!pack->EndianSwapped) {
        pack->EndianSwapped = 1;
    }
    if (Sim::Exists()) {
        unsigned int exclude_flags = Sim::GetUserMode() == Sim::USER_SPLIT_SCREEN ? 1 : 0;
        if (GRaceStatus::Exists() && GRaceStatus::Get().GetPlayMode() == GRaceStatus::kPlayMode_Racing) {
            exclude_flags |= 4;
        }
#if defined(EA_PLATFORM_WIN32)
        const bool low_world_detail = gPCGraphicsOptions.WorldLodLevel < 3;
#else
        const bool low_world_detail = false;
#endif
        exclude_flags |= low_world_detail ? 0x80000 : 0x40000;
        pack->OnLoad(exclude_flags);
    }
    return 1;
}

// speed.exe 1.3: 0x67C040
int UnloaderSmokeableSpawners(bChunk *chunk) {
    if (chunk->GetID() != BCHUNK_SPEED_SMOKEABLE_SPAWNER) {
        return 0;
    }
    if (GetChunkMovementOffset() == 0 && Sim::Exists()) {
        SmokeableSpawnerPack *pack = reinterpret_cast<SmokeableSpawnerPack *>(chunk->GetAlignedData(16));
        pack->OnUnload();
    }
    return 1;
}

} // namespace

bChunkLoader bChunkLoaderSmokeableSpawners(BCHUNK_SPEED_SMOKEABLE_SPAWNER, LoaderSmokeableSpawners, UnloaderSmokeableSpawners);

// speed.exe 1.3: 0x672200
void ResetPropTimers() {
    TheSmokeableSections.Reset();
}
