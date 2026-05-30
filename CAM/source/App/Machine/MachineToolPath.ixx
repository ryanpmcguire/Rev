module;

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstddef>

export module Cam.Machine.ToolPath;

import Cam.Machine.Pose;
import Rev.Core.Pos3;

// ------------------------------------------------------------------
// MachinePose  — the fully-solved world-space state of both actors
//                at one instant along the toolpath.
//
// MachineToolPath — the complete solved sequence.
//
// This is the output of IKSolver::solve().  It feeds:
//   • The "Machine Simulation" preview (part animates in WorldView)
//   • Collision detection (both meshes have concrete world poses)
//   • Post-processing (convert direction vectors to angles)
// ------------------------------------------------------------------

export namespace Cam::Machine {

    using Rev::Core::Pos3;

    struct MachinePose {

        // Both expressed in machine / world space.
        Pose toolWorldPose;
        Pose partWorldPose;

        // Residuals from the IK solve.
        // { 0,0,0 } on both ⟹ the machine can fully achieve this point.
        Pos3 positionResidual  = {};
        Pos3 directionResidual = {};
        bool singular          = false;   // degenerate configuration

        // The signed rotary-axis angle (radians) that orients the part for this
        // point: the rotation about the machine's fixed rotary axis that brings
        // the cut direction under the (fixed, vertical) tool.  This is the
        // honest, complete description of an indexed rotary axis — unlike the
        // pose's direction vector, it captures the part's azimuth about the axis.
        double rotaryAngle = 0.0;

        // Mirrors ToolPathPoint fields for timeline sampling.
        double t       = 0.0;
        bool   rapid   = false;
        bool   cutting = true;

        bool valid() const {
            return !singular
                && positionResidual.pythag()  < 1e-4f
                && directionResidual.pythag() < 1e-4f;
        }
    };

    struct MachineToolPath {

        std::vector<MachinePose> points;

        // The machine's fixed rotary axis and its pivot (stock centre), in
        // world space.  Set once by the solver; used to rebuild the exact part
        // transform (rotation about this axis by each point's rotaryAngle).
        Pos3 rotaryAxis  = { 1.0f, 0.0f, 0.0f };
        Pos3 rotaryPivot = {};

        bool   empty() const { return points.empty(); }
        size_t size()  const { return points.size(); }

        // How many points are not achievable on this machine?
        size_t invalidCount() const {
            size_t n = 0;
            for (auto const& p : points) { if (!p.valid()) ++n; }
            return n;
        }

        bool fullyValid() const { return invalidCount() == 0; }

        double durationSeconds() const {
            if (points.empty()) { return 0.0; }
            return points.back().t;
        }

        // Interpolate between the two nearest points for a given progress [0,1].
        bool sampleAtProgress(double progress, MachinePose& out) const {

            if (points.empty()) { return false; }
            if (points.size() == 1) { out = points.front(); return true; }

            const double total = durationSeconds();
            const double time  = total * std::clamp(progress, 0.0, 1.0);

            if (time <= 0.0) { out = points.front(); return true; }
            if (time >= total - 1e-12) { out = points.back(); return true; }

            for (size_t i = 0; i + 1 < points.size(); i++) {

                const double t0 = points[i].t;
                const double t1 = points[i + 1].t;

                if (time > t1 + 1e-9) { continue; }

                const double span  = t1 - t0;
                const float  alpha = span > 1e-12
                    ? float((time - t0) / span)
                    : 0.0f;

                MachinePose const& a = points[i];
                MachinePose const& b = points[i + 1];

                // Linear-interpolate positions; slerp direction via lerp+normalize
                // (good enough for preview; exact slerp not needed here).
                out = a;
                out.t = time;

                out.toolWorldPose.position =
                    a.toolWorldPose.position +
                    (b.toolWorldPose.position - a.toolWorldPose.position) * alpha;

                out.toolWorldPose.direction =
                    (a.toolWorldPose.direction +
                     (b.toolWorldPose.direction - a.toolWorldPose.direction) * alpha)
                    .normalized();

                out.partWorldPose.position =
                    a.partWorldPose.position +
                    (b.partWorldPose.position - a.partWorldPose.position) * alpha;

                out.partWorldPose.direction =
                    (a.partWorldPose.direction +
                     (b.partWorldPose.direction - a.partWorldPose.direction) * alpha)
                    .normalized();

                // Linear angle lerp: points are dense (every cut step + 32 per
                // link arc), so adjacent angles never differ enough to wrap.
                out.rotaryAngle =
                    a.rotaryAngle + (b.rotaryAngle - a.rotaryAngle) * double(alpha);

                return true;
            }

            out = points.back();
            return true;
        }

        // The EXACT part transform at a given progress: a rotation about the
        // machine's fixed rotary axis by the interpolated rotary angle, pivoted
        // at the stock centre.  Column-major 4x4 (glm / shader layout).
        //
        // This is precisely the transform the IK solver applied to produce the
        // machine tool positions, so applying it to every actor reproduces the
        // machine state with zero ambiguity.
        bool partMatrixAtProgress(double progress, float out[16]) const {

            Pose::identityMatrix(out);

            MachinePose sample;

            if (!sampleAtProgress(progress, sample)) { return false; }

            Pose::axisAngleMatrix(
                rotaryAxis,
                float(sample.rotaryAngle),
                rotaryPivot,
                out
            );

            return true;
        }
    };
}
