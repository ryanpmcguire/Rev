module;

#include <dbg.hpp>

export module App.Application;

import Rev.Application;

import Machine.Machines.CarveraAir;

export namespace App {

    // A thin Rev::Application that owns the machine system. For now it is a
    // CarveraAir concretely; later this becomes a selected/owned machine.
    struct Application : public Rev::Application {

        Machine::CarveraAir* machine = nullptr;

        // Create
        //--------------------------------------------------

        Application() : Rev::Application() {

            machine = new Machine::CarveraAir();

            // Diagnostic: log connection transitions.
            machine->info.network.onUpdate(this, [this]() {
                dbg("[Machine] connection: %s", machine->info.network.statusName());
            });

            // Diagnostic: log telemetry as it arrives.
            machine->telemetry.onUpdate(this, [this]() {
                Machine::MachineBase::Telemetry& t = machine->telemetry;
            });
        }

        // Destroy
        //--------------------------------------------------

        ~Application() {
            delete machine;
        }
    };
}
