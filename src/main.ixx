#include <GLFW/glfw3.h>

import Rev.Application;
import Rev.Window;
import Rev.Box;

import Interface;

using namespace Rev;
using namespace HelloWorld;

int main() {

    Application* application = new Application();
    Window* window = new Window(application->windows, { "Window 1", 640, 480 });

    Interface* interface = new Interface(window);

    application->run();

    return 0;
}