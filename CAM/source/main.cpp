#include <stdexcept>
#include <dbg.hpp>

import Rev.Application;
import Rev.Window;
import Rev.Socket;
import Rev.Serial;
import Rev.Client;
import Rev.Element.Event;

import Cam.App;
import Cam.Gui;
import Cam.Window;

import Carvera.Window;
import Carvera.Gui.Interface;

using namespace Rev;

int main() {

    Application* application = new Application();

    // -- CAM window --------------------------------------------------
    Cam::App::AppState* appState = new Cam::App::AppState();

    Cam::AppWindow* camWindow = new Cam::AppWindow(application->windows, {
        .name = "CAM",
        .size = { 1280, 720, .min = { 640, 480 }, .max = { 3840, 2160 } }
    });
    camWindow->shared->state = static_cast<void*>(appState);

    Cam::Gui::Interface* camInterface = new Cam::Gui::Interface(camWindow);

    // -- Carvera control window --------------------------------------
    Carvera::AppWindow* carveraWindow = new Carvera::AppWindow(application->windows, {
        .name = "Carvera Air",
        .size = { 380, 700, .min = { 300, 400 }, .max = { 800, 1200 } }
    });

    Carvera::Gui::Interface* carveraInterface = new Carvera::Gui::Interface(carveraWindow, appState);

    // ----------------------------------------------------------------

    application->run();

    return 0;
}