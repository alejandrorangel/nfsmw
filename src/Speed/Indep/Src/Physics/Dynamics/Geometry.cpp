// Dynamics::Collision::Geometry -- oriented box / sphere intersection tests.
// Reconstructed from speed.exe 1.3 (PC) for the native port; names follow the
// PS2 debug info (Geometry.cpp locals).

#include "Speed/Indep/Src/Physics/Dynamics/Collision.h"

#include <math.h>

namespace Dynamics {
namespace Collision {

namespace {

const float kGeomEpsilon = 1.0e-6f; // PC 0x8953F4

// UMath::Vector4::kIdentity / kZero (PC 0x8A2FD0 / 0x8A2FC0). Local copies:
// the UMath constants aren't defined in the native build yet (zero stubs).
// TODO(port): use the UMath constants once they are defined.
const UMath::Vector4 kGeomVector4Identity = {0.0f, 0.0f, 0.0f, 1.0f};
const UMath::Vector4 kGeomVector4Zero = {0.0f, 0.0f, 0.0f, 0.0f};

inline float Dot3(const UMath::Vector4 &a, const UMath::Vector4 &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// PC 0x40FEB0 (UMath::Unitxyz): r.xyz = a.xyz / |a.xyz|, zero if |a| == 0; w untouched.
inline void GeomUnitxyz(const UMath::Vector4 &a, UMath::Vector4 &r) {
    float len = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    float s = len != 0.0f ? 1.0f / len : 0.0f;
    r.x = s * a.x;
    r.y = s * a.y;
    r.z = s * a.z;
}

// PC 0x4875B0 (inlined helper of BoxVsBox): intersects segment P1->P2 with the
// plane (PtOnPlane, normal). Writes the xyz of the intersection on success.
bool SegmentIntersectsPlane(const UMath::Vector4 &P1, const UMath::Vector4 &P2, const UMath::Vector4 &normal,
                            const UMath::Vector4 &PtOnPlane, UMath::Vector4 &intersection) {
    UMath::Vector4 PtOnPlanemP1;
    UMath::Vector4 P2mP1;

    PtOnPlanemP1.x = PtOnPlane.x - P1.x;
    PtOnPlanemP1.y = PtOnPlane.y - P1.y;
    PtOnPlanemP1.z = PtOnPlane.z - P1.z;
    P2mP1.x = P2.x - P1.x;
    P2mP1.y = P2.y - P1.y;
    P2mP1.z = P2.z - P1.z;

    float denom = Dot3(P2mP1, normal);
    if (!(fabsf(denom) > kGeomEpsilon)) {
        return false;
    }
    float t = Dot3(PtOnPlanemP1, normal) / denom;
    if (t < 0.0f || t > 1.0f) {
        return false;
    }
    intersection.x = t * (P2.x - P1.x) + P1.x;
    intersection.y = t * (P2.y - P1.y) + P1.y;
    intersection.z = t * (P2.z - P1.z) + P1.z;
    return true;
}

// PC 0x487500 (inlined helper of SphereVsBox): distance along the unit
// direction dir from P (inside the sphere C/r) to the sphere surface, 0 if P
// is not inside.
float RayExitSphere(const UMath::Vector4 &C, float r, const UMath::Vector4 &P, const UMath::Vector4 &dir) {
    UMath::Vector4 Pc;
    Pc.x = P.x - C.x;
    Pc.y = P.y - C.y;
    Pc.z = P.z - C.z;

    float b = (Pc.x * dir.x + Pc.y * dir.y + Pc.z * dir.z);
    b = b + b;
    float c = (Pc.x * Pc.x + Pc.y * Pc.y + Pc.z * Pc.z) - r * r;
    if (!(c < 0.0f)) {
        return 0.0f;
    }
    float q = b * b - c * 4.0f;
    if (q < 0.0f) {
        return 0.0f;
    }
    return (sqrtf(q) - b) * 0.5f;
}

} // namespace

// PC 0x487690
Geometry::Geometry() {
    mShape = UNKNOWN;
}

// PC 0x48B5A0
Geometry::Geometry(const UMath::Matrix4 &orient, const UMath::Vector3 &position, const UMath::Vector3 &dimension, Shape shape,
                   const UMath::Vector3 &delta) {
    Set(orient, position, dimension, shape, delta);
}

// PC 0x4876A0
void Geometry::Move(const UMath::Vector3 &deltaP) {
    mPosition.x = deltaP.x + mPosition.x;
    mPosition.y = mPosition.y + deltaP.y;
    mPosition.z = mPosition.z + deltaP.z;
    mDelta.x = mDelta.x + deltaP.x;
    mDelta.y = mDelta.y + deltaP.y;
    mDelta.z = mDelta.z + deltaP.z;
}

// PC 0x4876F0
void Geometry::Set(const UMath::Matrix4 &orient, const UMath::Vector3 &position, const UMath::Vector3 &dimension, Shape shape,
                   const UMath::Vector3 &delta) {
    mShape = static_cast<unsigned short>(shape);
    if (shape == BOX) {
        mPosition.x = position.x;
        mPosition.y = position.y;
        mPosition.z = position.z;
        mDimension[0] = dimension.x;
        mDimension[1] = dimension.y;
        mDimension[2] = dimension.z;
        mDelta = delta;
        for (unsigned int i = 0; i < 3; i++) {
            const UMath::Vector4 &row = (&orient.v0)[i];
            float s = mDimension[i];
            mNormal[i].x = row.x;
            mNormal[i].y = row.y;
            mNormal[i].z = row.z;
            mExtent[i].x = s * mNormal[i].x;
            mExtent[i].y = s * mNormal[i].y;
            mExtent[i].z = s * mNormal[i].z;
        }
    } else if (shape == SPHERE) {
        for (int index = 0; index < 3; index++) {
            mNormal[index] = kGeomVector4Identity;
        }
        for (int index = 0; index < 3; index++) {
            mExtent[index] = kGeomVector4Identity;
        }
        UMath::Vector4 res;
        res.x = position.x;
        res.y = position.y;
        res.z = position.z;
        res.w = 1.0f;
        mPosition = res;

        float a = dimension.z;
        if (dimension.y > a) {
            a = dimension.y;
        }
        if (dimension.x > a) {
            a = dimension.x;
        }
        mDimension[2] = a;
        mDimension[1] = a;
        mDimension[0] = a;
        mDelta = delta;
    } else {
        mShape = UNKNOWN;
    }
    mCollision_normal.w = 0.0f;
    mCollision_point.w = 0.0f;
    mPenetratesOther = 0;
    mOverlap = -100000.0f;
}

// PC 0x4879B0
bool Geometry::BoxVsBox(const Geometry *GeometryA, const Geometry *GeometryB, Geometry *result) {
    const Geometry *a = GeometryA;
    const Geometry *b = GeometryB;
    const Geometry *penetrated = nullptr;
    float best_interval = -100000.0f;
    int best_normal_index = -1;
    UMath::Vector4 best_penetrated_normal = kGeomVector4Identity;
    int index_map[3] = {0, 2, 1};

    for (int cycle = 0; cycle < 2; cycle++) {
        if (cycle == 1) {
            a = GeometryB;
            b = GeometryA;
        }

        UMath::Vector4 a_vel;
        a_vel.x = a->mDelta.x - b->mDelta.x;
        a_vel.y = a->mDelta.y - b->mDelta.y;
        a_vel.z = a->mDelta.z - b->mDelta.z;

        UMath::Vector4 rel_position;
        rel_position.x = a->mPosition.x - b->mPosition.x;
        rel_position.y = a->mPosition.y - b->mPosition.y;
        rel_position.z = a->mPosition.z - b->mPosition.z;

        UMath::Vector4 rel_position_prev;
        rel_position_prev.x = rel_position.x - a_vel.x;
        rel_position_prev.y = rel_position.y - a_vel.y;
        rel_position_prev.z = rel_position.z - a_vel.z;

        for (int a_lp = 0; a_lp < 3; a_lp++) {
            int penetrated_index = index_map[a_lp];
            UMath::Vector4 penetrated_normal = a->mNormal[penetrated_index];
            UMath::Vector4 collision_point = b->mPosition;

            if (Dot3(rel_position_prev, penetrated_normal) < 0.0f) {
                penetrated_normal.x = -penetrated_normal.x;
                penetrated_normal.y = -penetrated_normal.y;
                penetrated_normal.z = -penetrated_normal.z;
            }

            float projected_interval = Dot3(rel_position, penetrated_normal) - a->mDimension[penetrated_index];

            for (int b_normal_index = 0; b_normal_index < 3; b_normal_index++) {
                const UMath::Vector4 *b_extent = &b->mExtent[b_normal_index];
                float result = Dot3(penetrated_normal, *b_extent);
                projected_interval = projected_interval - fabsf(result);
                if (result < 0.0f) {
                    collision_point.x -= b_extent->x;
                    collision_point.y -= b_extent->y;
                    collision_point.z -= b_extent->z;
                } else if (result > 0.0f) {
                    collision_point.x += b_extent->x;
                    collision_point.y += b_extent->y;
                    collision_point.z += b_extent->z;
                }
            }

            if (projected_interval > 0.0f) {
                return false;
            }

            float interval = projected_interval - Dot3(a_vel, penetrated_normal);
            if (interval > best_interval) {
                best_interval = interval;
                result->mOverlap = projected_interval;
                result->mCollision_point = collision_point;
                best_penetrated_normal = penetrated_normal;
                best_normal_index = penetrated_index;
                penetrated = a;
                if (result != a) {
                    result->mPenetratesOther = 1;
                    result->mCollision_normal.x = -penetrated_normal.x;
                    result->mCollision_normal.y = -penetrated_normal.y;
                    result->mCollision_normal.z = -penetrated_normal.z;
                } else {
                    result->mCollision_normal.x = penetrated_normal.x;
                    result->mCollision_normal.y = penetrated_normal.y;
                    result->mCollision_normal.z = penetrated_normal.z;
                    result->mPenetratesOther = 0;
                }
            }
        }
    }

    if (penetrated == nullptr || best_normal_index < 0) {
        return false;
    }

    // The penetration axis was found; now verify the contact against the two
    // faces adjoining the penetrated face, clipping the penetrator's edges.
    int joinedfaces[3][2] = {{1, 2}, {0, 2}, {0, 1}};
    const Geometry *penetrator = penetrated == GeometryA ? GeometryB : GeometryA;
    UMath::Vector4 &cpoint = result->mCollision_point;

    UMath::Vector4 p_vel;
    p_vel.x = penetrated->mDelta.x - penetrator->mDelta.x;
    p_vel.y = penetrated->mDelta.y - penetrator->mDelta.y;
    p_vel.z = penetrated->mDelta.z - penetrator->mDelta.z;

    UMath::Vector4 r_cpoint;
    r_cpoint.x = cpoint.x - penetrated->mPosition.x;
    r_cpoint.y = cpoint.y - penetrated->mPosition.y;
    r_cpoint.z = cpoint.z - penetrated->mPosition.z;

    UMath::Vector4 r_cpoint_prev;
    r_cpoint_prev.x = r_cpoint.x + p_vel.x;
    r_cpoint_prev.y = r_cpoint.y + p_vel.y;
    r_cpoint_prev.z = r_cpoint.z + p_vel.z;

    int passed = 0;
    for (int pi = 0; pi < 2; pi++) {
        int p_normal_index = joinedfaces[best_normal_index][pi];
        UMath::Vector4 p_normal = penetrated->mNormal[p_normal_index];
        if (Dot3(r_cpoint_prev, p_normal) < 0.0f) {
            p_normal.x = p_normal.x * -1.0f;
            p_normal.y = p_normal.y * -1.0f;
            p_normal.z = p_normal.z * -1.0f;
        }

        float dim = penetrated->mDimension[p_normal_index];
        if (!(Dot3(p_normal, r_cpoint) > dim)) {
            passed++;
            continue;
        }

        UMath::Vector4 plane_point;
        plane_point.x = p_normal.x * dim + penetrated->mPosition.x;
        plane_point.y = p_normal.y * dim + penetrated->mPosition.y;
        plane_point.z = p_normal.z * dim + penetrated->mPosition.z;

        UMath::Vector4 arm;
        arm.x = cpoint.x - penetrator->mPosition.x;
        arm.y = cpoint.y - penetrator->mPosition.y;
        arm.z = cpoint.z - penetrator->mPosition.z;

        for (int edge_index = 0; edge_index < 3; edge_index++) {
            const UMath::Vector4 &edge_normal = penetrator->mNormal[edge_index];
            float edge_len = Dot3(arm, edge_normal);
            edge_len = -(edge_len + edge_len);

            UMath::Vector4 edge_start;
            edge_start.x = edge_len * edge_normal.x + cpoint.x;
            edge_start.y = edge_len * edge_normal.y + cpoint.y;
            edge_start.z = edge_len * edge_normal.z + cpoint.z;
            edge_start.w = cpoint.w;

            UMath::Vector4 edge_intersection = cpoint;
            if (!SegmentIntersectsPlane(edge_start, cpoint, p_normal, plane_point, edge_intersection)) {
                continue;
            }

            UMath::Vector4 r_edgepoint;
            r_edgepoint.x = edge_intersection.x - penetrated->mPosition.x;
            r_edgepoint.y = edge_intersection.y - penetrated->mPosition.y;
            r_edgepoint.z = edge_intersection.z - penetrated->mPosition.z;

            float best_dist = -Dot3(r_edgepoint, best_penetrated_normal);
            float overlap = best_dist - penetrated->mDimension[best_normal_index];
            if (!(overlap < 0.0f)) {
                continue;
            }

            UMath::Vector4 r_edgepoint_prev;
            r_edgepoint_prev.x = r_edgepoint.x + p_vel.x;
            r_edgepoint_prev.y = r_edgepoint.y + p_vel.y;
            r_edgepoint_prev.z = r_edgepoint.z + p_vel.z;

            int fi;
            for (fi = 0; fi < 2; fi++) {
                int face = joinedfaces[p_normal_index][fi];
                float dist;
                if (face == best_normal_index) {
                    dist = best_dist;
                } else {
                    const UMath::Vector4 &fn = penetrated->mNormal[face];
                    dist = Dot3(r_edgepoint, fn);
                    if (Dot3(r_edgepoint_prev, fn) < 0.0f) {
                        dist = dist * -1.0f;
                    }
                }
                if (dist > penetrated->mDimension[face]) {
                    break;
                }
            }

            if (fi == 2) {
                result->mOverlap = overlap;
                cpoint = edge_intersection;
                return true;
            }
        }
    }

    return passed == 2;
}

// PC 0x48B5D0
bool Geometry::SphereVsBox(const Geometry *sphereA, const Geometry *boxB, Geometry *result) {
    UMath::Vector4 sphere_vel;
    sphere_vel.x = sphereA->mDelta.x - boxB->mDelta.x;
    sphere_vel.y = sphereA->mDelta.y - boxB->mDelta.y;
    sphere_vel.z = sphereA->mDelta.z - boxB->mDelta.z;

    float radius = sphereA->mDimension[0];

    UMath::Vector4 sphere_rpos;
    sphere_rpos.x = sphereA->mPosition.x - boxB->mPosition.x;
    sphere_rpos.y = sphereA->mPosition.y - boxB->mPosition.y;
    sphere_rpos.z = sphereA->mPosition.z - boxB->mPosition.z;

    result->mCollision_point = kGeomVector4Identity;
    result->mCollision_normal = kGeomVector4Identity;
    UMath::Vector4 &normal = result->mCollision_normal;
    UMath::Vector4 &collision_point = result->mCollision_point;

    UMath::Vector4 sphere_rpos_prev;
    sphere_rpos_prev.x = sphere_rpos.x - sphere_vel.x;
    sphere_rpos_prev.y = sphere_rpos.y - sphere_vel.y;
    sphere_rpos_prev.z = sphere_rpos.z - sphere_vel.z;

    float dists[3];
    float prev_dists[3];
    float normal_dir[3];
    float penetration[3];
    float prev_penetration[3];
    float area_box[3];

    area_box[0] = boxB->mDimension[2] * boxB->mDimension[1] * 4.0f;
    area_box[1] = boxB->mDimension[2] * boxB->mDimension[0] * 4.0f;
    area_box[2] = boxB->mDimension[0] * boxB->mDimension[1] * 4.0f;
    float volume_box = area_box[2] * area_box[1] * area_box[0];

    int in = 0;
    for (int a_lp = 0; a_lp < 3; a_lp++) {
        float b_dim = boxB->mDimension[a_lp];
        const UMath::Vector4 &bn = boxB->mNormal[a_lp];
        float prev = Dot3(sphere_rpos_prev, bn);
        normal_dir[a_lp] = prev > 0.0f ? 1.0f : -1.0f;
        dists[a_lp] = Dot3(sphere_rpos, bn) * normal_dir[a_lp];
        prev_dists[a_lp] = normal_dir[a_lp] * prev;
        if (b_dim + radius <= dists[a_lp]) {
            return false;
        }
        if (dists[a_lp] <= b_dim) {
            in++;
        }
        penetration[a_lp] = b_dim - (dists[a_lp] - radius);
        prev_penetration[a_lp] = b_dim - (prev_dists[a_lp] - radius);
    }

    UMath::Vector4 rel_collision_point = kGeomVector4Zero;
    float crossed_planes[3];
    crossed_planes[0] = 0.0f;
    crossed_planes[1] = 0.0f;
    crossed_planes[2] = 0.0f;

    if (in < 3) {
        unsigned int count = 0;
        for (int i = 0; i < 3; i++) {
            if (prev_dists[i] > boxB->mDimension[i]) {
                count++;
                crossed_planes[i] = 1.0f;
                float b_dim = (penetration[i] - radius) * normal_dir[i];
                rel_collision_point.x = b_dim * boxB->mNormal[i].x + rel_collision_point.x;
                rel_collision_point.y = b_dim * boxB->mNormal[i].y + rel_collision_point.y;
                rel_collision_point.z = b_dim * boxB->mNormal[i].z + rel_collision_point.z;
            }
        }

        if (count != 0) {
            collision_point.x = rel_collision_point.x + sphereA->mPosition.x;
            collision_point.y = rel_collision_point.y + sphereA->mPosition.y;
            collision_point.z = rel_collision_point.z + sphereA->mPosition.z;

            // Sweep the sphere centre over this frame's motion (point_0 -> centre).
            UMath::Vector4 point_0;
            point_0.x = sphereA->mPosition.x - sphere_vel.x;
            point_0.y = sphereA->mPosition.y - sphere_vel.y;
            point_0.z = sphereA->mPosition.z - sphere_vel.z;

            UMath::Vector4 v;
            v.x = sphereA->mPosition.x - point_0.x;
            v.y = sphereA->mPosition.y - point_0.y;
            v.z = sphereA->mPosition.z - point_0.z;

            UMath::Vector4 w;
            w.x = collision_point.x - point_0.x;
            w.y = collision_point.y - point_0.y;
            w.z = collision_point.z - point_0.z;

            float vv = v.x * v.x + v.z * v.z + v.y * v.y;
            float wv = w.x * v.x + w.z * v.z + w.y * v.y;

            if (vv > kGeomEpsilon && wv < vv && wv > 0.0f) {
                float t = wv / vv;
                UMath::Vector4 point_c;
                point_c.x = v.x * t + point_0.x;
                point_c.y = v.y * t + point_0.y;
                point_c.z = v.z * t + point_0.z;

                float dx = point_c.x - collision_point.x;
                float dy = point_c.y - collision_point.y;
                float dz = point_c.z - collision_point.z;
                float rr_xx = dx * dx + dy * dy + dz * dz;

                float f = radius * radius - rr_xx;
                if (f < kGeomEpsilon) {
                    return false;
                }

                if (volume_box > 0.0f) {
                    float temp = sqrtf(f);
                    float scaleby = 1.0f / sqrtf(vv);
                    UMath::Vector4 norm_motion;
                    norm_motion.x = v.x * scaleby;
                    norm_motion.y = v.y * scaleby;
                    norm_motion.z = v.z * scaleby;

                    UMath::Vector4 point_t;
                    point_t.x = norm_motion.x * -temp + point_c.x;
                    point_t.y = norm_motion.y * -temp + point_c.y;
                    point_t.z = norm_motion.z * -temp + point_c.z;

                    normal.x = point_t.x - collision_point.x;
                    normal.y = point_t.y - collision_point.y;
                    normal.z = point_t.z - collision_point.z;
                    GeomUnitxyz(normal, normal);
                    result->mOverlap = sqrtf(rr_xx) - radius;
                } else {
                    int i;
                    for (i = 0; i < 3; i++) {
                        if (crossed_planes[i] * area_box[i] > 0.0f) {
                            float s = normal_dir[i];
                            normal.x = s * boxB->mNormal[i].x;
                            normal.y = s * boxB->mNormal[i].y;
                            normal.z = s * boxB->mNormal[i].z;
                            result->mOverlap = -penetration[i];
                            break;
                        }
                    }
                    if (i == 3) {
                        return false;
                    }
                }
            } else if (volume_box > 0.0f) {
                UMath::Vector4 dd;
                GeomUnitxyz(rel_collision_point, dd);
                float len = sqrtf(rel_collision_point.x * rel_collision_point.x + rel_collision_point.z * rel_collision_point.z +
                                  rel_collision_point.y * rel_collision_point.y);
                float q = dd.x * sphere_vel.x + dd.y * sphere_vel.y + dd.z * sphere_vel.z + radius;
                if (len > q) {
                    return false;
                }
                if (!(len > kGeomEpsilon)) {
                    return false;
                }
                float overlap = dd.x * rel_collision_point.x + dd.z * rel_collision_point.z + dd.y * rel_collision_point.y - radius;
                result->mOverlap = overlap;
                if (!(overlap < 0.0f)) {
                    return false;
                }
                collision_point.x = rel_collision_point.x + sphereA->mPosition.x;
                collision_point.y = rel_collision_point.y + sphereA->mPosition.y;
                collision_point.z = rel_collision_point.z + sphereA->mPosition.z;
                normal.x = dd.x * -1.0f;
                normal.y = dd.y * -1.0f;
                normal.z = dd.z * -1.0f;
            } else {
                float lensq = rel_collision_point.x * rel_collision_point.x + rel_collision_point.w * rel_collision_point.w +
                              rel_collision_point.z * rel_collision_point.z + rel_collision_point.y * rel_collision_point.y;
                if (!(radius * radius > lensq)) {
                    return false;
                }
                int i;
                for (i = 0; i < 3; i++) {
                    if (crossed_planes[i] * area_box[i] > 0.0f) {
                        float s = normal_dir[i];
                        normal.x = s * boxB->mNormal[i].x;
                        normal.y = s * boxB->mNormal[i].y;
                        normal.z = s * boxB->mNormal[i].z;
                        break;
                    }
                }
                if (i == 3) {
                    return false;
                }
                collision_point.x = rel_collision_point.x + sphereA->mPosition.x;
                collision_point.y = rel_collision_point.y + sphereA->mPosition.y;
                collision_point.z = rel_collision_point.z + sphereA->mPosition.z;

                UMath::Vector4 colldir;
                colldir.x = normal.x * -1.0f;
                colldir.y = normal.y * -1.0f;
                colldir.z = normal.z * -1.0f;
                float x = RayExitSphere(sphereA->mPosition, radius, collision_point, colldir);
                if (!(x > kGeomEpsilon)) {
                    return false;
                }
                result->mOverlap = -x;
            }

            if (!(result->mOverlap < 0.0f)) {
                return false;
            }
            goto finished;
        }
    }

    {
        // Sphere centre is inside the box (or crossed no face this frame): push
        // out through the face with the smallest previous penetration.
        int nearestface = -1;
        if (area_box[0] > 0.0f) {
            nearestface = 0;
        }
        if (area_box[1] > 0.0f && (nearestface == -1 || prev_penetration[1] < prev_penetration[nearestface])) {
            nearestface = 1;
        }
        if (area_box[2] > 0.0f && (nearestface == -1 || prev_penetration[2] < prev_penetration[nearestface])) {
            nearestface = 2;
        }
        if (nearestface == -1) {
            return false;
        }

        float s = normal_dir[nearestface];
        normal.x = s * boxB->mNormal[nearestface].x;
        normal.y = s * boxB->mNormal[nearestface].y;
        normal.z = s * boxB->mNormal[nearestface].z;
        float scaleby = -penetration[nearestface];
        result->mOverlap = scaleby;
        scaleby = -scaleby;
        collision_point.x = scaleby * normal.x + sphereA->mPosition.x;
        collision_point.y = scaleby * normal.y + sphereA->mPosition.y;
        collision_point.z = scaleby * normal.z + sphereA->mPosition.z;
    }

finished:
    if (result != sphereA) {
        normal.x = normal.x * -1.0f;
        normal.y = normal.y * -1.0f;
        normal.z = normal.z * -1.0f;
        result->mPenetratesOther = 1;
        return true;
    }
    result->mPenetratesOther = 0;
    return true;
}

// PC 0x48BF50
bool Geometry::SphereVsSphere(const Geometry *a, const Geometry *b, Geometry *result) {
    UMath::Vector4 rPos;
    rPos.x = a->mPosition.x - b->mPosition.x;
    rPos.y = a->mPosition.y - b->mPosition.y;
    rPos.z = a->mPosition.z - b->mPosition.z;

    float result_len = sqrtf(rPos.y * rPos.y + rPos.z * rPos.z + rPos.x * rPos.x);
    float radius_total = a->mDimension[0] + b->mDimension[0];

    if (result_len == 0.0f) {
        result->mCollision_normal.x = 0.0f;
        result->mCollision_normal.y = -1.0f;
        result->mCollision_normal.z = 0.0f;
        // speed.exe writes all three components into mCollision_point.x (so
        // only .x = a.z survives); kept as shipped.
        result->mCollision_point.x = a->mPosition.x;
        result->mCollision_point.x = a->mDimension[0] + a->mPosition.y;
        result->mCollision_point.x = a->mPosition.z;
        result->mOverlap = -a->mDimension[0];
    } else {
        if (!(result_len < radius_total)) {
            return false;
        }
        GeomUnitxyz(rPos, result->mCollision_normal);
        result->mOverlap = result_len - radius_total;
        float s = result->mOverlap + b->mDimension[0];
        result->mCollision_point.x = s * result->mCollision_normal.x + b->mPosition.x;
        result->mCollision_point.y = s * result->mCollision_normal.y + b->mPosition.y;
        result->mCollision_point.z = s * result->mCollision_normal.z + b->mPosition.z;
    }

    // Note the inverted sense compared to the box tests (as shipped).
    if (a != result) {
        result->mCollision_normal.x = result->mCollision_normal.x * -1.0f;
        result->mPenetratesOther = 0;
        result->mCollision_normal.y = result->mCollision_normal.y * -1.0f;
        result->mCollision_normal.z = result->mCollision_normal.z * -1.0f;
        return true;
    }
    result->mPenetratesOther = 1;
    return true;
}

// PC 0x48C0E0
bool Geometry::BoxVsSphere(const Geometry *boxA, const Geometry *sphereB, Geometry *result) {
    return SphereVsBox(sphereB, boxA, result);
}

// PC 0x48C100
bool Geometry::FindIntersection(const Geometry *A, const Geometry *B, Geometry *result) {
    typedef bool (*IntersectionAlgo)(const Geometry *, const Geometry *, Geometry *);
    // PC 0x895408
    static IntersectionAlgo algos[MAXSHAPES][MAXSHAPES] = {
        {nullptr, nullptr, nullptr},
        {nullptr, BoxVsBox, BoxVsSphere},
        {nullptr, SphereVsBox, SphereVsSphere},
    };

    result->mCollision_normal.w = 0.0f;
    result->mCollision_point.w = 0.0f;
    result->mOverlap = -100000.0f;
    result->mPenetratesOther = 0;

    IntersectionAlgo handler = algos[A->mShape][B->mShape];
    if (handler == nullptr) {
        return false;
    }
    return handler(A, B, result);
}

}; // namespace Collision
}; // namespace Dynamics
