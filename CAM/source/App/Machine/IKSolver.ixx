module;

#include <cmath>
#include <vector>

export module Cam.Machine.IKSolver;

import Cam.Machine.Pose;
import Cam.Machine.Definition;
import Cam.Machine.ToolPath;
import Cam.App.ToolPath;
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

            for (Cam::App::ToolPathPoint const& pt : toolPath.points) {
                result.points.push_back(solvePoint(
                    pt.position,
                    pt.toolDirection,
                    machine,
                    pt.rapid,
                    pt.cutting,
                    pt.t
                ));
            }

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

        // Solve one toolpath point into a MachinePose.

        static MachinePose solvePoint(
            Pos3   const& toolpathPos,
            Pos3   const& toolpathDir,
            MachineDefinition const& machine,
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

            // ── No part rotation DOF (pure 3-axis) ──────────────────────
            if (machine.part.dof.freeRotations.empty()) {
                result.toolWorldPose     = { toolpathPos, M };
                result.partWorldPose     = machine.part.defaultPose;
                result.directionResidual = toolpathDir - M;
                return result;
            }

            // ── One free rotation axis on the part (e.g. A = {1,0,0}) ──
            Pos3 A = machine.part.dof.freeRotations[0];

            // Achievability: rotation around A preserves dot(D, A).
            // The cut is possible only when dot(toolpathDir, A) == dot(M, A).
            float delta = toolpathDir.dot(A) - M.dot(A);

            if (std::fabs(delta) > 1e-4f) {
                result.toolWorldPose     = { toolpathPos, M };
                result.partWorldPose     = machine.part.defaultPose;
                result.directionResidual = A * delta;
                return result;
            }

            // Singularity: toolpathDir parallel to A → angle undefined.
            if (toolpathDir.cross(A).pythag() < 1e-4f) {
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

            return result;
        }
    };
}
