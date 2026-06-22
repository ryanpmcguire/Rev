module;

#include <string>
#include <vector>

export module Machine.Operation;

import Machine.Command;
import Machine.Adapter;

// An operation lives in its own namespace: one base (OperationBase) carrying a
// Type tag, metadata, and the commands it hands the machine. Like commands and
// events, the Type lets a stored OperationBase* be downcast. Finite operations
// fill their command list up front; streaming ones replenish it in service().
export namespace Machine::Operation {

    // A typed unit of work: an owned FIFO of commands the machine drains in order.
    // The machine peek()s the next command, sends it, then advance()s -- it never
    // needs to know what the operation is. A streaming operation overrides service()
    // to top its commands up over time and finished() to say when it is spent.
    struct OperationBase {

        enum class Type {
            Sequence,      // a plain ordered batch
            Home,
            ToolChange,
            Jog,
            Probe,
            Program
        };

        Type type;

        // Metadata
        std::string name;                    // human label
        float       progress        = 0.0f;  // 0..1, best-effort
        float       expectedSeconds = 0.0f;  // best-effort estimate

        // The work: owned commands, drained from the front.
        std::vector<Command::CommandBase*> commands;

        OperationBase(Type type, std::string name) : type(type), name(std::move(name)) {}
        virtual ~OperationBase() { for (auto* command : commands) { delete command; } }

        // Build -- append a command (ownership transfers to the operation).
        OperationBase& add(Command::CommandBase* command) { commands.push_back(command); return *this; }

        // Pump this operation through the adapter for one machine tick (`dtMs` since
        // the last). The default sends every queued command; a streaming operation
        // overrides this to generate and send on the fly.
        virtual void tick(Adapter& adapter, float /*dtMs*/) {
            while (Command::CommandBase* command = peek()) {
                adapter.sendCommand(*command);
                advance();
            }
        }

        // The next command to send, or null when none is pending right now.
        Command::CommandBase* peek() const { return commands.empty() ? nullptr : commands.front(); }

        // Consume the command peek() returned.
        void advance() {
            if (commands.empty()) { return; }
            delete commands.front();
            commands.erase(commands.begin());
        }

        // True once this operation will never produce another command.
        virtual bool finished() const { return commands.empty(); }
    };
}
