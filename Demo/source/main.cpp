#include <stdexcept>
#include <dbg.hpp>


import Rev.Application;
import Rev.Window;
import Rev.Socket;
import Rev.Serial;
import Rev.Element.Event;

import Interface;

using namespace Rev;
using namespace HelloWorld;

int main() {

    Application* application = new Application();

    Window* window = new Window(application->windows, { "Hello World", { 800, 600 } });
    Interface* interface = new Interface(window);

    // Frameless test window: no OS title bar, but native resize via a 1px frame.
    Window* window2 = new Window(application->windows, {
        .name = "Another Window",
        .size = { 800, 600 },
        .nativeFrameless = true
    });
    Interface* interface2 = new Interface(window2);

    application->run();

    return 0;
}