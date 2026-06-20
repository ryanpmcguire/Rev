module;

export module App.Machine.CarveraAir;

import Machine.App.Machine;

export namespace Machine::App {

    // The CarveraAir machine: a thin Machine::Machine that owns the CarveraAir system.
    // This type holds the machine-wide state the GUI reflects.
    struct CarveraAir : public Machine {

        // Create
        //--------------------------------------------------

        CarveraAir() : Machine() {}

        // Destroy
        //--------------------------------------------------

        ~CarveraAir() {}
    };
}