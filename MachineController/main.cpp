#include <dbg.hpp>
#include <managed.hpp>

import Rev.Application;
import Rev.Window;
import Rev.Core.Resource;

import App.Application;
import App.Machine;
import App.Machines.CarveraAir;
import Gui;

using namespace Rev;

int main() {

    App::Application* application = new App::Application();

    Gui::AppWindow* gui = new Gui::AppWindow(application->windows, *application->machine, {
        .name = "MachineController",
        .icon = File("./source/Gui/C3DT.svg"),
        .size = { 1280, 720, .min = { 640, 480 }, .max = { 3840, 2160 } }
    });

    // Begin the main loop of the application
    application->run();

    // Delete to cascade descructors
    delete application;

    return 0;
}
