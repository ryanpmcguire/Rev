module;

#include <string>

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
            Jog,
            ChangeTool,

            // Queries
            QueryStatus,
            QueryOffsets,
            QueryState,
            QuerySwitches,
            QueryVersion
        };

        Type type;

        // Construct/destruct
        CommandBase(Type type) : type(type) {}
        virtual ~CommandBase() {}

        // Stringify command (derived classes must implement)
        virtual std::string emit() const = 0;
    };
}
