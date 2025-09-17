#include <iostream>
#include <stdexcept>

import Rev.Application;
import Rev.Window;
import Rev.Box;

import Interface;

using namespace Rev;
using namespace HelloWorld;

int main() {

    try {

        Application* application = new Application();
        Window* window = new Window(application->windows, { "Window 1", 640, 480 });

        Interface* interface = new Interface(window);

        application->run();

        return 0;
    }

    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}