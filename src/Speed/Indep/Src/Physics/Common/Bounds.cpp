#include "Speed/Indep/Src/Physics/Bounds.h"
#include "Speed/Indep/Src/Physics/Behaviors/RigidBody.h"
#include "Speed/Indep/Src/Physics/Dynamics/Articulation.h"
#include "Speed/Indep/Libs/Support/Utility/FastMem.h"
#include "Speed/Indep/Libs/Support/Utility/UMath.h"
#include "Speed/Indep/Libs/Support/Utility/UStandard.h"
#include "Speed/Indep/Src/Sim/SimSurface.h"
#include "Speed/Indep/bWare/Inc/bChunk.hpp"
#include "Speed/Indep/bWare/Inc/bList.hpp"

DECLARE_CONTAINER_TYPE(CollisionBoundsTable);

namespace CollisionGeometry {

// One loaded bounds chunk: every collection in it, sorted by name.
// total size: 0x1C
class BoundsPack : public bTNode<BoundsPack> {
  public:
    // total size: 0x8
    struct Pair {
        Pair(UCrc32 key, Collection *col) : mKey(key), mCollection(col) {}

        bool operator<(const Pair &rhs) const {
            return mKey.GetValue() < rhs.mKey.GetValue();
        }

        UCrc32 mKey;             // offset 0x0, size 0x4
        Collection *mCollection; // offset 0x4, size 0x4
    };

    // total size: 0x10
    struct Table : public UTL::Std::vector<Pair, _type_CollisionBoundsTable> {
        Collection *Find(UCrc32 key) const {
            const_iterator iter = std::lower_bound(begin(), end(), Pair(key, nullptr));
            if (iter != end() && iter->mKey == key && iter->mCollection) {
                return iter->mCollection;
            }
            return nullptr;
        }

        void Add(Collection *col) {
            Pair pair(col->fNameHash, col);
            insert(std::upper_bound(begin(), end(), pair), pair);
        }
    };

    void *operator new(std::size_t size) {
        return gFastMem.Alloc(size, nullptr);
    }

    void operator delete(void *mem, std::size_t size) {
        if (mem) {
            gFastMem.Free(mem, size, nullptr);
        }
    }

    BoundsPack(bChunk *chunk);

    Collection *Find(UCrc32 key) const {
        return mTable.Find(key);
    }

    bChunk *GetHeader() const {
        return mChunk;
    }

  private:
    bChunk *mChunk; // offset 0x8, size 0x4
    Table mTable;   // offset 0xC, size 0x10
};

// total size: 0x8
class Collections : public bTList<BoundsPack> {
  public:
    Collection *Find(UCrc32 key);
    BoundsPack *Find(const bChunk *chunk);
};

static Collections TheCollections;

// speed.exe 1.3: 0x6870E0
BoundsPack::BoundsPack(bChunk *chunk) : mChunk(chunk) {
    bChunk *first = chunk->GetFirstChunk();
    bChunk *last = chunk->GetLastChunk();
    int count = 0;
    for (bChunk *c = first; c < last; c = c->GetNext()) {
        count++;
    }
    mTable.reserve(count);

    for (bChunk *c = first; c < last; c = c->GetNext()) {
        Collection *col = reinterpret_cast<Collection *>(c->GetAlignedData(16));
        if (!mTable.Find(col->fNameHash)) {
            col->Init();
            mTable.Add(col);
        }
    }
}

// speed.exe 1.3: 0x681340
Collection *Collections::Find(UCrc32 key) {
    for (BoundsPack *pack = GetHead(); pack != EndOfList(); pack = pack->GetNext()) {
        Collection *col = pack->Find(key);
        if (col) {
            return col;
        }
    }
    return nullptr;
}

BoundsPack *Collections::Find(const bChunk *chunk) {
    for (BoundsPack *pack = GetHead(); pack != EndOfList(); pack = pack->GetNext()) {
        if (pack->GetHeader() == chunk) {
            return pack;
        }
    }
    return nullptr;
}

// speed.exe 1.3: 0x66A7D0
// Resolves the point cloud vertex pointers (each cloud's vertices follow its
// header) and points every node back at its collection.
void Collection::Init() {
    if (!fIsResolved) {
        fIsResolved = 1;
    }
    Bounds *bounds = reinterpret_cast<Bounds *>(this + 1);
    PCloudHeader *header = reinterpret_cast<PCloudHeader *>(&bounds[fNumBounds]);
    PCloud *cloud = reinterpret_cast<PCloud *>(header + 1);
    for (int i = 0; i < header->fNumPClouds; i++) {
        UMath::Vector4 *verts = reinterpret_cast<UMath::Vector4 *>(cloud + 1);
        cloud->fPList = verts;
        cloud = reinterpret_cast<PCloud *>(verts + cloud->fNumVerts);
    }
    for (int i = 0; i < fNumBounds; i++) {
        bounds[i].fCollection = this;
    }
}

// speed.exe 1.3: 0x66A690
Bounds *const Collection::GetRoot() const {
    if (fNumBounds > 0) {
        return const_cast<Bounds *>(reinterpret_cast<const Bounds *>(this + 1));
    }
    return nullptr;
}

// GC 0x8021DFCC
const Bounds *Collection::GetChild(const Bounds *parent, unsigned int idx) const {
    if (parent->fChildIndex >= 0 && idx < parent->fNumChildren) {
        return &reinterpret_cast<const Bounds *>(this + 1)[parent->fChildIndex + idx];
    }
    return nullptr;
}

// speed.exe 1.3: 0x66A6A0
const Bounds *Collection::GetChild(const Bounds *parent, UCrc32 name) const {
    if (parent->fChildIndex >= 0) {
        const Bounds *child = &reinterpret_cast<const Bounds *>(this + 1)[parent->fChildIndex];
        for (int i = 0; i < parent->fNumChildren; i++, child++) {
            if (child->fNameHash == name) {
                return child;
            }
        }
    }
    return nullptr;
}

// speed.exe 1.3: 0x66A730
const PCloud *Collection::GetPointCloud(const Bounds *geom) const {
    const Bounds *bounds = reinterpret_cast<const Bounds *>(this + 1);
    const PCloudHeader *header = reinterpret_cast<const PCloudHeader *>(&bounds[fNumBounds]);
    if (header->fNumPClouds > 0 && geom->fPCloudIndex >= 0) {
        const PCloud *cloud = reinterpret_cast<const PCloud *>(header + 1);
        for (int i = 0; i < header->fNumPClouds; i++) {
            if (i == geom->fPCloudIndex) {
                return cloud;
            }
            cloud = reinterpret_cast<const PCloud *>(cloud->fPList.Get() + cloud->fNumVerts);
        }
    }
    return nullptr;
}

// speed.exe 1.3: 0x66A780
Bounds *const Collection::GetBounds(UCrc32 hash_name) const {
    if (hash_name != UCrc32::kNull) {
        Bounds *bounds = const_cast<Bounds *>(reinterpret_cast<const Bounds *>(this + 1));
        for (int i = 0; i < fNumBounds; i++) {
            if (bounds[i].fNameHash == hash_name) {
                return &bounds[i];
            }
        }
    }
    return nullptr;
}

// speed.exe 1.3: 0x672050
bool Collection::AddTo(IBoundable *irbc, const Bounds *root, const SimSurface &defsurface, bool parsechildren) const {
    bool result = false;
    if (!root) {
        return false;
    }
    if (parsechildren && root->fNumChildren != 0) {
        for (unsigned int i = 0; i < root->fNumChildren; i++) {
            if (AddNode(irbc, GetChild(root, i), defsurface, true)) {
                result = true;
            }
        }
        if (result) {
            return result;
        }
    }
    return AddNode(irbc, root, defsurface, false);
}

// speed.exe 1.3: 0x66A890
// A root node becomes the body's frame: its primitive sits at the origin and
// its point cloud is brought into that frame. Children keep their offsets.
bool Collection::AddNode(IBoundable *irbc, const Bounds *geom, const SimSurface &defsurface, bool child) const {
    UMath::Vector3 dim;
    UMath::Vector3 position;
    UMath::Vector4 orient;
    UMath::Matrix4 tolocal = UMath::Matrix4::kIdentity;
    bool result = false;

    geom->GetHalfDimensions(dim);
    geom->fPosition.Decompress(position);
    geom->GetOrientation(orient);

    SimSurface material(defsurface);
    if (geom->fSurface.GetValue() != 0) {
        material.Change(SimSurface(SimSurface::Lookup(geom->fSurface)).GetConstCollection());
        if (material.GetConstCollection() == SimSurface::kNull.GetConstCollection()) {
            material.Change(defsurface.GetConstCollection());
        }
    }

    if (child) {
        if (geom->fFlags & kBounds_Internal) {
            return false;
        }
    } else {
        UMath::QuaternionToMatrix4(orient, tolocal);
        tolocal.v3.x = position.x;
        tolocal.v3.y = position.y;
        tolocal.v3.z = position.z;
        tolocal.v3.w = 1.0f;
        OrthoInverse(tolocal);
        position = UMath::Vector3::kZero;
        orient = UMath::Vector4::kIdentity;
    }

    if ((geom->fFlags & (kBounds_PrimVsWorld | kBounds_PrimVsObjects | kBounds_PrimVsGround)) &&
        irbc->AddCollisionPrimitive(geom->fNameHash, dim, geom->fRadius, position, material, orient,
                                    static_cast<BoundFlags>(geom->fFlags))) {
        result = true;
    }

    if (geom->fFlags & kBounds_MeshVsGround) {
        const PCloud *cloud = GetPointCloud(geom);
        if (cloud && cloud->fNumVerts > 0) {
            int count = cloud->fNumVerts;
            if (!child) {
                UMath::Vector4 verts[16];
                if (count > 16) {
                    count = 16;
                }
                const UMath::Vector4 *src = cloud->fPList;
                for (int i = 0; i < count; i++) {
                    UMath::Vector4 p = src[i];
                    p.w = 1.0f;
                    verts[i].x = p.x * tolocal.v0.x + p.y * tolocal.v1.x + p.z * tolocal.v2.x + p.w * tolocal.v3.x;
                    verts[i].y = p.x * tolocal.v0.y + p.y * tolocal.v1.y + p.z * tolocal.v2.y + p.w * tolocal.v3.y;
                    verts[i].z = p.x * tolocal.v0.z + p.y * tolocal.v1.z + p.z * tolocal.v2.z + p.w * tolocal.v3.z;
                    verts[i].w = p.x * tolocal.v0.w + p.y * tolocal.v1.w + p.z * tolocal.v2.w + p.w * tolocal.v3.w;
                }
                irbc->AddCollisionMesh(geom->fNameHash, verts, count, material, static_cast<BoundFlags>(geom->fFlags), false);
            } else {
                irbc->AddCollisionMesh(geom->fNameHash, cloud->fPList, count, material, static_cast<BoundFlags>(geom->fFlags), true);
            }
            result = true;
        }
    }
    return result;
}

// speed.exe 1.3: 0x6813B0
const Collection *Lookup(UCrc32 object_name_hash) {
    return TheCollections.Find(object_name_hash);
}

// speed.exe 1.3: 0x6718F0
// Joins two bodies at matching joint nodes of their collision geometry: the
// female node (kBounds_Joint_Female) on ifemale and the male node
// (kBounds_Joint_Male) on imale. Constraint children of the female node are
// paired with the male node's post children of the same name.
bool CreateJoint(IBoundable *ifemale, UCrc32 femalenode_name, IBoundable *imale, UCrc32 malenode_name, UMath::Vector3 *out_female,
                 UMath::Vector3 *out_male, unsigned int joint_flags) {
    const Bounds *female_root = ifemale->GetGeometryNode();
    const Bounds *male_root = imale->GetGeometryNode();
    if (!female_root || !male_root) {
        return false;
    }
    IDynamicsEntity *female_entity;
    if (!ifemale->QueryInterface(&female_entity)) {
        return false;
    }
    IDynamicsEntity *male_entity;
    if (!imale->QueryInterface(&male_entity)) {
        return false;
    }

    for (unsigned int i = 0; i < male_root->fNumChildren; i++) {
        const Bounds *male_node = male_root->GetChild(i);
        if (male_node->fNameHash != malenode_name || !(male_node->fFlags & kBounds_Joint_Male)) {
            continue;
        }

        const Bounds *female_node = nullptr;
        for (unsigned int j = 0; j < female_root->fNumChildren; j++) {
            const Bounds *node = female_root->GetChild(j);
            if (node->fNameHash == femalenode_name && (node->fFlags & kBounds_Joint_Female)) {
                female_node = node;
                break;
            }
        }
        if (!female_node) {
            return false;
        }

        UMath::Vector3 male_arm;
        UMath::Vector3 female_arm;
        male_node->fPosition.Decompress(male_arm);
        female_node->fPosition.Decompress(female_arm);
        if (out_female) {
            *out_female = female_arm;
        }
        if (out_male) {
            *out_male = male_arm;
        }

        Dynamics::IEntity *female = female_entity;
        Dynamics::IEntity *male = male_entity;
        Dynamics::Articulation::HJOINT hJoint =
            Dynamics::Articulation::Create(female, female_arm, male, male_arm, static_cast<Dynamics::Articulation::eJointFlags>(joint_flags));
        if (!hJoint) {
            return false;
        }

        // geometry space (z up) to body space (y up); a static in speed.exe (0x92C718)
        UMath::Matrix4 zup;
        zup.v0 = UMath::Vector4Make(1.0f, 0.0f, 0.0f, 0.0f);
        zup.v1 = UMath::Vector4Make(0.0f, 0.0f, -1.0f, 0.0f);
        zup.v2 = UMath::Vector4Make(0.0f, 1.0f, 0.0f, 0.0f);
        zup.v3 = UMath::Vector4Make(0.0f, 0.0f, 0.0f, 1.0f);

        for (unsigned int c = 0; c < female_node->fNumChildren; c++) {
            const Bounds *constraint = female_node->GetChild(c);
            if (!(constraint->fFlags & (kBounds_Constraint_Conical | kBounds_Constraint_Prismatic))) {
                continue;
            }
            for (unsigned int p = 0; p < male_node->fNumChildren; p++) {
                const Bounds *post = male_node->GetChild(p);
                if (!(post->fFlags & kBounds_Male_Post) || post->fNameHash != constraint->fNameHash) {
                    continue;
                }

                UMath::Vector3 cdim;
                constraint->GetHalfDimensions(cdim);

                UMath::Vector4 q0;
                UMath::Vector4 q1;
                UMath::Vector4 q;
                constraint->GetOrientation(q0);
                female_node->GetOrientation(q1);
                VU0_qmul(q0, q1, q);
                UMath::Matrix4 local;
                UMath::Matrix4 orient;
                UMath::QuaternionToMatrix4(q, local);
                UMath::Mult(zup, local, orient);

                UMath::Vector3 pdim;
                post->GetHalfDimensions(pdim);
                post->GetOrientation(q0);
                male_node->GetOrientation(q1);
                VU0_qmul(q0, q1, q);
                UMath::Matrix4 postorient;
                UMath::QuaternionToMatrix4(q, local);
                UMath::Mult(zup, local, postorient);

                Dynamics::Articulation::eConstraint type;
                if (constraint->fFlags & kBounds_Constraint_Conical) {
                    type = Dynamics::Articulation::CONICAL;
                } else if (constraint->fFlags & kBounds_Constraint_Prismatic) {
                    type = Dynamics::Articulation::PRISMATIC;
                } else {
                    break;
                }
                float length = pdim.y + pdim.y;
                UMath::Vector3 postvec;
                postvec.x = postorient.v2.x * length;
                postvec.y = postorient.v2.y * length;
                postvec.z = postorient.v2.z * length;
                float theta = VU0_Atan2(cdim.x, cdim.y + cdim.y) * 360.0f;
                Dynamics::Articulation::Constrain(hJoint, female, orient, theta, theta, postvec, type);
                break;
            }
        }
        return true;
    }
    return false;
}

#if defined(EA_PORT_NATIVE)
// Interface defaults; never called (every IBoundable overrides them), but they
// anchor the vtable.
const Bounds *IBoundable::GetGeometryNode() const {
    return nullptr;
}

bool IBoundable::AddCollisionPrimitive(UCrc32 name, const UMath::Vector3 &dim, float radius, const UMath::Vector3 &offset, const SimSurface &material,
                                       const UMath::Vector4 &orient, BoundFlags boundFlags) {
    return false;
}

bool IBoundable::AddCollisionMesh(UCrc32 name, const UMath::Vector4 *verts, unsigned int count, const SimSurface &material, BoundFlags flags,
                                  bool persistant) {
    return false;
}
#endif

}; // namespace CollisionGeometry

// speed.exe 1.3: 0x687240
int LoaderBounds(bChunk *chunk) {
    if (chunk->GetID() != 0x8003B900) {
        return 0;
    }
    CollisionGeometry::TheCollections.AddHead(new CollisionGeometry::BoundsPack(chunk));
    return 1;
}

// speed.exe 1.3: 0x683570
int UnloaderBounds(bChunk *chunk) {
    if (chunk->GetID() != 0x8003B900) {
        return 0;
    }
    CollisionGeometry::BoundsPack *pack = CollisionGeometry::TheCollections.Find(chunk);
    if (pack) {
        pack->Remove();
        delete pack;
    }
    return 1;
}
