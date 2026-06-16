module;

#include <cmath>
#include <algorithm>

export module Cam.CoordinateSystem;

import Rev.Core.Pos3;
export import Cam.CoordinateSystem.Axis;

// ------------------------------------------------------------------
// Cam::CoordinateSystem
//
// A coordinate system is TWO things in one:
//
//   1. A POSE -- a rigid transform (the matrix) to its parent frame.  This is the
//      current best estimate, and is what every transform in the app reads.
//   2. Six AXES -- the rigid degrees of freedom, NAMED (x/y/z translate along the
//      local X/Y/Z; rx/ry/rz rotate about them).  Each carries its `value`,
//      `certainty`, and `maxSpeed`, so the frame knows what it KNOWS and what the
//      machine can DRIVE.  (The CoordinateSystem owns the interpretation -- the
//      Axis itself is just a scalar DOF.)
//
// The single shared SOLVER core lives here so the IK and the probe-fit reduce to
// the same operation: project a desired relative pose onto what the frame's FREE
// axes (maxSpeed > 0) can produce -- ORIENTATION FIRST, then TRANSLATION GIVEN the
// orientation -- and report the residual (the part the free DOF cannot make).
// That residual is simultaneously the IK's unreachable-direction and the probe's
// off-axis remainder; they are the same thing.
// ------------------------------------------------------------------

export namespace Cam {

    using Rev::Core::Pos3;

    // The result of projecting a desired (direction, position) onto a frame's free
    // DOF.  Mirrors the machine layer's PoseResult.
    struct Projection {
        Pos3 achievableDirection { 0.0f, 0.0f, 1.0f };
        Pos3 achievablePosition  {};
        Pos3 directionResidual   {};   // ~0 => orientation achievable
        Pos3 positionResidual    {};   // ~0 => position achievable
        bool singular = false;         // direction parallel to a rotation axis

        bool achievable() const {
            return !singular
                && directionResidual.pythag() < 1e-4f
                && positionResidual.pythag()  < 1e-4f;
        }
        float directionError() const { return directionResidual.pythag(); }
        float positionError()  const { return positionResidual.pythag(); }
    };

    struct CoordinateSystem {

        // Rigid transform LOCAL -> PARENT:  p_parent = R * p_local + t.
        // Row-major 3x3 + translation (same convention as the probe fit).
        double r[9] = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
        Pos3   t {};

        // The six rigid DOF, named.  x/y/z translate along local X/Y/Z;
        // rx/ry/rz rotate about local X/Y/Z.
        Axis x, y, z;
        Axis rx, ry, rz;

        // ===========================================================
        // Transforms (the "matrix" half)
        // ===========================================================

        Pos3 apply(const Pos3& p) const {
            return {
                float(r[0]*p.x + r[1]*p.y + r[2]*p.z + t.x),
                float(r[3]*p.x + r[4]*p.y + r[5]*p.z + t.y),
                float(r[6]*p.x + r[7]*p.y + r[8]*p.z + t.z)
            };
        }
        Pos3 applyDirection(const Pos3& d) const {
            return {
                float(r[0]*d.x + r[1]*d.y + r[2]*d.z),
                float(r[3]*d.x + r[4]*d.y + r[5]*d.z),
                float(r[6]*d.x + r[7]*d.y + r[8]*d.z)
            };
        }
        // Inverse of a rigid transform: q -> R^T (q - t).
        Pos3 applyInverse(const Pos3& q) const {
            const Pos3 d = q - t;
            return {
                float(r[0]*d.x + r[3]*d.y + r[6]*d.z),
                float(r[1]*d.x + r[4]*d.y + r[7]*d.z),
                float(r[2]*d.x + r[5]*d.y + r[8]*d.z)
            };
        }
        // Column-major 4x4 (glm / shader layout) from the row-major rigid pose.
        void toMatrix4(float out[16]) const {
            out[0]  = float(r[0]); out[1]  = float(r[3]); out[2]  = float(r[6]); out[3]  = 0.0f;
            out[4]  = float(r[1]); out[5]  = float(r[4]); out[6]  = float(r[7]); out[7]  = 0.0f;
            out[8]  = float(r[2]); out[9]  = float(r[5]); out[10] = float(r[8]); out[11] = 0.0f;
            out[12] = t.x;         out[13] = t.y;         out[14] = t.z;         out[15] = 1.0f;
        }

        // ===========================================================
        // Matrix <-> axes (two views of the SAME pose; you keep them in sync)
        // ===========================================================
        //
        // The pose can be read/written EITHER as the raw matrix (r/t) OR as the six
        // named axis VALUES.  They are two views of one thing, so after editing one
        // side you call the matching resolver to reconcile -- NO dirty flags, NO
        // implicit recompute; the responsibility is explicit and visible at the
        // call site, which is exactly what stops slop.
        //
        //   * edited an Axis value?   -> resolveMatrix()  (rebuild r/t from the axes)
        //   * edited r/t directly?    -> resolveAxes()    (re-read the axes from r/t)
        //
        // Convention: translation axes x/y/z ARE the origin position (t), and the
        // rotation axes are Euler angles with R = Rz(rz) . Ry(ry) . Rx(rx) -- rx is
        // rotation about X (the rotary, the dominant one), so gimbal lock sits at a
        // 90 deg tilt we never reach in this milling domain.  Resolvers touch only
        // each Axis's `value`; `certainty` and `maxSpeed` are epistemic and are
        // never derived from the matrix.

        // Rebuild the matrix (r/t) from the six axis VALUES.
        void resolveMatrix() {
            const double cx = std::cos(rx.value), sx = std::sin(rx.value);
            const double cy = std::cos(ry.value), sy = std::sin(ry.value);
            const double cz = std::cos(rz.value), sz = std::sin(rz.value);

            // R = Rz * Ry * Rx  (row-major).
            r[0] = cy*cz;  r[1] = sx*sy*cz - cx*sz;  r[2] = cx*sy*cz + sx*sz;
            r[3] = cy*sz;  r[4] = sx*sy*sz + cx*cz;  r[5] = cx*sy*sz - sx*cz;
            r[6] = -sy;    r[7] = sx*cy;             r[8] = cx*cy;

            t = { float(x.value), float(y.value), float(z.value) };
        }

        // Re-read the six axis VALUES from the matrix (r/t).  Leaves certainty /
        // maxSpeed untouched.
        void resolveAxes() {
            x.value = t.x;  y.value = t.y;  z.value = t.z;

            const double sy = std::clamp(-r[6], -1.0, 1.0);
            ry.value = std::asin(sy);

            if (std::fabs(r[6]) < 0.999999) {           // not gimbal-locked
                rx.value = std::atan2(r[7], r[8]);      // atan2(sx*cy, cx*cy)
                rz.value = std::atan2(r[3], r[0]);      // atan2(cy*sz, cy*cz)
            }
            else {                                      // ry ~ +/-90: fold roll into rx
                rx.value = std::atan2(-r[5], r[4]);
                rz.value = 0.0;
            }
        }

        // ===========================================================
        // Chaining (the reason frames stack: machine -> work -> part)
        // ===========================================================

        // The frame `child` -- which is defined RELATIVE to this one -- expressed in
        // THIS frame's parent.  result.apply(p) == this.apply(child.apply(p)).
        // (Pose only; epistemics are not composed here -- see notes.)
        CoordinateSystem composedWith(const CoordinateSystem& child) const {
            CoordinateSystem out;
            for (int row = 0; row < 3; row++) {
                for (int col = 0; col < 3; col++) {
                    out.r[row*3+col] = r[row*3+0]*child.r[0*3+col]
                                     + r[row*3+1]*child.r[1*3+col]
                                     + r[row*3+2]*child.r[2*3+col];
                }
            }
            out.t = apply(child.t);   // R*child.t + t
            out.resolveAxes();
            return out;
        }

        // The inverse transform (PARENT -> LOCAL).
        CoordinateSystem inverse() const {
            CoordinateSystem out;
            out.r[0] = r[0]; out.r[1] = r[3]; out.r[2] = r[6];   // R^T
            out.r[3] = r[1]; out.r[4] = r[4]; out.r[5] = r[7];
            out.r[6] = r[2]; out.r[7] = r[5]; out.r[8] = r[8];
            out.t = applyInverse({ 0.0f, 0.0f, 0.0f });          // -R^T t
            out.resolveAxes();
            return out;
        }

        // THIS frame expressed in `other`'s frame:  other^-1 . this.
        CoordinateSystem relativeTo(const CoordinateSystem& other) const {
            return other.inverse().composedWith(*this);
        }

        // ===========================================================
        // Construction from a basis / an axis
        // ===========================================================

        // From an explicit basis: columns of R are the local X/Y/Z directions
        // expressed in the parent, `origin` is the local origin in the parent.
        static CoordinateSystem fromBasis(Pos3 origin, Pos3 X, Pos3 Y, Pos3 Z) {
            CoordinateSystem cs;
            cs.r[0] = X.x; cs.r[1] = Y.x; cs.r[2] = Z.x;
            cs.r[3] = X.y; cs.r[4] = Y.y; cs.r[5] = Z.y;
            cs.r[6] = X.z; cs.r[7] = Y.z; cs.r[8] = Z.z;
            cs.t = origin;
            cs.resolveAxes();
            return cs;
        }

        // ===========================================================
        // Epistemics (the "axes" half)
        // ===========================================================

        // Any DOF whose value we do not yet know (certainty 0).  Such a frame
        // cannot serve as the trusted PARENT of the next (a stock cannot be
        // measured until the work frame has no unknowns, etc.).
        bool hasUnknown() const {
            return x.isUnknown()  || y.isUnknown()  || z.isUnknown()
                || rx.isUnknown() || ry.isUnknown() || rz.isUnknown();
        }
        bool resolved() const { return !hasUnknown(); }

        // ===========================================================
        // The shared SOLVER core (orientation-first projection)
        // ===========================================================

        // Step 1 -- ORIENTATION.  Project a desired direction onto what the FREE
        // rotation axes (maxSpeed > 0) can produce.  A rotation about axis R
        // preserves dot(D, R), and the canonical local direction is {0,0,1}, so the
        // target is achievable iff dot(targetDir, R) == R.z for every free rotation
        // axis; the residual is the mismatch along R.  No position dependence.
        void solveOrientation(const Pos3& targetDir,
                              Pos3& achievableDir, Pos3& dirResidual, bool& singular) const
        {
            dirResidual = {};
            singular    = false;
            bool any    = false;

            auto consider = [&](const Axis& a, Pos3 R) {
                if (!a.isFree()) { return; }
                any = true;
                const float delta = targetDir.dot(R) - R.z;
                if (std::fabs(delta) > 1e-5f) { dirResidual = dirResidual + R * delta; }
                if (targetDir.cross(R).pythag() < 1e-4f) { singular = true; }
            };
            consider(rx, { 1.0f, 0.0f, 0.0f });
            consider(ry, { 0.0f, 1.0f, 0.0f });
            consider(rz, { 0.0f, 0.0f, 1.0f });

            if (!any) {
                achievableDir = { 0.0f, 0.0f, 1.0f };
                dirResidual   = targetDir - achievableDir;
                return;
            }

            achievableDir = targetDir - dirResidual;
            const float len = achievableDir.pythag();
            achievableDir = (len > 1e-6f) ? achievableDir / len : Pos3{ 0.0f, 0.0f, 1.0f };
        }

        // Step 2 -- TRANSLATION, GIVEN the orientation.  Project the desired
        // position onto the FREE translation axes; the remainder is the residual.
        void solveTranslation(const Pos3& targetPos,
                              Pos3& achievablePos, Pos3& posResidual) const
        {
            achievablePos = {};
            auto consider = [&](const Axis& a, Pos3 D) {
                if (!a.isFree()) { return; }
                achievablePos = achievablePos + D * targetPos.dot(D);
            };
            consider(x, { 1.0f, 0.0f, 0.0f });
            consider(y, { 0.0f, 1.0f, 0.0f });
            consider(z, { 0.0f, 0.0f, 1.0f });
            posResidual = targetPos - achievablePos;
        }

        // The two steps, in the only valid order.  `targetDir` / `targetPos` are the
        // desired pose expressed in THIS frame's local basis; the result is what the
        // frame's free DOF can actually reach, plus the residual.
        Projection project(const Pos3& targetDir, const Pos3& targetPos) const {
            Projection out;
            solveOrientation(targetDir, out.achievableDirection, out.directionResidual, out.singular);
            solveTranslation(targetPos, out.achievablePosition, out.positionResidual);
            return out;
        }
    };
}
