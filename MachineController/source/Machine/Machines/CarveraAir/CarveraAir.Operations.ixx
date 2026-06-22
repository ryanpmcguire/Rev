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

    // Jog-type operation
    struct Jog : Base {

        Jog() : Base(Type::Jog, "Jog") {}


    };
}
