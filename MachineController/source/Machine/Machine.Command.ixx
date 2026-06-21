module;

#include <string>

export module Machine.Command;

export namespace Machine {

    // An intent sent to a machine. Each concrete command emit()s its own wire
    // form; `type` tags the kind so a stored Command* can be downcast.
    struct Command {

        enum class Type {
            // Actions
            Unlock,
            Reset,
            ChangeTool,
            // Queries
            QueryStatus,
            QueryOffsets,
            QueryState,
            QuerySwitches,
            QueryVersion
        };

        Type type;

        Command(Type type) : type(type) {}
        virtual ~Command() {}

        virtual std::string emit() const = 0;
    };
}
