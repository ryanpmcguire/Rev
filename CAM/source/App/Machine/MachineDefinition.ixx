module;

export module Cam.Machine.Definition;

import Cam.Machine.Pose;
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
    };
}
