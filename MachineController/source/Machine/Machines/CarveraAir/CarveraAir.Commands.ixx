module;

#include <string>

export module Machine.Machines.Carvera.Commands;

import Machine.Command;

export namespace Machine::Carvera {

    // The Carvera command vocabulary: one struct per command, each emit()s its
    // wire form. (`Command` is Machine::Command, the parent-namespace base.)

    // Actions
    //--------------------------------------------------

    struct Unlock : Command {
        Unlock() : Command(Command::Type::Unlock) {}
        std::string emit() const override { return "$X\n"; }                 // clear alarm
    };

    struct Reset : Command {
        Reset() : Command(Command::Type::Reset) {}
        std::string emit() const override { return std::string(1, '\x18'); } // ctrl-x soft reset
    };

    struct ChangeTool : Command {
        int tool = 0;
        ChangeTool(int tool) : Command(Command::Type::ChangeTool), tool(tool) {}
        std::string emit() const override { return "M6 T" + std::to_string(tool) + "\n"; }
    };

    // Queries -- the reply is decoded by the adapter into telemetry / info.
    //--------------------------------------------------

    struct QueryStatus : Command {
        QueryStatus() : Command(Command::Type::QueryStatus) {}
        std::string emit() const override { return "?"; }                    // live status frame
    };

    struct QueryOffsets : Command {
        QueryOffsets() : Command(Command::Type::QueryOffsets) {}
        std::string emit() const override { return "$#\n"; }                 // WCS / TLO / PRB
    };

    struct QueryState : Command {
        QueryState() : Command(Command::Type::QueryState) {}
        std::string emit() const override { return "$G\n"; }                 // modal / parser state
    };

    struct QuerySwitches : Command {
        QuerySwitches() : Command(Command::Type::QuerySwitches) {}
        std::string emit() const override { return "$S\n"; }                 // switch states
    };

    struct QueryVersion : Command {
        QueryVersion() : Command(Command::Type::QueryVersion) {}
        std::string emit() const override { return "version\n"; }            // firmware version
    };
}
