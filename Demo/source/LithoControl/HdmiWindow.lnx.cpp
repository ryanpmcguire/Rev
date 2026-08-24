module;

#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

module LithoControl.Interface;   // implementation unit -- no 'export'

namespace LithoControl {

    // Opaque state behind Interface::hdmiHwnd (void*) on Linux. The projector
    // window itself lives in a completely separate OS process (see
    // ProjectorChild.lnx.cpp), spawned via fork+exec of this same binary --
    // this struct just tracks that child and the pipe of frames going to it.
    // A second X11 connection living inside this process (the previous
    // approach) was the leading suspect behind a projector-window freeze bug
    // that was never fully root-caused; a separate process can't destabilize
    // this one's own rendering no matter what's wrong with it.
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

        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
            logQ.push("[DISP] socketpair failed"); return;
        }

        char exePath[4096];
        ssize_t pathLen = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (pathLen <= 0) {
            logQ.push("[DISP] Could not resolve own executable path");
            close(sv[0]); close(sv[1]);
            return;
        }
        exePath[pathLen] = '\0';

        pid_t pid = fork();
        if (pid < 0) {
            logQ.push("[DISP] fork failed");
            close(sv[0]); close(sv[1]);
            return;
        }

        if (pid == 0) {
            // Child: re-exec into a clean process image (no inherited Rev/GL
            // state) running as the standalone projector process instead.
            close(sv[0]);
            dup2(sv[1], 3);
            if (sv[1] != 3) close(sv[1]);

            std::string fdArg = "3";
            std::string xArg = std::to_string(mx), yArg = std::to_string(my);
            std::string wArg = std::to_string(mw), hArg = std::to_string(mh);
            char* args[] = {
                exePath, const_cast<char*>("--projector-child"),
                const_cast<char*>(fdArg.c_str()), const_cast<char*>(xArg.c_str()),
                const_cast<char*>(yArg.c_str()), const_cast<char*>(wArg.c_str()),
                const_cast<char*>(hArg.c_str()), nullptr
            };
            execv(exePath, args);
            _exit(127);   // exec only returns on failure
        }

        close(sv[1]);

        auto* state  = new ProjectorProcState();
        state->pid   = pid;
        state->sock  = sv[0];

        hdmiHwnd = state;
        hdmiWinRunning = true;
        logQ.push("[DISP] Projector window open (ESC or CLOSE to close)");

        // Sends the latest composed frame whenever requestHdmiRepaint() marks
        // it dirty -- deliberately polling+coalescing rather than sending on
        // every single dirty signal, same cadence the old in-process paint
        // loop used, so a burst of repaint requests (e.g. the RGB test
        // animation) collapses to one frame per tick instead of queuing up.
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

        // The child never sends anything back -- this just blocks until the
        // socket closes, which happens when the child process exits for any
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
