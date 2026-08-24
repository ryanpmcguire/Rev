#include <cstdlib>
#include <exception>
#include <string>

#if defined(__linux__)
#include <csignal>
#include "LithoControl/ProjectorChild.h"
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

int main(int argc, char** argv) {

#if defined(__linux__)
    // Re-exec'd by Interface::openHdmiWindow() as a standalone projector
    // process (see HdmiWindow.lnx.cpp / ProjectorChild.lnx.cpp) -- runs its
    // own X11 connection and event loop, fully separate from this process's
    // own Rev/GL state, then returns here instead of falling through to the
    // normal GUI startup below.
    if (argc > 1 && std::string(argv[1]) == "--projector-child") {
        return runProjectorChild(argc, argv);
    }
    // Projector child processes are never explicitly waited on -- their
    // lifecycle is entirely IPC-socket-driven (HdmiWindow.lnx.cpp) -- so
    // reap them automatically instead of leaving zombies behind.
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
