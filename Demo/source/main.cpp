#include <cstdlib>
#include <exception>
#include <string>

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

        // application->run() returns as soon as the MAIN window closes --
        // including via the OS "X" button, not just the in-app CLOSE
        // button on the Connections page (which calls this same method).
        // Without this, closing via the X left the separate
        // LithoRevProjector helper process (posix_spawn'd, its own
        // executable) orphaned and still running: std::_Exit() below skips
        // destructors entirely, so nothing would otherwise ever signal it
        // to exit. closeHdmiWindow() shuts down our end of their shared
        // socket; the helper's own loop treats that as end-of-connection
        // and exits itself (see ProjectorHelper/src/main.cpp) -- reliable
        // even though our own process is about to disappear, since it's a
        // synchronous kernel-level half-close, not something that depends
        // on us staying alive to finish anything async afterward.
        iface->closeHdmiWindow();

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
