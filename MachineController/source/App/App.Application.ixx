module;

export module App.Application;

import Rev.Application;

import App.Machines.CarveraAir;

export namespace App {

    // A thin Rev::Application that owns the machine system. For now it is a
    // CarveraAir concretely; later this becomes a selected/owned Machine.
    struct Application : public Rev::Application {

        CarveraAir machine;

        // Create
        //--------------------------------------------------

        Application() : Rev::Application() {}

        // Destroy
        //--------------------------------------------------

        ~Application() {}
    };
}
