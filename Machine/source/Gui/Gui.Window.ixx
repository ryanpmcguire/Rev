module;

#include <vector>

export module Machine.Gui;

import Rev.Window;

import Machine.Gui.Interface;

export namespace Machine::Gui {

    using namespace Rev;

    // The GUI window: a subclassed Rev::Window that hosts the content root
    // (Interface). It is added as a child of the application (application->windows)
    // in main. The Interface paints the dark surface and parents everything else.
    //
    // NB: named AppWindow, NOT Window. Naming it Window while `using namespace
    // Rev;` is in scope puts two distinct class types (this one and Rev::Window)
    // under the same unqualified name across the program -- an ODR/type-identity
    // hazard that corrupted teardown and aborted on close.
    struct AppWindow : public Rev::Window {

        Interface* content = nullptr;

        // Create
        //--------------------------------------------------

        AppWindow(
            std::vector<void*>& group,
            Rev::Window::Details details = {}
        ) : Rev::Window(group, details) {

            content = new Interface(this);
        }

        // Destroy
        //--------------------------------------------------

        ~AppWindow() {}
    };
}
