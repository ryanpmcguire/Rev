#include <windows.h>
#include <exception>
#include <string>

import Rev.Application;
import Rev.Window;
import Rev.Serial;
import Rev.SocketClient;
import Rev.Element.Event;

import LithoControl.Interface;

using namespace Rev;
using namespace LithoControl;

int main() {

    try {
        Application* application = new Application();

        Window* window = new Window(application->windows, Window::Details{
            .name   = "LithoRev",
            .width  = 1280,
            .height = 720
        });

        Interface* iface = new Interface(window);
        (void)iface;

        application->run();
        ExitProcess(0);   // force exit: detached threads (HDMI window) keep the process alive otherwise
    } catch (const std::exception& ex) {
        MessageBoxA(nullptr, ex.what(), "LithoControl – Unhandled Exception", MB_OK | MB_ICONERROR);
        return 1;
    } catch (...) {
        MessageBoxA(nullptr,
            "An unknown exception was thrown during startup.",
            "LithoControl – Unhandled Exception", MB_OK | MB_ICONERROR);
        return 1;
    }

    return 0;
}
