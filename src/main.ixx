#include <GLFW/glfw3.h>

import Rev.Application;
import Rev.Window;
import Rev.Box;

using namespace Rev;

int main() {

    Application* application = new Application();
    Window* window = new Window(application->windows, { 640, 480, "Window 1" });
    
    //for (int i = 0; i < 10000; i++) {
        Box* box = new Box(window);
        box->style = {
            .size = { .width = Px(500), .height = Px(500) }
        };
    //}

    application->run();

    return 0;
}