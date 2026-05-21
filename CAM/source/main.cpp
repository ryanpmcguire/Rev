#include <stdexcept>
#include <dbg.hpp>

import Rev.Application;
import Rev.Window;
import Rev.Socket;
import Rev.Serial;
import Rev.Element.Event;

import Cam.App;
import Cam.Gui;
import Cam.Window;

using namespace Rev;

int main() {

    Application* application = new Application();

    Cam::App::AppState* appState = new Cam::App::AppState();

    Cam::AppWindow* window = new Cam::AppWindow(application->windows, {});
    window->shared->state = static_cast<void*>(appState);

    Cam::Gui::Interface* interface = new Cam::Gui::Interface(window);

    application->run();

    return 0;
}