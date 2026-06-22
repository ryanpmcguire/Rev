module;

#include <cmath>
#include <cstdint>

export module Machine.Machines.Carvera.Operations;

import Machine.Operation;
import Machine.Adapter;
import Machine.Machines.Carvera.Commands;

// Carvera's concrete operations. They produce Carvera commands, so they live in
// the Carvera layer; the machine drains them through the generic OperationBase.
export namespace Machine::Carvera::Operation {

    using Base = Machine::Operation::OperationBase;

    // A continuous jog: streams short absolute machine-frame moves, pacing itself so
    // only a small lookahead is ever queued -- a key release then coasts just that
    // far, not a whole buffer. The frontier is the last queued target, anchored at
    // the live machine position when the jog opens.
    struct Jog : Base {

        // Directive
        float dirX = 0, dirY = 0, dirZ = 0, dirA = 0;   // per-axis sign
        int   speed     = 0;                            // mm/min
        float segmentMm = 1.0f;                         // travel quantum

        // Runtime
        float frontierX = 0, frontierY = 0, frontierZ = 0, frontierA = 0;
        float queuedMs = 0.0f;   // motion time queued ahead of expected progress
        bool  holding  = false;  // keep refilling while held

        static constexpr float TargetMs    = 250.0f;   // keep ~this much motion time queued (smoothness)
        static constexpr float MaxQueuedMm = 10.0f;    // ...but never more than this distance, any speed

        Jog() : Base(Type::Jog, "Jog") {}

        // Consume the time elapsed this tick, then (while held) refill the buffer up
        // to the time target -- but never past the distance cap (10 mm expressed as
        // time at this feed), so a key release coasts at most that far.
        void tick(Machine::Adapter& adapter, float dtMs) override {

            queuedMs -= dtMs;
            if (queuedMs < 0.0f) { queuedMs = 0.0f; }

            const bool moving = dirX != 0.0f || dirY != 0.0f || dirZ != 0.0f || dirA != 0.0f;
            if (!holding || speed <= 0 || !moving) { return; }

            float target = TargetMs;
            const float capMs = MaxQueuedMm / speed * 60000.0f;
            if (capMs < target) { target = capMs; }

            while (queuedMs < target) { emitSegment(adapter); }
        }

        // Finished once released; the small lookahead already queued coasts out.
        bool finished() const override { return !holding; }

        // Advance the frontier one segment and send the absolute move for it, pacing
        // off the move's own advertised duration.
        void emitSegment(Machine::Adapter& adapter) {

            frontierX += dirX * segmentMm;
            frontierY += dirY * segmentMm;
            frontierZ += dirZ * segmentMm;
            frontierA += dirA * segmentMm;

            Command::GoTo move(speed);
            if (dirX != 0.0f) { move.x = { true, frontierX }; }
            if (dirY != 0.0f) { move.y = { true, frontierY }; }
            if (dirZ != 0.0f) { move.z = { true, frontierZ }; }
            if (dirA != 0.0f) { move.a = { true, frontierA }; }

            // How long this move should take: path length over feed.
            const float linear = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ) * segmentMm;
            const float travel = linear > 0.0f ? linear : std::abs(dirA) * segmentMm;
            const float ms     = speed > 0 ? travel / speed * 60000.0f : 0.0f;
            move.dt = static_cast<uint64_t>(ms);

            adapter.sendCommand(move);

            queuedMs += ms;
        }
    };
}
