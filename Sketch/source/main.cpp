#include <stdexcept>
#include <dbg.hpp>

import Rev.Application;
import Rev.Window;
import Rev.Socket;
import Rev.Serial;
import Rev.Element.Event;

import Sketch.App;
import Sketch.Gui;
import Sketch.Window;

using namespace Rev;

int main() {

    Application* application = new Application();

    Sketch::App::AppState* appState = new Sketch::App::AppState();

    Sketch::AppWindow* window = new Sketch::AppWindow(application->windows, {
        .name = "Sketch",
        .size = { 1280, 720, .min = { 640, 480 }, .max = { 3840, 2160 } }
    });
    window->shared->state = static_cast<void*>(appState);

    Sketch::Gui::Interface* interface = new Sketch::Gui::Interface(window);

    application->run();

    return 0;
}
