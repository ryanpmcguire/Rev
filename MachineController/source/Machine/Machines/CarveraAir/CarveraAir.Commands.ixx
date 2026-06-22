module;

#include <string>
#include <format>

export module Machine.Machines.Carvera.Commands;

import Machine.Command;

// The Carvera command vocabulary: one struct per command, each emit()s its wire
// form. They live in Carvera's own Command namespace and derive from the shared
// Machine::Command::CommandBase. Reference them as Carvera::Command::Unlock, ...
export namespace Machine::Carvera::Command {

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

    // An absolute move in MACHINE coordinates (G53), streamed under the hood by the
    // jog. Only the "active" axes are commanded; the rest hold.
    struct GoTo : Base {
        struct Axis { bool active = false; float value = 0.0f; };
        Axis x, y, z, a;
        int  feed = 1000;

        GoTo(int feed) : Base(Type::GoTo), feed(feed) {}
        std::string emit() const override {
            const struct { char label; const Axis& axis; } axes[] = { {'X', x}, {'Y', y}, {'Z', z}, {'A', a} };

            std::string cmd = "G53 G1";   // feed move, machine coordinates
            for (const auto& [label, axis] : axes) {
                if (axis.active) { cmd += std::format(" {}{:.3f}", label, axis.value); }
            }
            cmd += std::format(" F{}\n", feed);
            return cmd;
        }
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
