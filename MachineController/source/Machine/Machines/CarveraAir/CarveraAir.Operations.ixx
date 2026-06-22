module;

#include <cmath>
#include <cstdint>

export module Machine.Machines.Carvera.Operations;

import Machine.Operation;
import Machine.Adapter;
import Machine.Machines.Carvera.Commands;

// Carvera's concrete operations: they produce Carvera commands.
export namespace Machine::Carvera::Operation {

    using Base = Machine::Operation::OperationBase;

    // The last absolute position commanded; anchors a new jog in place of telemetry.
    struct JogAnchor {
        float x = 0, y = 0, z = 0, a = 0;
        bool  valid = false;
    };

    // A jog. influence() sets the direction and tops the buffer up to the auto-cancel
    // distance; tick() sends what is buffered. It generates nothing on its own -- once
    // the buffered motion elapses without a fresh influence, it is finished.
    struct Jog : Base {

        JogAnchor* anchor = nullptr;   // machine's anchor (not owned)

        float dirX = 0, dirY = 0, dirZ = 0, dirA = 0;   // direction vector (per-axis sign)
        int   speed = 0;                                // mm/min
        float frontierX = 0, frontierY = 0, frontierZ = 0, frontierA = 0;
        bool  spent = false;

        static constexpr float StreamSegmentMs = 20.0f;   // motion time per generated move

        Jog() : Base(Type::Jog, "Jog") {}

        // Set the direction and fill the buffer so it leads `now` by autoCancelMm.
        void influence(float dx, float dy, float dz, float da, float autoCancelMm, int spd, uint64_t now) {

            dirX = dx; dirY = dy; dirZ = dz; dirA = da; speed = spd;

            const bool moving = dirX != 0.0f || dirY != 0.0f || dirZ != 0.0f || dirA != 0.0f;
            if (!moving || speed <= 0) { return; }

            if (clock < now) { clock = now; }

            const float    seg  = speed / 60.0f * (StreamSegmentMs / 1000.0f);
            const uint64_t span = static_cast<uint64_t>(autoCancelMm / speed * 60000.0f);

            while (clock - now < span) { addStep(seg); }
        }

        bool finished() const override { return spent; }

        // Generate one absolute move continuing the direction vector.
        void addStep(float seg) {

            frontierX += dirX * seg; frontierY += dirY * seg;
            frontierZ += dirZ * seg; frontierA += dirA * seg;

            Command::GoTo* move = new Command::GoTo(speed);
            move->x = { dirX != 0.0f, frontierX };
            move->y = { dirY != 0.0f, frontierY };
            move->z = { dirZ != 0.0f, frontierZ };
            move->a = { dirA != 0.0f, frontierA };

            const float linear = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ) * seg;
            const float travel = linear > 0.0f ? linear : std::abs(dirA) * seg;
            const uint64_t ms  = speed > 0 ? static_cast<uint64_t>(travel / speed * 60000.0f) : 0;

            move->dt = ms;
            move->t  = clock;
            add(move);

            clock += ms;
        }

        // Send buffered moves; record each as the anchor. Spent once they have elapsed.
        void tick(Machine::Adapter& adapter, uint64_t now) override {

            while (Machine::Command::CommandBase* cmd = peek()) {
                Command::GoTo* g = static_cast<Command::GoTo*>(cmd);
                if (anchor) {
                    anchor->x = g->x.value; anchor->y = g->y.value;
                    anchor->z = g->z.value; anchor->a = g->a.value;
                    anchor->valid = true;
                }
                adapter.sendCommand(*cmd);
                advance();
            }

            spent = now >= clock;
        }
    };
}
