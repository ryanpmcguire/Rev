module;

#include <string>
#include <cstdint>

export module Machine.Command;

import Machine.Types;

// Implied telemetry lives here (its apply() asks a command for its own effect, so it
// needs CommandBase); forward declared so CommandBase can name it.
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

        // Fold this command's effect into implied telemetry: once it is sent, the
        // machine will reach this state. Commands with no implied effect (queries,
        // unlock, ...) leave it untouched. Reached via Implied::apply.
        virtual void applyImplied(Implied&) const {}
    };
}

export namespace Machine {

    // Telemetry implied by the commands sent so far: once a command goes out, the
    // machine *will* reach this state. apply() folds a command in by asking it for its
    // own effect, so command knowledge stays in the commands. The Operations manager
    // threads this from one operation to the next so a fresh op starts from a guess.
    struct Implied {
        Coord pos;        // last commanded position (machine coords)
        int   feed = 0;   // last commanded feedrate (mm/min)

        void apply(const Command::CommandBase& command) { command.applyImplied(*this); }
    };
}
