#include <GLFW/glfw3.h>

import Application;
import Window;
import Box;

using namespace Rev;

int main() {

    Application* application = new Application();
    Window* window = new Window(application->windows, { 640, 480, "Window 1" });
    
    Box* box = new Box(window);
    box->style = {
        .size = { .width = Px(100), .height = Px(100) }
    };

    application->run();

    return 0;
}