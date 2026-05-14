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

    Window* window = new Window(application->windows);
    Interface* interface = new Interface(window);

    Window* window2 = new Window(application->windows);
    //Interface* interface2 = new Interface(window2);

    application->run();

    return 0;
}