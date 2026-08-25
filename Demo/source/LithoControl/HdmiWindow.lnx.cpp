module;

#include <spawn.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern char **environ;

module LithoControl.Interface;   // implementation unit -- no 'export'

namespace LithoControl {

    // Opaque state behind Interface::hdmiHwnd (void*) on Linux. The projector
    // window is a completely separate executable (LithoRevProjector, raylib-
    // based -- see ProjectorHelper/), launched via posix_spawn(). Two earlier
    // approaches were tried and ruled out on hardware: an in-process
    // background thread with its own X11 connection (froze even without any
    // custom display mode involved), and fork()+exec() of this same binary
    // (fork() is unsafe to call from a process with an active OpenGL context
    // -- LithoRev's main window has one -- and corrupted the PARENT's own
    // rendering even though the forked child was always fine). posix_spawn()
    // to a genuinely separate executable avoids both: LithoRev's process
    // never calls fork() at all.
    struct ProjectorProcState {
        pid_t pid = -1;
        int   sock = -1;
        std::thread senderThread;
        std::thread readerThread;
        std::atomic<bool> dirty   { true };   // paint once immediately after open
        std::atomic<bool> running { true };
    };

    void Interface::openHdmiWindow() {
        if (hdmiHwnd) { logQ.push("[DISP] Window already open"); return; }
        if (!hdmiDisplayDrop || hdmiDisplayDrop->params.value.empty()) {
            logQ.push("[DISP] Select a display first"); return;
        }
        std::string dev = hdmiDisplayDrop->params.value;
        int mx = 0, my = 0, mw = 640, mh = 360;
        for (auto& d : hdmiDisplays)
            if (d.devName == dev) { mx = d.x; my = d.y; mw = d.w; mh = d.h; break; }

        // LithoRevProjector is built as a sibling of this executable (same
        // Demo/ output directory) -- resolve it relative to our own path
        // rather than assuming a working directory or PATH entry.
        char exePath[4096];
        ssize_t pathLen = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (pathLen <= 0) {
            logQ.push("[DISP] Could not resolve own executable path");
            return;
        }
        exePath[pathLen] = '\0';
        std::string helperPath(exePath);
        size_t slash = helperPath.find_last_of('/');
        helperPath = (slash == std::string::npos ? "" : helperPath.substr(0, slash + 1)) + "LithoRevProjector";

        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
            logQ.push("[DISP] socketpair failed"); return;
        }

        std::string fdArg = "3";
        std::string xArg = std::to_string(mx), yArg = std::to_string(my);
        std::string wArg = std::to_string(mw), hArg = std::to_string(mh);
        char* args[] = {
            const_cast<char*>(helperPath.c_str()),
            const_cast<char*>(fdArg.c_str()), const_cast<char*>(xArg.c_str()),
            const_cast<char*>(yArg.c_str()), const_cast<char*>(wArg.c_str()),
            const_cast<char*>(hArg.c_str()), nullptr
        };

        // posix_spawn() rather than fork()+exec(): unlike fork(), it doesn't
        // duplicate this process's address space (and with it, whatever
        // OpenGL/driver-internal state the main window's GL context holds),
        // which is what made the previous fork()-based attempt corrupt the
        // PARENT's own rendering. file_actions here does the same job
        // fork()'s child-side dup2()/close() used to: give the new process
        // our end of the socketpair as fd 3, without ever touching this
        // process's own memory image to do it.
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, sv[1], 3);
        posix_spawn_file_actions_addclose(&actions, sv[0]);
        if (sv[1] != 3) posix_spawn_file_actions_addclose(&actions, sv[1]);

        pid_t pid = -1;
        int rc = posix_spawn(&pid, helperPath.c_str(), &actions, nullptr, args, environ);
        posix_spawn_file_actions_destroy(&actions);

        if (rc != 0) {
            logQ.push("[DISP] Failed to launch LithoRevProjector (" + helperPath + ")");
            close(sv[0]); close(sv[1]);
            return;
        }

        close(sv[1]);   // our copy of the child's end -- the child has its own via dup2

        auto* state  = new ProjectorProcState();
        state->pid   = pid;
        state->sock  = sv[0];

        hdmiHwnd = state;
        hdmiWinRunning = true;
        logQ.push("[DISP] Projector window open (ESC or CLOSE to close)");

        // Sends the latest composed frame whenever requestHdmiRepaint() marks
        // it dirty -- deliberately polling+coalescing rather than sending on
        // every single dirty signal, so a burst of repaint requests (e.g.
        // the RGB test animation) collapses to one frame per tick instead of
        // queuing up.
        state->senderThread = std::thread([this, state]() {
            std::vector<uint32_t> px(640u * 360u);
            const size_t total = px.size() * sizeof(uint32_t);
            while (state->running.load()) {
                if (state->dirty.exchange(false)) {
                    composeHdmiFrame(px.data());
                    const char* buf = reinterpret_cast<const char*>(px.data());
                    size_t sent = 0;
                    bool ok = true;
                    while (sent < total) {
                        ssize_t n = send(state->sock, buf + sent, total - sent, MSG_NOSIGNAL);
                        if (n <= 0) { ok = false; break; }
                        sent += (size_t)n;
                    }
                    if (!ok) { state->running.store(false); break; }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
        });

        // LithoRevProjector never sends anything back -- this just blocks
        // until the socket closes, which happens when it exits for any
        // reason (ESC pressed, killed, crashed) or when closeHdmiWindow()
        // shuts this side down. Either way, that's the one signal that means
        // "the projector window is gone now," and all cleanup happens here.
        state->readerThread = std::thread([this, state]() {
            char scratch[64];
            while (recv(state->sock, scratch, sizeof(scratch), 0) > 0) {}

            state->running.store(false);
            if (state->senderThread.joinable()) state->senderThread.join();
            close(state->sock);

            hdmiHwnd = nullptr;
            hdmiWinRunning = false;
            logQ.push("[DISP] Projector window closed");
            delete state;
        });
        state->readerThread.detach();
    }

    void Interface::closeHdmiWindow() {
        if (!hdmiHwnd) return;
        auto* state = reinterpret_cast<ProjectorProcState*>(hdmiHwnd);
        // Unblocks the reader thread's recv() safely from this thread --
        // unlike close(), which races with a concurrent blocking recv() on
        // the same fd, shutdown() is the documented-safe way to do this.
        // The reader thread does all the actual teardown once it wakes up.
        shutdown(state->sock, SHUT_RDWR);
    }

    void Interface::requestHdmiRepaint() {
        if (!hdmiHwnd) return;
        auto* state = reinterpret_cast<ProjectorProcState*>(hdmiHwnd);
        state->dirty.store(true);
    }
}
