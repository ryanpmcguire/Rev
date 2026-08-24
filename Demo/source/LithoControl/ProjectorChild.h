#pragma once

namespace LithoControl {
    // Entry point for the standalone Linux "projector-only" process. Spawned
    // by Interface::openHdmiWindow() (HdmiWindow.lnx.cpp) via fork+exec of
    // this same binary, with argv = { exePath, "--projector-child",
    // "<ipc-fd>", "<x>", "<y>", "<w>", "<h>" }. Runs its own X11 connection
    // and event loop in a process with no Rev/GL state at all -- the point
    // of the split is that this process can never contend with or destabilize
    // the main window's rendering the way a second X11 connection living
    // inside the main process could.
    int runProjectorChild(int argc, char** argv);
}
