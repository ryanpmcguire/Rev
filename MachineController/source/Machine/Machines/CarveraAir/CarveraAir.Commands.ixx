module;

#include <string>
#include <format>

export module Machine.Machines.Carvera.Commands;

import Machine.Base;
import Machine.Command;

// The Carvera command vocabulary: one struct per command, each emit()s its wire
// form. They live in Carvera's own Command namespace and derive from the shared
// Machine::Command::CommandBase. Reference them as Carvera::Command::Unlock, ...
export namespace Machine::Carvera::Command {

    using namespace Machine::Command;
    using Base = Machine::Command::CommandBase;

    // Actions
    //--------------------------------------------------

    struct Unlock : Base {
        Unlock() : Base(Type::Unlock) {}
        std::string emit() const override { return "$X\n"; }                 // clear alarm
    };

    struct Reset : Base {
        Reset() : Base(Type::Reset) {}
        std::string emit() const override { return std::string(1, '\x18'); } // ctrl-x soft reset
    };

    struct Home : Base {
        Home() : Base(Type::Home) {}
        std::string emit() const override { return "$H\n"; }                 // home all axes
    };

    struct ChangeTool : Base {
        int tool = 0;
        ChangeTool(int tool) : Base(Type::ChangeTool), tool(tool) {}
        std::string emit() const override { return "M6 T" + std::to_string(tool) + "\n"; }
    };

    // Set the modal feedrate (mm/min). Persists on the machine for the moves that
    // follow, so motion commands stay purely geometric.
    struct Feed : Base {
        int rate = 1000;
        Feed(int rate) : Base(Command::Type::Feed), rate(rate) {}
        std::string emit() const override { return std::format("F{}\n", rate); }
        void applyImplied(Implied& imp) const override { imp.feed = rate; }
    };

    // An absolute move to a target in MACHINE coordinates (G53). Every axis is
    // commanded; an axis already at its target simply doesn't move. Feed is modal
    // (see Feed), not carried here.
    struct GoTo : Base {
        Coord target;
        GoTo(Coord target) : Base(Command::Type::GoTo), target(target) {}
        std::string emit() const override {
            return std::format("G53 G1 X{:.3f} Y{:.3f} Z{:.3f} A{:.3f}\n",
                               target.x, target.y, target.z, target.a);
        }
        void applyImplied(Implied& imp) const override { imp.pos = target; }
    };

    // Queries -- the reply is decoded by the adapter into telemetry / info.
    //--------------------------------------------------

    struct QueryStatus : Base {
        QueryStatus() : Base(Type::QueryStatus) {}
        std::string emit() const override { return "?"; }                    // live status frame
    };

    struct QueryOffsets : Base {
        QueryOffsets() : Base(Type::QueryOffsets) {}
        std::string emit() const override { return "$#\n"; }                 // WCS / TLO / PRB
    };

    struct QueryState : Base {
        QueryState() : Base(Type::QueryState) {}
        std::string emit() const override { return "$G\n"; }                 // modal / parser state
    };

    struct QuerySwitches : Base {
        QuerySwitches() : Base(Type::QuerySwitches) {}
        std::string emit() const override { return "$S\n"; }                 // switch states
    };

    struct QueryVersion : Base {
        QueryVersion() : Base(Type::QueryVersion) {}
        std::string emit() const override { return "version\n"; }            // firmware version
    };
}
