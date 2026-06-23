module;

#include <string>
#include <cstdint>

export module Machine.Command;

import Machine.Types;

// Forward declared so CommandBase::applyImplied can name it; defined below.
export namespace Machine { struct Implied; }

// Commands live in their own namespace: one base (CommandBase) subclassed per
// intent. A concrete machine defines its own commands (in its own Command
// namespace) deriving from this base; `type` tags the kind so a stored
// CommandBase* can be downcast.
export namespace Machine::Command {

    // Command types (allows for later casting)
    enum class Type {

        // Actions
        Unlock,
        Reset,
        Home,
        Feed,
        GoTo,
        ChangeTool,

        // Queries
        QueryStatus,
        QueryOffsets,
        QueryState,
        QuerySwitches,
        QueryVersion
    };

    struct CommandBase {

        Type type;

        // Estimated execution time, ms (0 = unknown). Set by producers that know the
        // move (feed + delta) so readers can pace / schedule.
        uint64_t dt = 0;

        // Absolute time, ms, at which this command should execute (0 = unscheduled).
        // Auto-filled when the queue first reaches the command.
        uint64_t t = 0;

        // Construct/destruct
        CommandBase(Type type) : type(type) {}
        virtual ~CommandBase() {}

        // Stringify command (derived classes must implement)
        virtual std::string emit() const = 0;

        // Fold this command's effect into implied telemetry (no-op by default).
        virtual void applyImplied(Implied&) const {}
    };
}

export namespace Machine {

    // Telemetry implied by the commands sent so far. apply() defers to the command's
    // own effect, keeping wire knowledge in the commands.
    struct Implied {
        Coord pos;        // last commanded position (machine coords)
        int   feed = 0;   // last commanded feedrate (mm/min)

        void apply(const Command::CommandBase& command) { command.applyImplied(*this); }
    };
}
