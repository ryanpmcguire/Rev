#include <stdexcept>
#include <dbg.hpp>


import Rev.Application;
import Rev.Window;
import Rev.Socket;
import Rev.Serial;
import Rev.Element.Event;

import Interface;

using namespace Rev;
using namespace LayoutDiagnose;

int main() {

    Application* application = new Application();

    Window* window = new Window(application->windows, { "Layout Diagnose", { 1000, 700 } });
    Interface* interface = new Interface(window);

    application->run();

    return 0;
}
