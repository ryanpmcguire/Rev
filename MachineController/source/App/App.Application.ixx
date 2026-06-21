module;

#include <dbg.hpp>

export module App.Application;

import Rev.Application;

import App.Events;
import App.Machines.CarveraAir;

export namespace App {

    // A thin Rev::Application that owns the machine system. For now it is a
    // CarveraAir concretely; later this becomes a selected/owned Machine.
    struct Application : public Rev::Application {

        CarveraAir* machine = nullptr;

        // Create
        //--------------------------------------------------

        Application() : Rev::Application() {

            machine = new CarveraAir();

            // Diagnostic: trace the machine's connection transitions. (Owner is
            // us; the subscription dies with the machine we delete in ~Application.)
            machine->onConnection(this, [](ConnectionEvent& e) {
                dbg("[Machine] %s %s", connectionStatusName(e.status), e.message.c_str());
            });

            // Diagnostic: trace telemetry as it arrives. The signal carries no
            // payload -- read the machine's cached telemetry on each tick.
            machine->onTelemetry(this, [this]() {
                Machine::Telemetry& t = machine->telemetry;
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
