module;

#include <cmath>

export module Cam.Machine.Definition;

import Cam.Machine.Pose;
import Cam.CoordinateSystem;
import Rev.Core.Pos3;

// ------------------------------------------------------------------
// MachineActor  — one actor (tool or part) with a default pose and
//                 a set of degrees of freedom.
//
// MachineDefinition — the complete machine: two actors.
//
// The design is deliberately symmetric: a "perfect" machine with
// 12 free DOF total would have both actors fully unconstrained.
// A real machine collapses that freedom via its DOF masks.
// ------------------------------------------------------------------

export namespace Cam::Machine {

    using Rev::Core::Pos3;

    struct MachineActor {
        Pose      defaultPose;  // pose with all DOF at their zero position
        MachineDOF dof;
    };

    struct MachineDefinition {

        MachineActor tool;   // the cutting tool
        MachineActor part;   // the workpiece

        // -- Convenience factories -------------------------------------

        // 3-axis vertical mill.
        // Tool translates freely in XYZ; part is fixed.
        static MachineDefinition ThreeAxis() {
            return {
                .tool = { Pose::identity(), MachineDOF::XYZ() },
                .part = { Pose::identity(), MachineDOF::Fixed() }
            };
        }

        // Indexed 3+1 with A-axis (rotation around X) on the part.
        // Tool translates XYZ; part rotates around A through partOrigin.
        static MachineDefinition ThreePlusOneA(Pos3 partOrigin = {}) {
            return {
                .tool = { Pose::identity(),              MachineDOF::XYZ() },
                .part = { Pose{ partOrigin, {0,0,1} },   MachineDOF::RotaryA() }
            };
        }

        // Indexed 3+1 with B-axis (rotation around Y) on the part.
        static MachineDefinition ThreePlusOneB(Pos3 partOrigin = {}) {
            return {
                .tool = { Pose::identity(),              MachineDOF::XYZ() },
                .part = { Pose{ partOrigin, {0,0,1} },   MachineDOF::RotaryB() }
            };
        }

        // Indexed 3+1 with an arbitrary user-defined rotary axis.
        // Use this when the A-axis direction is not aligned with world X —
        // e.g. when the user has defined the part's own X axis (axisXDirection)
        // which may differ from {1,0,0} in world space.
        static MachineDefinition ThreePlusOne(Pos3 partOrigin, Pos3 rotaryAxis) {
            MachineDOF partDOF;
            partDOF.freeRotations = { rotaryAxis.normalized() };
            return {
                .tool = { Pose::identity(),             MachineDOF::XYZ() },
                .part = { Pose{ partOrigin, {0,0,1} },  partDOF }
            };
        }

        // Build the machine FROM a work coordinate system: its origin is the
        // rotary pivot and its local X axis is the rotary axis -- the unified view
        // in which "the rotary IS the work frame's X".  A free rx yields a 3+1
        // indexed machine; a locked rx (no rotational freedom) yields pure 3-axis.
        // This is the single bridge from "where the work frame is" (measured by
        // probing, or taken on faith from the user frame) to "what the machine can
        // do" -- the IK and every emitted cut flow from this one frame.
        static MachineDefinition fromWorkFrame(const Cam::Coord::CoordinateSystem& work) {
            const Pos3 pivot = work.apply({ 0.0f, 0.0f, 0.0f });
            if (work.rx.isFree()) {
                const Pos3 rotaryAxis = work.applyDirection({ 1.0f, 0.0f, 0.0f });
                return ThreePlusOne(pivot, rotaryAxis);
            }
            return ThreeAxis();
        }

        // -- The DOF, expressed as CoordinateSystems -------------------
        //
        // The machine's freedom IS a coordinate system: a frame whose axes carry a
        // maxSpeed (free vs locked).  An actor's first free rotary becomes the
        // frame's local X (so rx is the rotary), and its free translations light up
        // the matching x/y/z.  The IK then solves directly on these frames.

        static Cam::Coord::CoordinateSystem actorFrame(const MachineActor& a) {
            Cam::Coord::CoordinateSystem cs;

            // Orientation: put the (first) rotary on local X so rx is the rotary.
            if (!a.dof.freeRotations.empty()) {
                cs = Cam::Coord::CoordinateSystem::fromAxisX(
                    a.defaultPose.position, a.dof.freeRotations[0]);
                cs.rx.maxSpeed = 1.0;   // free rotation (machine-driven)
            }
            else {
                cs.t = a.defaultPose.position;
                cs.resolveAxes();
            }

            // Translations: with identity orientation the world axes map straight to
            // x/y/z; light up whichever the actor can drive.
            for (const Pos3& T : a.dof.freeTranslations) {
                if (std::fabs(T.x) > 0.5f) { cs.x.maxSpeed = 1.0; }
                if (std::fabs(T.y) > 0.5f) { cs.y.maxSpeed = 1.0; }
                if (std::fabs(T.z) > 0.5f) { cs.z.maxSpeed = 1.0; }
            }
            return cs;
        }

        Cam::Coord::CoordinateSystem toolFrame() const { return actorFrame(tool); }
        Cam::Coord::CoordinateSystem partFrame() const { return actorFrame(part); }
    };
}
