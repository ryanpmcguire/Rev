#include <GLFW/glfw3.h>

import Rev;
import Application;
import Window;
import Box;

using namespace Rev;

int main() {

    Application* application = new Application();
    Window* window = new Window(application->windows, { 640, 480, "Window 1" });
    
    Box* box = new Box(window);

    application->run();

    return 0;
}