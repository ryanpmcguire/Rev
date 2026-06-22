module;

#include <string>
#include <vector>
#include <cstdint>

export module Machine.Operation;

import Machine.Command;
import Machine.Adapter;

// Operations live in their own namespace; Type tags the kind for downcasting.
export namespace Machine::Operation {

    enum class Type {
        Sequence,      // a plain ordered batch
        Home,
        ToolChange,
        Jog,
        Probe,
        Program
    };

    // An operation is a FIFO queue of commands with a `current` (front) command --
    // the same queue/current shape as the Operations manager that owns it, so the two
    // read alike. The metadata, the command-sending tick, and finished() are the
    // operation-level exceptions.
    struct OperationBase {

        Type type;

        // Metadata
        std::string name;                // human label
        float progress        = 0.0f;    // 0..1, best-effort
        float expectedSeconds = 0.0f;    // best-effort estimate
        uint64_t clock        = 0;       // schedule clock for paced streaming

        // Command queue + current pointer
        std::vector<Command::CommandBase*> queue;
        Command::CommandBase* current = nullptr;

        // Telemetry implied by the commands sent so far. Seeded by the manager on
        // enqueue, then advanced by tick as each command goes out.
        Implied implied;

        OperationBase(Type type, std::string name) : type(type), name(std::move(name)) {}
        virtual ~OperationBase() { for (auto* command : queue) { delete command; } }

        // Queue a command; it runs once it reaches the front.
        void enqueue(Command::CommandBase* command) {
            queue.push_back(command);
            if (!current) { current = command; }   // nothing pending -- it is next
        }

        void advance() {

            if (queue.empty()) { return; }
            delete queue.front();

            queue.erase(queue.begin());
            current = queue.empty() ? nullptr : queue.front();
        }

        // Drop everything still pending.
        void clear() {

            for (auto* command : queue) { delete command; }
            queue.clear();

            current = nullptr;
        }

        // Send the pending commands. Streaming operations override to pace them.
        // Each sent command folds its effect into our implied telemetry.
        virtual void tick(Adapter& adapter, uint64_t now) {
            while (current) {
                adapter.sendCommand(*current);
                implied.apply(*current);
                advance();
            }
        }

        // True once the queue is drained.
        virtual bool finished() const { return queue.empty(); }
    };
}
