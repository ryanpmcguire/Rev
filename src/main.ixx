#include <GLFW/glfw3.h>

import Application;
import Window;

using namespace Rev;

int main() {

    Application* application = new Application();
    Window* window = new Window(application->windows, { 640, 480, "Window 1" });
    Window* window2 = new Window(application->windows, { 640, 480, "Window 2" });

    application->run();

    return 0;
}