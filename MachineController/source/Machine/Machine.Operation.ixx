module;

#include <string>
#include <vector>

export module Machine.Operation;

import Machine.Command;

// An operation lives in its own namespace: one base (OperationBase) carrying a
// Type tag, metadata, and an ordered list of commands. Like commands and events,
// the Type lets a stored OperationBase* be downcast. Richer operations subclass it
// to add their own fields. Reference as Operation::OperationBase (and, later,
// Operation::Probe / Operation::Program / ...).
export namespace Machine::Operation {

    // A typed, ordered batch of commands with progress metadata. Owns its commands
    // (deletes them). The cursor marks the next command to send; advance() consumes
    // one and recomputes progress.
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
        std::string name;                       // human label
        float       progress        = 0.0f;     // 0..1, advanced as commands complete
        float       expectedSeconds = 0.0f;     // best-effort estimate

        // The work: an ordered, owned list of commands + a cursor into it.
        std::vector<Command::CommandBase*> commands;
        size_t                             cursor = 0;

        OperationBase(Type type, std::string name) : type(type), name(std::move(name)) {}
        virtual ~OperationBase() { for (auto* command : commands) { delete command; } }

        // Build -- append a command (ownership transfers to the operation).
        OperationBase& add(Command::CommandBase* command) { commands.push_back(command); return *this; }

        // Query
        bool   complete()  const { return cursor >= commands.size(); }
        size_t remaining() const { return commands.size() - cursor; }

        // The next command to send (null when complete).
        Command::CommandBase* peek() const { return cursor < commands.size() ? commands[cursor] : nullptr; }

        // Consume the next command and recompute progress.
        void advance() {
            if (cursor < commands.size()) { ++cursor; }
            progress = commands.empty() ? 1.0f : static_cast<float>(cursor) / static_cast<float>(commands.size());
        }
    };

    // A continuous-jog directive: "keep moving along this direction at this speed".
    // Unlike a finite operation it holds no fixed command list -- the machine reads
    // the metadata and streams short absolute moves on the fly (see CarveraAir),
    // pacing itself so the open-loop frontier stays in step with the real motion.
    struct Jog : OperationBase {

        // Directive
        float dirX = 0, dirY = 0, dirZ = 0, dirA = 0;   // unit direction (per-axis sign)
        int   speed     = 0;                            // mm/min
        float segmentMm = 1.0f;                         // quantization: travel is emitted in these steps

        // Runtime
        float frontierX = 0, frontierY = 0, frontierZ = 0, frontierA = 0;   // last queued target (segment grid from start)
        float queuedMm = 0.0f;                          // emitted distance still ahead of expected progress
        bool  holding  = false;                         // true => keep refilling (continuous); false => one-shot / draining

        Jog() : OperationBase(Type::Jog, "Jog") {}
    };
}
