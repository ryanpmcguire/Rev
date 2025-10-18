#include <iostream>
#include <stdexcept>

import Rev.Application;
import Rev.Element.Window;

import MacInterface;

using namespace Rev;
using namespace HelloWorld;

int main() {

    try {

        Application* application = new Application();
        Window* window = new Window(application->windows);

        MacInterface* interface = new MacInterface(window);

        application->run();

        return 0;
    }

    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}