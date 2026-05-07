#include <stdexcept>

import Rev.Application;
import Rev.Window;
import Rev.Socket;

import Interface;

using namespace Rev;
using namespace HelloWorld;

int main() {

    Application* application = new Application();

    Window* window = new Window(application->windows);
    Interface* interface = new Interface(window);

    // Create socket
    Socket* socket = new Socket(8080, [&](Socket::NetEvent& e) {

        switch (e.type) {

            case Socket::NetEvent::Connect: {
                //dbg("Client connected");
                break;
            }

            case Socket::NetEvent::Disconnect: {
                //dbg("Client disconnected");
                break;
            }

            case Socket::NetEvent::Data: {
                
                // Example: interpret data
                std::string msg(e.data.begin(), e.data.end());

                // Update UI state here
                // interface->something = ...

                // Trigger redraw
                window->refresh(window->event);

                break;
            }
        }
    });

    application->run();

    return 0;
}