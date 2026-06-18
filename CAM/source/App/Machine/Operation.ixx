module;

#include <string>
#include <utility>
#include <vector>

export module Cam.Machine.Operation;

// ------------------------------------------------------------------
// The abstract MACHINE's execution vocabulary -- independent of any
// particular controller.  A program is a list of Operations; each Operation
// is a tool + a list of Paths; each Path is a kind (travel / cut / probe-
// intersect) + waypoints.  A concrete machine link (Carvera::Air, or any
// other) translates these into its own wire protocol.
//
// These belong to the MACHINE, not to one controller -- so they live here and
// the controller merely aliases them.
// ------------------------------------------------------------------

export namespace Cam::Machine {

    // The outcome of a machine request (start / enqueue / validate / ...).
    struct OperationResult {
        bool        ok = true;
        std::string reason;

        static OperationResult success()              { return { true, "" }; }
        static OperationResult failure(std::string r) { return { false, std::move(r) }; }
    };

    // One waypoint in a path: a target pose in the machine/work frame the
    // caller already resolved (X/Y/Z linear + the rotary A).
    struct Waypoint { double x = 0, y = 0, z = 0, a = 0; };

    struct Path {
        // Travel    = rapid repositioning (nothing engaging).
        // Cut       = feed-rate cutting move (spindle on for a Cut op).
        // Intersect = drive slowly until the probe contacts the part (e.g. G38.2);
        //             the contact is reported back as a probe event.
        enum class Kind { Travel, Cut, Intersect };

        Kind kind = Kind::Travel;
        std::vector<Waypoint> points;
        double feed = 0;   // Cut: cutting feed; Intersect: probe feed; Travel: ignored

        static Path travel()              { Path p; p.kind = Kind::Travel;    return p; }
        static Path cut(double feed)      { Path p; p.kind = Kind::Cut;       p.feed = feed; return p; }
        static Path intersect(double feed){ Path p; p.kind = Kind::Intersect; p.feed = feed; return p; }
    };

    struct Operation {
        enum class Kind { Cut, Probe };

        Kind        kind = Kind::Cut;
        int         toolSlot = 0;   // tool to ensure loaded for this operation
        std::string toolName;       // human label (logs only)
        double      rpm = 0;        // Cut spindle RPM (0 / Probe => spindle never runs)

        std::vector<Path> paths;

        static Operation cut(int slot, std::string name, double rpm) {
            Operation o; o.kind = Kind::Cut; o.toolSlot = slot; o.toolName = std::move(name); o.rpm = rpm; return o;
        }
        static Operation probe(int slot, std::string name) {
            Operation o; o.kind = Kind::Probe; o.toolSlot = slot; o.toolName = std::move(name); o.rpm = 0; return o;
        }
    };
}
