module;

#include <vector>

export module Gui;

import Rev.Window;

import Machine.Base;
import Gui.Interface;

export namespace Gui {

    // The window: a subclassed Rev::Window that hosts the content root (Interface).
    // Named AppWindow, NOT Window, to never share an unqualified name with
    // Rev::Window (an ODR/type-identity hazard that aborted teardown on close).
    struct AppWindow : public Rev::Window {

        Interface* content = nullptr;

        // Create
        //--------------------------------------------------

        AppWindow(
            std::vector<void*>& group,
            Machine::MachineBase& machine,
            Rev::Window::Details details = {}
        ) : Rev::Window(group, details) {

            content = new Interface(this, machine);
        }

        // Destroy
        //--------------------------------------------------

        ~AppWindow() {}
    };
}
