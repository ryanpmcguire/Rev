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
    struct Window : public Rev::Window {

        Interface* content = nullptr;

        // Create
        //--------------------------------------------------

        Window(
            std::vector<void*>& group,
            Rev::Window::Details details = {}
        ) : Rev::Window(group, details) {

            content = new Interface(this);
        }

        // Destroy
        //--------------------------------------------------

        ~Window() {}
    };
}
