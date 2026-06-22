module;

#include <string>
#include <vector>
#include <cstdint>

export module Machine.Operation;

import Machine.Command;
import Machine.Adapter;

// Operations live in their own namespace; Type tags the kind for downcasting.
export namespace Machine::Operation {

    // An ordered command list plus a cursor marking the next to send.
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
        float progress        = 0.0f;  // 0..1, best-effort
        float expectedSeconds = 0.0f;  // best-effort estimate

        // The work: owned commands and a cursor into them (next to send).
        std::vector<Command::CommandBase*> commands;
        size_t cursor = 0;

        // Running schedule clock: the time the next unsent command should execute.
        uint64_t clock = 0;

        OperationBase(Type type, std::string name) : type(type), name(std::move(name)) {}
        virtual ~OperationBase() { for (auto* command : commands) { delete command; } }

        // Build -- append a command (ownership transfers to the operation).
        OperationBase& add(Command::CommandBase* command) { commands.push_back(command); return *this; }

        // Send the pending commands. Streaming operations override.
        virtual void tick(Adapter& adapter, uint64_t now) {
            while (Command::CommandBase* command = peek()) {
                adapter.sendCommand(*command);
                advance();
            }
        }

        // The next command to send, or null when the cursor has reached the end.
        Command::CommandBase* peek() const { return cursor < commands.size() ? commands[cursor] : nullptr; }

        // The last command sent (behind the cursor), or null if none yet.
        Command::CommandBase* lastSent() const { return cursor > 0 ? commands[cursor - 1] : nullptr; }

        // Consume the command peek() returned: advance the cursor (the command stays).
        void advance() {
            if (cursor >= commands.size()) { return; }
            ++cursor;
            progress = commands.empty() ? 0.0f : static_cast<float>(cursor) / static_cast<float>(commands.size());
        }

        // True once every command has been sent.
        virtual bool finished() const { return cursor >= commands.size(); }
    };
}
