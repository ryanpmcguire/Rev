module;

#include <dbg.hpp>

export module App.Application;

import Rev.Application;

import Machine.Events;
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
                dbg("[Machine] connection: %s", Machine::connectionStatusName(machine->info.network.status));
            });

            // Diagnostic: log telemetry as it arrives.
            machine->telemetry.onUpdate(this, [this]() {
                Machine::MachineBase::Telemetry& t = machine->telemetry;
                dbg("[Telemetry] spindle pos %.3f,%.3f,%.3f rpm %.0f/%.0f load %.0f%% temp %.1fC | "
                    "tool pos %.3f,%.3f,%.3f feed %.0f/%.0f T%d off %.3f | laser %.0f",
                    t.spindle.pos.x, t.spindle.pos.y, t.spindle.pos.z,
                    t.spindle.rpm, t.spindle.target, t.spindle.load, t.spindle.temp,
                    t.tool.pos.x, t.tool.pos.y, t.tool.pos.z,
                    t.tool.speed, t.tool.speedTarget, t.tool.number, t.tool.offset,
                    t.laser.power);
            });
        }

        // Destroy
        //--------------------------------------------------

        ~Application() {
            delete machine;
        }
    };
}
