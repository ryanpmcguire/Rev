module;

#include <cmath>
#include <vector>

export module Cam.Machine.IKSolver;

import Cam.Machine.Pose;
import Cam.Machine.Definition;
import Cam.Machine.ToolPath;
import Cam.App.ToolPath;
import Cam.CoordinateSystem;
import Rev.Core.Pos3;

// ------------------------------------------------------------------
// IKSolver
//
// Converts a world-space ToolPath into a MachineToolPath.
// For every toolpath point it finds the physical world-space poses
// of both the tool and the part that satisfy the machine's DOF.
//
// Core idea (3+1 indexed example)
// ─────────────────────────────
// The machine tool is always fixed at direction M = {0,0,1}.
// The toolpath point says the cut direction in world space is D.
// The part must rotate by R (around its free rotation axis A) so:
//
//     R · D = M
//
// Achievability: rotation around A preserves dot(D, A), so the cut
// is only possible when dot(D, A) == dot(M, A).
//
// Once R is known, every toolpath position P transforms to:
//
//     machineXYZ = partOrigin + R · (P − partOrigin)
//
// "No angular math": R is never extracted as an angle.  cos θ and
// sin θ are derived from dot/cross projections — no atan2 anywhere.
// ------------------------------------------------------------------

export namespace Cam::Machine {

    using Rev::Core::Pos3;

    struct IKSolver {

        // The direction the machine's physical tool always points.
        static Pos3 machineToolDirection() {
            return { 0.0f, 0.0f, 1.0f };
        }

        // Solve a complete toolpath against a machine definition.
        // Points that the machine cannot achieve have valid() == false;
        // their residuals describe which DOF is missing and by how much.
        static MachineToolPath solve(
            Cam::App::ToolPath const& toolPath,
            MachineDefinition  const& machine
        ) {
            MachineToolPath result;
            result.points.reserve(toolPath.points.size());

            // Record the machine's fixed rotary axis + pivot so the display
            // can rebuild the exact part transform (rotation about this axis).
            if (!machine.part.dof.freeRotations.empty()) {
                result.rotaryAxis = machine.part.dof.freeRotations[0];
            }
            result.rotaryPivot = machine.part.defaultPose.position;

            // The part's freedom as a coordinate system (built once): its free
            // rotary is rx, located on the rotary axis through the pivot.  Every
            // point is solved directly on THIS frame.
            Cam::Coord::CoordinateSystem partCS = machine.partFrame();

            for (Cam::App::ToolPathPoint const& pt : toolPath.points) {
                result.points.push_back(solvePoint(
                    pt.position,
                    pt.toolDirection,
                    machine,
                    partCS,
                    pt.rapid,
                    pt.cutting,
                    pt.t
                ));
            }

            unwrapRotaryAngles(result.points);

            return result;
        }

    private:

        // Apply the rotation that maps D → M around axis A, to vector v.
        //
        // The rotation lives in the plane perpendicular to A.
        // D and M are projected to get in-plane unit vectors; cos/sin
        // are derived from their dot and cross — no atan2.
        //
        // Rodrigues in-plane form:
        //     v_perp' = c · v_perp  +  s · (A × v_perp)
        //     v'      = v_parallel  +  v_perp'
        //
        // Returns v unchanged when D is already aligned with M, or
        // when D is parallel to A (singularity — caller detects this).

        static Pos3 applyRotationDtoM(
            Pos3 const& v,
            Pos3 const& D,   // source direction (normalised)
            Pos3 const& M,   // target direction (normalised)
            Pos3 const& A    // rotation axis    (normalised)
        ) {
            Pos3 vParallel = A * v.dot(A);
            Pos3 vPerp     = v - vParallel;

            Pos3  Dperp    = D - A * D.dot(A);
            Pos3  Mperp    = M - A * M.dot(A);
            float DperpLen = Dperp.pythag();
            float MperpLen = Mperp.pythag();

            if (DperpLen < 1e-6f || MperpLen < 1e-6f) { return v; }

            Pos3  Dn = Dperp / DperpLen;
            Pos3  Mn = Mperp / MperpLen;
            float c  = Dn.dot(Mn);
            float s  = A.dot(Dn.cross(Mn));   // signed via right-hand rule

            return vParallel + vPerp * c + A.cross(vPerp) * s;
        }

        // Make the per-point rotary angles continuous.
        //
        // Each angle is computed independently with atan2 → range (-π, π].  When
        // the true angle crosses the ±180° boundary, adjacent points read e.g.
        // +179° then -179° — a stored jump of ~358° that the interpolator would
        // sweep the long way around, snapping the part a full turn.  Shift each
        // angle by whole turns of 2π so every step stays within π of the prior
        // point, yielding a smooth, monotone-where-physical sequence.
        static void unwrapRotaryAngles(std::vector<MachinePose>& points) {

            constexpr double twoPi = 6.283185307179586;

            for (size_t i = 1; i < points.size(); i++) {

                double delta = points[i].rotaryAngle - points[i - 1].rotaryAngle;

                while (delta >  3.141592653589793) { delta -= twoPi; }
                while (delta < -3.141592653589793) { delta += twoPi; }

                points[i].rotaryAngle = points[i - 1].rotaryAngle + delta;
            }
        }

        // Solve one toolpath point into a MachinePose.

        static MachinePose solvePoint(
            Pos3   const& toolpathPos,
            Pos3   const& toolpathDir,
            MachineDefinition const& machine,
            Cam::Coord::CoordinateSystem const& partCS,
            bool   rapid,
            bool   cutting,
            double t
        ) {
            MachinePose result;
            result.t       = t;
            result.rapid   = rapid;
            result.cutting = cutting;

            Pos3 M          = machineToolDirection();
            Pos3 partOrigin = machine.part.defaultPose.position;

            // The part's free rotary axis, in machine space (rx of the part frame).
            Pos3 A = partCS.freeRotationAxis();

            // ── No part rotation DOF (pure 3-axis) ──────────────────────
            if (A.pythag() < 1e-6f) {
                result.toolWorldPose     = { toolpathPos, M };
                result.partWorldPose     = machine.part.defaultPose;
                result.directionResidual = toolpathDir - M;
                return result;
            }

            // ── Orientation, solved ON THE FRAME ───────────────────────
            // The frame's free rotary decides what's reachable: the angle that maps
            // the cut direction onto the tool, the un-makeable residual (rotation
            // about A can't change the along-A component), and the singularity.
            auto rot = partCS.solveRotationAxis(toolpathDir, M);

            // Achievability: residual ⟹ the cut needs a DOF the machine lacks.
            if (rot.residual.pythag() > 1e-4f) {
                result.toolWorldPose     = { toolpathPos, M };
                result.partWorldPose     = machine.part.defaultPose;
                result.directionResidual = rot.residual;
                return result;
            }

            // Singularity: toolpathDir parallel to A → angle undefined.
            if (rot.singular) {
                result.toolWorldPose = { toolpathPos, M };
                result.partWorldPose = machine.part.defaultPose;
                result.singular      = true;
                return result;
            }

            // Apply R (rotates toolpathDir → M around A) to:
            //   1. Part Z axis        → new part world direction
            //   2. Toolpath position  → machine XYZ for the tool
            Pos3 partDir    = applyRotationDtoM({ 0.0f, 0.0f, 1.0f }, toolpathDir, M, A);
            Pos3 machineXYZ = partOrigin + applyRotationDtoM(toolpathPos - partOrigin, toolpathDir, M, A);

            result.toolWorldPose = { machineXYZ, M };
            result.partWorldPose = { partOrigin, partDir };
            result.rotaryAngle   = float(rot.angle);

            return result;
        }
    };
}
