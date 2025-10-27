#include <stdexcept>

import Rev.Application;
import Rev.Element.Window;

import Interface;

using namespace Rev;
using namespace HelloWorld;

int main() {

    //try {

        Application* application = new Application();
            Window* window = new Window(application->windows);
                Interface* interface = new Interface(window);

        application->run();

        return 0;
    //}

    /*catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }*/
}