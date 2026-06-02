module;

#include <atomic>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

export module Carvera.MachineLink;

import Rev.Client;

export namespace Carvera {

    // Process-wide bridge between the Carvera control window (which owns the
    // physical connection) and the CAM window (which decides what to execute).
    //
    // The two run as separate windows with separate state, so a single shared
    // instance is the cleanest way for the CAM preview to know whether the
    // machine is armed and to push moves to it.
    //
    // ARMING is the safety gate: nothing is ever sent for execution unless armed
    // AND connected.  Losing the connection disarms and flushes the queue.
    //
    // Streaming uses send-on-ok flow control with a small in-flight budget so we
    // never overrun the controller's receive buffer (the classic Grbl streaming
    // hazard).  All sends happen on the main thread via pump(); the worker
    // thread only ever increments an atomic ack counter.
    struct MachineLink {

        static MachineLink& instance() {
            static MachineLink link;
            return link;
        }

        // -- Connection / arming --------------------------------------

        void setClient(Rev::Client* c) {
            client = c;
            if (!c) {
                armed.store(false);
                spindleArmed.store(false);
                clearQueue();
            }
        }

        bool connected() const {
            return client && client->isConnected.load();
        }

        bool isArmed() const {
            return armed.load() && connected();
        }

        bool setArmed(bool value) {
            if (value && !connected()) { return false; }
            armed.store(value);
            if (!armed.load()) { clearQueue(); }
            return armed.load();
        }

        void disarm() {
            armed.store(false);
            clearQueue();
        }

        // Spindle arming is an independent gate: motion can be armed (to dry-run
        // a toolpath on the real machine) with the spindle left off.  The
        // program only emits M3 when the spindle is armed.
        bool isSpindleArmed() const { return spindleArmed.load(); }
        void setSpindleArmed(bool value) { spindleArmed.store(value); }

        // -- Direct (unqueued) send — for one-off control lines -------

        void send(const std::string& line) {
            if (connected()) { client->send(line); }
        }

        void beep() {
            std::putchar('\a');
            std::fflush(stdout);
            if (connected()) { client->send("M300 S660 P200\n"); }  // best-effort buzzer tone
        }

        // -- Streaming (flow-controlled) ------------------------------

        // Queue a whole program for execution.  Requires arming.
        bool enqueueProgram(const std::vector<std::string>& lines) {

            if (!isArmed()) { return false; }

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                queue.assign(lines.begin(), lines.end());
                inFlight = 0;
                pendingOk.store(0);
            }

            executing.store(!lines.empty());
            pump();
            return true;
        }

        // Worker-thread hook: called for each "ok" the controller returns.
        void notifyOk() {
            pendingOk.fetch_add(1);
        }

        // Main-thread pump: retire acked lines and send more, keeping the
        // in-flight count under the budget.  Safe to call every frame.
        void pump() {

            std::lock_guard<std::mutex> lock(queueMutex);

            inFlight -= pendingOk.exchange(0);
            if (inFlight < 0) { inFlight = 0; }

            if (!connected()) { return; }

            while (inFlight < MaxInFlight && !queue.empty()) {
                client->send(queue.front());
                queue.pop_front();
                inFlight++;
            }

            if (queue.empty() && inFlight == 0) {
                executing.store(false);
            }
        }

        void clearQueue() {
            std::lock_guard<std::mutex> lock(queueMutex);
            queue.clear();
            inFlight = 0;
            pendingOk.store(0);
            executing.store(false);
        }

        bool isExecuting() const {
            return executing.load();
        }

        // -- Telemetry mirror (machine MPos, pushed by the control panel) -----

        // App-wide position broadcast: any part of the app can subscribe to
        // live machine position (the CAM view uses it to track the real tool).
        std::function<void(float, float, float, float)> onTelemetry;

        void setTelemetry(float x, float y, float z, float a) {
            telemX.store(x); telemY.store(y); telemZ.store(z); telemA.store(a);
            telemValid.store(true);
            if (onTelemetry) { onTelemetry(x, y, z, a); }
        }

        bool telemetry(float& x, float& y, float& z, float& a) const {
            if (!telemValid.load()) { return false; }
            x = telemX.load(); y = telemY.load(); z = telemZ.load(); a = telemA.load();
            return true;
        }

        // -- Work origin reference --------------------------------------------
        //
        // captureMachineOrigin records the machine MPos at the "set origin"
        // instant; setCadOrigin records the matching CAD point (the stock-top
        // centre the CAM side streams relative to).  With both, telemetry MPos
        // can be mapped back into CAD space for the live view.

        void captureMachineOrigin(float mx, float my, float mz) {
            originMx.store(mx); originMy.store(my); originMz.store(mz);
            originValid.store(true);
        }

        void setCadOrigin(float cx, float cy, float cz) {
            cadOriginX.store(cx); cadOriginY.store(cy); cadOriginZ.store(cz);
            cadOriginValid.store(true);
        }

        bool workOrigin(
            float& mx, float& my, float& mz,
            float& cx, float& cy, float& cz
        ) const {
            if (!originValid.load() || !cadOriginValid.load()) { return false; }
            mx = originMx.load(); my = originMy.load(); mz = originMz.load();
            cx = cadOriginX.load(); cy = cadOriginY.load(); cz = cadOriginZ.load();
            return true;
        }

        Rev::Client* client = nullptr;

    private:

        // Conservative in-flight budget: a couple of short G-code lines stay
        // well under typical controller receive buffers.
        static constexpr int MaxInFlight = 2;

        std::atomic<bool> armed { false };
        std::atomic<bool> spindleArmed { false };
        std::atomic<bool> executing { false };

        std::mutex queueMutex;
        std::deque<std::string> queue;
        int inFlight = 0;
        std::atomic<int> pendingOk { 0 };

        std::atomic<float> telemX { 0 }, telemY { 0 }, telemZ { 0 }, telemA { 0 };
        std::atomic<bool>  telemValid { false };

        std::atomic<float> originMx { 0 }, originMy { 0 }, originMz { 0 };
        std::atomic<bool>  originValid { false };

        std::atomic<float> cadOriginX { 0 }, cadOriginY { 0 }, cadOriginZ { 0 };
        std::atomic<bool>  cadOriginValid { false };
    };
}
