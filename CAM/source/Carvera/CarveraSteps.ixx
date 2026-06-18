module;

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

export module Carvera.Steps;

import Cam.Machine.Operation;   // Operation / Path / Waypoint

// ------------------------------------------------------------------
// Carvera::Step + the abstract-Operation -> wire-Step translation.
//
// This is the CARVERA-SPECIFIC lowering: it turns the machine-agnostic typed
// Operations (Cam::Machine::Operation) into the low-level Step pipeline Air
// streams as G-code.  It is a pure translation -- the only machine state it
// needs is whether the spindle is armed (a dry run suppresses M3), passed in --
// so it lives apart from Air's connection / streaming / state.
// ------------------------------------------------------------------

export namespace Carvera {

    using Cam::Machine::Operation;
    using Cam::Machine::Path;
    using Cam::Machine::Waypoint;

    struct Step {
        // Probe = G38.2 toward (x,y,z,a) until contact (the "intersect" path).
        // Note  = a runtime log line (not sent to the machine); marks the
        //         boundaries of typed operations so the log narrates the work.
        enum class Kind { Move, ToolChange, Spindle, Dwell, Raw, Probe, Note };
        Kind kind = Kind::Raw;

        // Move: feed <= 0 -> rapid G0, feed > 0 -> G1 F<feed>.
        double x = 0, y = 0, z = 0, a = 0;
        double feed = 0;

        int         slot    = 0;   // ToolChange
        double      rpm     = 0;   // Spindle (> 0 -> M3 S<rpm>, else M5)
        double      seconds = 0;   // Dwell (G4 P<seconds>)
        std::string raw;           // Raw G-code line / Note message

        static Step moveTo(double x, double y, double z, double a, double feed) {
            Step s; s.kind = Kind::Move; s.x = x; s.y = y; s.z = z; s.a = a; s.feed = feed; return s;
        }
        static Step probeTo(double x, double y, double z, double a, double feed) {
            Step s; s.kind = Kind::Probe; s.x = x; s.y = y; s.z = z; s.a = a; s.feed = feed; return s;
        }
        static Step toolChange(int slot) { Step s; s.kind = Kind::ToolChange; s.slot = slot; return s; }
        static Step spindle(double rpm)   { Step s; s.kind = Kind::Spindle;    s.rpm  = rpm;  return s; }
        static Step dwell(double seconds) { Step s; s.kind = Kind::Dwell;      s.seconds = seconds; return s; }
        static Step raw_(std::string g)   { Step s; s.kind = Kind::Raw;        s.raw  = std::move(g); return s; }
        static Step note(std::string m)   { Step s; s.kind = Kind::Note;       s.raw  = std::move(m); return s; }
    };

    // Translate typed Operations into the Step pipeline.  `spindleArmed` gates the
    // spindle spin-up (a dry run suppresses it).  A negative tool slot means "do
    // not change tools -- use whatever is loaded".  Probes are emitted as a
    // RELATIVE G91 G38.2 plunge from the standoff (anchored wherever the tool is
    // when it runs), bracketed by G90.
    inline std::vector<Step> buildSteps(const std::vector<Operation>& ops, bool spindleArmed) {

        std::vector<Step> program;
        program.push_back(Step::raw_("G90\n"));   // absolute positioning

        for (const Operation& op : ops) {

            const char* kindName = (op.kind == Operation::Kind::Probe) ? "Probe" : "Cut";
            program.push_back(Step::note(std::format(
                "== {} operation: T{}{} ==",
                kindName, op.toolSlot,
                op.toolName.empty() ? std::string() : " (" + op.toolName + ")")));

            program.push_back(Step::spindle(0.0));
            if (op.toolSlot >= 0) {
                program.push_back(Step::toolChange(op.toolSlot));
            }

            bool   spindleOn = false;
            double lastX = 0, lastY = 0, lastZ = 0, lastA = 0;
            bool   havePos = false;

            for (const Path& path : op.paths) {

                if (op.kind == Operation::Kind::Cut &&
                    path.kind == Path::Kind::Cut &&
                    op.rpm > 0.0 && !spindleOn && spindleArmed) {
                    program.push_back(Step::spindle(op.rpm));
                    spindleOn = true;
                }

                for (const Waypoint& w : path.points) {
                    switch (path.kind) {
                        case Path::Kind::Travel:
                            program.push_back(Step::moveTo(w.x, w.y, w.z, w.a, 0.0));   // rapid
                            break;
                        case Path::Kind::Cut:
                            program.push_back(Step::moveTo(w.x, w.y, w.z, w.a, path.feed));
                            break;
                        case Path::Kind::Intersect: {
                            const double dx = havePos ? w.x - lastX : 0.0;
                            const double dy = havePos ? w.y - lastY : 0.0;
                            const double dz = havePos ? w.z - lastZ : 0.0;
                            const double da = havePos ? w.a - lastA : 0.0;
                            program.push_back(Step::raw_("G91\n"));
                            program.push_back(Step::probeTo(dx, dy, dz, da, path.feed));
                            program.push_back(Step::raw_("G90\n"));
                            break;
                        }
                    }
                    lastX = w.x; lastY = w.y; lastZ = w.z; lastA = w.a;
                    havePos = true;
                }
            }

            if (spindleOn) { program.push_back(Step::spindle(0.0)); }
        }

        program.push_back(Step::spindle(0.0));   // belt-and-braces spindle off
        return program;
    }

    // The safe clearance height for the program: above the highest commanded move.
    inline double computeClearance(const std::vector<Step>& program) {
        double maxZ = 0.0;
        bool   any  = false;
        for (const Step& s : program) {
            if (s.kind == Step::Kind::Move) {
                maxZ = any ? std::max(maxZ, s.z) : s.z;
                any  = true;
            }
        }
        return (any ? maxZ : 0.0) + 5.0;
    }
}
