#include <dbg.hpp>

import Rev.Application;
import Rev.Window;
import Rev.Serial;
import Rev.SocketClient;
import Rev.Element.Event;

import LithoControl.Interface;

using namespace Rev;
using namespace LithoControl;

int main() {

    Application* application = new Application();

    Window* window = new Window(application->windows, Window::Details{
        .name   = "LithoControl  v2.0",
        .width  = 1500,
        .height = 900
    });

    Interface* iface = new Interface(window);
    (void)iface;

    application->run();

    return 0;
}
