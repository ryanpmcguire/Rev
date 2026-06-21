module;

export module App.Machines.CarveraAir;

import App.Machine;
import App.Machines.Carvera.Adapter;

export namespace App {

    // A concrete Machine: owns the Carvera adapter and binds the base Machine to it.
    struct CarveraAir : public Machine {

        Carvera::Adapter* carveraAdapter = nullptr;

        CarveraAir() {
            carveraAdapter = new Carvera::Adapter();
            bindAdapter(carveraAdapter);
        }

        ~CarveraAir() {
            // Destroy the adapter (and the transport inside it) before the base
            // Machine's dispatchers go: the bound adapter feeds those dispatchers.
            delete carveraAdapter;
        }
    };
}
