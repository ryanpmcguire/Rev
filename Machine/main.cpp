#include <dbg.hpp>
#include <managed.hpp>

import Rev.Application;
import Rev.Window;
import Rev.Core.Resource;

import Machine.Application;
import Machine.Gui;

using namespace Rev;

int main() {

    Machine::Application* application = new Machine::Application();

    Machine::Gui::AppWindow* gui = new Machine::Gui::AppWindow(application->windows, {
        .name = "Machine",
        .icon = File("./source/Gui/C3DT.svg"),
        .size = { 1280, 720, .min = { 640, 480 }, .max = { 3840, 2160 } }
    });

    application->run();

    return 0;
}
