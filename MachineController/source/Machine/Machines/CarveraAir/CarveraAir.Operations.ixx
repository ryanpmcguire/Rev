module;

#include <cmath>
#include <cstdint>

#include <dbg.hpp>

export module Machine.Machines.Carvera.Operations;

import Machine.Base;
import Machine.Operation;
import Machine.Adapter;
import Machine.Machines.Carvera.Commands;

// Carvera's concrete operations: they produce Carvera commands.
export namespace Machine::Carvera::Operation {

    using Base = Machine::Operation::OperationBase;
    using Type = Machine::Operation::Type;

    // Jog-type operation
    struct Jog : Base {

        Coord direction;

        static constexpr int FeedRate = 1000;   // mm/min, fixed for now

        Jog() : Base(Type::Jog, "Jog") {

            dbg("[Jog] Creating");
        }

        // Queue one absolute step to implied.pos + direction (scaled by the step): set
        // the modal feed, then move there. implied.pos advances as the moves go out
        // (Base::tick), so successive steps build on each other.
        void setDir(Coord dir, float stepMm, float stepDeg, bool hold) {

            direction = dir;

            Coord target = {
                implied.pos.x + dir.x * stepMm,
                implied.pos.y + dir.y * stepMm,
                implied.pos.z + dir.z * stepMm,
                implied.pos.a + dir.a * stepDeg,
            };

            enqueue(new Command::Feed(FeedRate));
            enqueue(new Command::GoTo(target));
        }

        // Implied telemetry does the position tracking now; nothing extra to intercept.
        void tick(Adapter& adapter, uint64_t now) override {
            Base::tick(adapter, now);
        }
    };
}
