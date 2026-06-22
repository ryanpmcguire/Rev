module;

#include <string>
#include <cstdint>

export module Machine.Command;

// Commands live in their own namespace: one base (CommandBase) subclassed per
// intent. A concrete machine defines its own commands (in its own Command
// namespace) deriving from this base; `type` tags the kind so a stored
// CommandBase* can be downcast.
export namespace Machine::Command {

    struct CommandBase {

        // Command types (allows for later casting)
        enum class Type {

            // Actions
            Unlock,
            Reset,
            Home,
            GoTo,
            ChangeTool,

            // Queries
            QueryStatus,
            QueryOffsets,
            QueryState,
            QuerySwitches,
            QueryVersion
        };

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
    };
}
