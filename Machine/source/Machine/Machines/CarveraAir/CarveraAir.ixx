module;

export module Machine.App.Machines.CarveraAir;

import Machine.App.Machine;
import Machine.App.Machines.Carvera.Adapter;

export namespace App {

    // A concrete Machine: owns the Carvera adapter and binds the base Machine to it.
    struct CarveraAir : public Machine {

        Carvera::Adapter carveraAdapter;

        CarveraAir() {
            bindAdapter(&carveraAdapter);
        }

        ~CarveraAir() {}
    };
}
