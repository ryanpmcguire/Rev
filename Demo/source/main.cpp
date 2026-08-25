#include <cstdlib>
#include <exception>
#include <string>

#if defined(__linux__)
#include <csignal>
#endif

import Rev.Application;
import Rev.Window;
import Rev.Serial;
import Rev.SocketClient;
import Rev.OS.Dialog;
import Rev.Element.Event;

import LithoControl.Interface;

using namespace Rev;
using namespace LithoControl;

int main() {

#if defined(__linux__)
    // The projector-window process (LithoRevProjector, launched via
    // posix_spawn() from Interface::openHdmiWindow()) is never explicitly
    // waited on -- its lifecycle is entirely IPC-socket-driven -- so reap it
    // automatically instead of leaving a zombie behind when it exits.
    std::signal(SIGCHLD, SIG_IGN);
#endif

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
        std::_Exit(0);   // force exit: detached threads (HDMI window) keep the process alive otherwise
    } catch (const std::exception& ex) {
        Rev::OS::Dialog::Error("LithoControl - Unhandled Exception", ex.what());
        return 1;
    } catch (...) {
        Rev::OS::Dialog::Error("LithoControl - Unhandled Exception",
            "An unknown exception was thrown during startup.");
        return 1;
    }

    return 0;
}
