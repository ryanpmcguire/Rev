module;

#include <cmath>
#include <vector>

export module Cam.Machine.Pose;

import Rev.Core.Pos3;

// ------------------------------------------------------------------
// Cam::Machine::Pose
//
// A 6-DOF pose: where something is (position) and which way it
// faces (direction).  Direction is the primary axis — for a tool
// this is the tool axis; for a part it is the approach direction.
// Roll around the direction axis is deliberately not represented
// because it is irrelevant for milling.
//
// The implied rotation is always the minimum-arc (swing) rotation
// from the canonical up {0,0,1} to direction, so ALL math in this
// file is purely dot / cross — no sin, cos, atan2 anywhere.
//
// MachineDOF and PoseResult live in the same module so that
// Pose::relativeTo(ref, dof) can be declared inside the class
// and defined inline below after both structs are complete.
// ------------------------------------------------------------------

export namespace Cam::Machine {

    using Rev::Core::Pos3;

    // Forward declarations so Pose can reference them in its signature.
    struct MachineDOF;
    struct PoseResult;

    // ---------------------------------------------------------------
    struct Pose {

        Pos3 position  = {};
        Pos3 direction = { 0.0f, 0.0f, 1.0f };  // normalised primary axis

        static Pose identity() { return {}; }

        // -- Low-level rotation (Rodrigues, cross-product form) ----------
        //
        // Rotate v by the minimum-arc rotation from {0,0,1} → direction.
        //
        //   k  = from × to        (rotation axis scaled by sin θ)
        //   c  = from · to        (= cos θ)
        //
        //   v' = v + k×v + k×(k×v) / (1 + c)
        //
        // Degenerate case (antiparallel, c → -1): choose 180° around X.

        Pos3 transformDirection(Pos3 const& v) const {

            Pos3  k = Pos3{ 0.0f, 0.0f, 1.0f }.cross(direction);
            float c = Pos3{ 0.0f, 0.0f, 1.0f }.dot(direction);

            if (c >=  1.0f - 1e-6f) { return v; }
            if (c <= -1.0f + 1e-6f) { return { v.x, -v.y, -v.z }; }  // 180° around X

            Pos3 kxv = k.cross(v);
            return v + kxv + k.cross(kxv) * (1.0f / (1.0f + c));
        }

        // Rotate v by the inverse of the above (un-rotate into world frame).
        // Identical formula with k' = direction × {0,0,1}.

        Pos3 inverseTransformDirection(Pos3 const& v) const {

            Pos3  k = direction.cross(Pos3{ 0.0f, 0.0f, 1.0f });
            float c = direction.dot(Pos3{ 0.0f, 0.0f, 1.0f });

            if (c >=  1.0f - 1e-6f) { return v; }
            if (c <= -1.0f + 1e-6f) { return { v.x, -v.y, -v.z }; }

            Pos3 kxv = k.cross(v);
            return v + kxv + k.cross(kxv) * (1.0f / (1.0f + c));
        }

        // -- Point transforms --------------------------------------------

        Pos3 transformPoint(Pos3 const& local) const {
            return transformDirection(local) + position;
        }

        Pos3 inverseTransformPoint(Pos3 const& world) const {
            return inverseTransformDirection(world - position);
        }

        // -- Composition -------------------------------------------------

        // other is expressed in this frame; result is in the parent frame.
        Pose operator*(Pose const& other) const {
            return {
                .position  = transformPoint(other.position),
                .direction = transformDirection(other.direction)
            };
        }

        // T^{-1} such that T^{-1} * T = identity.
        //
        //   If T(p) = R·p + t,  then  T^{-1}(p) = R^{-1}·(p − t)
        //   invDir = R^{-1}·{0,0,1}  =  inverseTransformDirection({0,0,1})
        //   invPos = −R^{-1}·t        =  inverseTransformDirection(−position)

        Pose inverse() const {
            Pos3 invDir = inverseTransformDirection({ 0.0f, 0.0f, 1.0f });
            Pos3 invPos = inverseTransformDirection(Pos3{} - position);
            return { invPos, invDir };
        }

        // -- relativeTo (unconstrained) ----------------------------------

        // Express this pose in the coordinate frame of reference.
        // Pure linear algebra; no DOF constraints applied.

        Pose relativeTo(Pose const& reference) const {
            return {
                .position  = reference.inverseTransformPoint(position),
                .direction = reference.inverseTransformDirection(direction)
            };
        }

        // -- relativeTo (DOF-constrained) --------------------------------
        //
        // Projects the full relative relationship onto the subspace of
        // motions that the DOF constraints allow.  Returns both the
        // achievable component and the residual (the "impossible" part).
        //
        // Defined after MachineDOF and PoseResult are complete (below).

        PoseResult relativeTo(Pose const& reference, MachineDOF const& dof) const;
    };

    // ---------------------------------------------------------------
    // Degrees of freedom for one actor (tool or part) in the machine.
    // All axis vectors are expressed as unit vectors in machine space.
    // ---------------------------------------------------------------

    struct MachineDOF {

        std::vector<Pos3> freeTranslations;  // axes the actor can translate along
        std::vector<Pos3> freeRotations;     // axes the actor can rotate around

        // Convenience factories

        static MachineDOF XYZ() {
            return { { {1,0,0}, {0,1,0}, {0,0,1} }, {} };
        }
        static MachineDOF XZ() {
            return { { {1,0,0}, {0,0,1} }, {} };
        }
        static MachineDOF RotaryA() {  // rotation around X
            return { {}, { {1,0,0} } };
        }
        static MachineDOF RotaryB() {  // rotation around Y
            return { {}, { {0,1,0} } };
        }
        static MachineDOF RotaryC() {  // rotation around Z
            return { {}, { {0,0,1} } };
        }
        static MachineDOF Fixed() {
            return { {}, {} };
        }
    };

    // ---------------------------------------------------------------
    // Result of a constrained Pose::relativeTo call.
    // ---------------------------------------------------------------

    struct PoseResult {

        Pose pose;                          // achievable component within DOF
        Pos3 positionResidual  = {};        // untranslatable delta; ~0 ⟹ achievable
        Pos3 directionResidual = {};        // unrotatable delta;    ~0 ⟹ achievable
        bool singular          = false;     // direction parallel to rotation axis

        // Fully achievable: both residuals near zero, not singular.
        bool valid() const {
            return !singular
                && positionResidual.pythag()  < 1e-4f
                && directionResidual.pythag() < 1e-4f;
        }

        float positionError()  const { return positionResidual.pythag(); }
        float directionError() const { return directionResidual.pythag(); }
    };

    // ---------------------------------------------------------------
    // Constrained relativeTo implementation
    //
    // Position:
    //   Project position delta onto each free translation axis; sum
    //   is the achievable component, remainder is the residual.
    //
    // Direction:
    //   A rotation around axis R preserves dot(D, R) for any vector D.
    //   In the reference frame the default direction is {0,0,1}, whose
    //   component along R is R.z.  The target direction is achievable
    //   iff dot(D, R) == R.z for every free rotation axis.
    //   The residual is the mismatch in that component.
    //
    //   Singularity: direction parallel to rotation axis → the
    //   rotation angle is geometrically undefined.
    // ---------------------------------------------------------------

    inline PoseResult Pose::relativeTo(
        Pose    const& reference,
        MachineDOF const& dof
    ) const {

        Pose full = relativeTo(reference);

        // -- Position ---------------------------------------------------
        Pos3 achievablePos = {};
        for (Pos3 const& axis : dof.freeTranslations) {
            achievablePos = achievablePos + axis * full.position.dot(axis);
        }
        Pos3 posResidual = full.position - achievablePos;

        // -- Direction --------------------------------------------------
        Pos3 dirResidual  = {};
        bool singular     = false;
        Pos3 achievableDir = full.direction;

        if (dof.freeRotations.empty()) {
            // No rotational freedom — direction must be the identity {0,0,1}.
            dirResidual   = full.direction - Pos3{ 0.0f, 0.0f, 1.0f };
            achievableDir = { 0.0f, 0.0f, 1.0f };
        }
        else {
            for (Pos3 const& R : dof.freeRotations) {

                // Achievability: dot(D, R) must equal dot({0,0,1}, R) = R.z
                float delta = full.direction.dot(R) - R.z;

                if (std::fabs(delta) > 1e-5f) {
                    dirResidual = dirResidual + R * delta;
                }

                // Singularity: D parallel to the rotation axis.
                if (full.direction.cross(R).pythag() < 1e-4f) {
                    singular = true;
                }
            }

            // Remove the residual component and renormalize.
            achievableDir = full.direction - dirResidual;
            float len = achievableDir.pythag();
            achievableDir = len > 1e-6f
                ? achievableDir / len
                : Pos3{ 0.0f, 0.0f, 1.0f };
        }

        return {
            .pose              = { achievablePos, achievableDir },
            .positionResidual  = posResidual,
            .directionResidual = dirResidual,
            .singular          = singular
        };
    }
}
