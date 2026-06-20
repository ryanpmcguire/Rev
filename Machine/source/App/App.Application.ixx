module;

export module Machine.Application;

import Rev.Application;

import Machine.App.Machine;

export namespace Machine {

    // The application: a thin Rev::Application that owns the Machine system.
    // The GUI is added as a child window (application->windows) in main; this
    // type holds the application-wide state the GUI reflects.
    struct Application : public Rev::Application {

        App::Machine machine;

        // Create
        //--------------------------------------------------

        Application() : Rev::Application() {}

        // Destroy
        //--------------------------------------------------

        ~Application() {}
    };
}
