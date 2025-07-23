#include <GLFW/glfw3.h>

import Rev.Application;
import Rev.Window;
import Rev.Box;

using namespace Rev;

int main() {

    Application* application = new Application();
    Window* window = new Window(application->windows, { 640, 480, "Window 1" });

    window->style = {
        .padding = { .left = Px(10), .right = Px(10), .top = Px(10), .bottom = Px(10) },
    };
    
    for (int i = 0; i < 10000; i++) {
        Box* box = new Box(window);
        box->style = {
            .size = { .width = Grow(), .height = Grow(), .minWidth = Px(10), .minHeight = Px(10) },
            .margin = { .left = Px(2), .right = Px(2), .top = Px(2), .bottom = Px(2) },
            .background { .color = Color(1, 1, 0, 1) },
        };
    }

    application->run();

    return 0;
}