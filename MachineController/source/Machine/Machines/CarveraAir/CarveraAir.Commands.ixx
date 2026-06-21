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

    // A relative ($J=G91) jog: each non-zero axis moves by its delta at `feed`.
    // Zero axes are omitted so the planner never reads "go to here" on an axis.
    struct Jog : Base {
        float x = 0, y = 0, z = 0, a = 0;
        int   feed = 1000;
        Jog(float x, float y, float z, float a, int feed)
            : Base(Type::Jog), x(x), y(y), z(z), a(a), feed(feed) {}
        std::string emit() const override {
            std::string cmd = "$J=G91";
            if (x != 0.0f) { cmd += std::format(" X{:.3f}", x); }
            if (y != 0.0f) { cmd += std::format(" Y{:.3f}", y); }
            if (z != 0.0f) { cmd += std::format(" Z{:.3f}", z); }
            if (a != 0.0f) { cmd += std::format(" A{:.3f}", a); }
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
