module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <format>

#include <dbg.hpp>

export module CarveraAir;

import Rev.Client;
import Rev.Core.Animator;
import Rev.Core.Dispatcher;

export namespace Carvera {

    // ==================================================================
    // Carvera::Air
    //
    // The machine's sole representative inside the application.  It OWNS
    // every machine concern and is the ONLY thing that ever opens a
    // connection, emits G-code, or reads telemetry.  Nothing else in the
    // program talks to the controller directly.
    //
    // The GUI never commands the machine; it *asks* Air (connect(), jog(),
    // goTo(), changeTool(), arm(), stop(), ...) and *reflects* Air by
    // listening to its dispatched events (telemetry, state, connection,
    // log, tool-change phase, arming).  In that sense Air behaves as though
    // it simply *is* the Carvera Air.
    //
    // Threading
    // ---------
    //   - The socket Client delivers its callbacks on a worker thread.
    //     Those callbacks only ever touch mutex-guarded "pending" buffers
    //     (and start/stop the animators, exactly as the old control panel
    //     did).
    //   - All authoritative state, all G-code emission past the initial
    //     status kick, and ALL dispatched events happen on the MAIN thread
    //     inside the animator tick (drainTelemetry / tickDisplay).  So every
    //     listener is invoked on the main thread and may safely refresh GUI.
    //
    // Position model (unchanged from the original control panel)
    // ----------------------------------------------------------
    //   - Telemetry ("?") is chain-polled: a new request is only sent once
    //     the previous reply has been parsed, so we never read stale frames.
    //   - An "intent" accumulates every commanded delta; the displayed
    //     position chases the intent at a fixed rate for instant feedback,
    //     and re-anchors to confirmed telemetry every frame so it always
    //     converges to ground truth.
    // ==================================================================

    struct Air {

        // -- Public enums --------------------------------------------

        // Tool-change lifecycle.  A change walks None → Seeking → Standby →
        // (Confirming) → None:
        //
        //   None        no change in progress.
        //   Seeking     M6 issued; the machine is travelling to the change
        //               position (it has not yet reported "Tool" state).
        //   Standby     the machine is at the change position and waiting —
        //               for the ATC carousel, or for the operator to fit the
        //               tool and confirm.  This is the blue-light state.
        //   Confirming  the operator confirmed; the machine is finishing the
        //               cycle (tool touch-off / height measurement).
        //
        // Each transition emits a discrete event (begin / standby / confirm /
        // complete) so the GUI can react precisely.
        enum class ToolChangePhase { None, Seeking, Standby, Confirming };

        enum class ConnectionStatus { Disconnected, Connecting, Connected, Error };

        // -- Event payloads ------------------------------------------

        struct TelemetryEvent  { float x = 0, y = 0, z = 0, a = 0; };
        struct StateEvent      { std::string state; };
        struct ConnectionEvent { ConnectionStatus status = ConnectionStatus::Disconnected; std::string message; };
        struct LogEvent        { std::string line; };
        struct ArmEvent        { bool armed = false; bool spindleArmed = false; };
        struct StartEvent      {};

        // One payload shared by all four tool-change lifecycle channels; the
        // `phase` field says which transition the machine just entered.
        struct ToolChangeEvent { int slot = 0; ToolChangePhase phase = ToolChangePhase::None; };

        // -- Singleton -----------------------------------------------

        static Air& instance() {
            static Air air;
            return air;
        }

        Air() {

            // Watchdog — re-issues "?" if a chain-polled reply never arrived.
            watchdog.onFrame([this](Rev::Core::AnimationEvent&) {
                if (!connected()) { return; }
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    Clock::now() - statusSentAt).count();
                if (statusInFlight && elapsed > StatusTimeoutMs) {
                    statusInFlight = false;
                    sendStatus();
                }
                if (!statusInFlight) { sendStatus(); }
            });

            // Main tick.  ALWAYS running: at an idle period when disconnected
            // (just drains log/connection events so the GUI reflects connect
            // failures and disconnects), and sped up to 140 Hz while connected
            // to advance the display chase, poll status, and pump the streamer.
            displayLoop.onFrame([this](Rev::Core::AnimationEvent&) {
                tickDisplay();
                pump();
            });

            displayLoop.setPeriod(IdlePeriodMs);
            displayLoop.play();
        }

        ~Air() {
            if (client) { delete client; client = nullptr; }
        }

        Air(const Air&)            = delete;
        Air& operator=(const Air&) = delete;

        // ============================================================
        // Event registration (dispatcher-based — many listeners allowed)
        // ============================================================

        void onTelemetryFrame(const std::function<void(TelemetryEvent&)>& f) { telemetryDispatcher.listen(&Air::telemetryEvent, f); }
        void onState         (const std::function<void(StateEvent&)>&      f) { stateDispatcher.listen(&Air::stateEvent, f); }
        void onConnection    (const std::function<void(ConnectionEvent&)>& f) { connectionDispatcher.listen(&Air::connectionEvent, f); }
        void onLog           (const std::function<void(LogEvent&)>&        f) { logDispatcher.listen(&Air::logEvent, f); }
        void onArm           (const std::function<void(ArmEvent&)>&        f) { armDispatcher.listen(&Air::armEvent, f); }

        // Tool-change lifecycle channels:
        //   Begin     — a change was just requested (M6 sent, machine seeking).
        //   Standby   — the machine reached the change position and is waiting
        //               (carousel, or operator confirmation — the blue light).
        //   Confirm   — the operator confirmed; the machine is finishing.
        //   Complete  — the change is fully done; back to normal operation.
        void onToolChangeBegin   (const std::function<void(ToolChangeEvent&)>& f) { tcBeginDispatcher.listen(&Air::toolChangeBeginEvent, f); }
        void onToolChangeStandby (const std::function<void(ToolChangeEvent&)>& f) { tcStandbyDispatcher.listen(&Air::toolChangeStandbyEvent, f); }
        void onToolChangeConfirm (const std::function<void(ToolChangeEvent&)>& f) { tcConfirmDispatcher.listen(&Air::toolChangeConfirmEvent, f); }
        void onToolChangeComplete(const std::function<void(ToolChangeEvent&)>& f) { tcCompleteDispatcher.listen(&Air::toolChangeCompleteEvent, f); }
        void onStart         (const std::function<void(StartEvent&)>&      f) { startDispatcher.listen(&Air::startEvent, f); }

        // Legacy single-callback hooks retained for the CAM world view, which
        // sets and clears them across its own lifetime.  Air fires BOTH these
        // and the dispatcher events.
        std::function<void(float, float, float, float)> onTelemetry;   // live position push
        std::function<void()>                           onStartRequested;

        // ============================================================
        // Connection
        // ============================================================

        std::string targetHost = "192.168.1.104";
        int         targetPort = 2222;

        void setTarget(const std::string& host, int port) {
            targetHost = host;
            targetPort = port;
        }

        void connect() { connect(targetHost, targetPort); }

        void connect(const std::string& host, int port) {

            if (client) { return; }

            targetHost = host;
            targetPort = port;

            client = new Rev::Client();

            client->onConnecting([this](Rev::Client::ConnectingEvent& e) {
                pushLog(std::format("Connecting to {}...", e.address));
                queueConnection(ConnectionStatus::Connecting, e.address);
            });

            client->onConnect([this](Rev::Client::ConnectEvent& e) {
                pushLog(std::format("Connected to {}", e.address));
                queueConnection(ConnectionStatus::Connected, e.address);
                resetTelemetryState();
                sendStatus();          // kick off the chain poll
                watchdog.play();
                displayLoop.setPeriod(LivePeriodMs);
            });

            client->onDisconnect([this](Rev::Client::DisconnectEvent&) {
                watchdog.stop();
                displayLoop.setPeriod(IdlePeriodMs);
                disarmInternal();
                clearQueue();
                pushLog("Connection closed by remote.");
                queueConnection(ConnectionStatus::Disconnected, "");
                std::lock_guard lock(stateMutex);
                pendingState        = "";
                pendingPosValid     = false;
                pendingResponseSeen = false;
            });

            client->onData([this](Rev::Client::DataEvent& e) { onData(e); });

            client->onError([this](Rev::Client::ErrorEvent& e) {
                watchdog.stop();
                displayLoop.setPeriod(IdlePeriodMs);
                disarmInternal();
                clearQueue();
                pushLog(std::format("Error: {}", e.reason));
                queueConnection(ConnectionStatus::Error, e.reason);
            });

            client->connect(targetHost, targetPort);
        }

        void disconnect() {
            if (!client) { return; }
            watchdog.stop();
            displayLoop.setPeriod(IdlePeriodMs);
            disarmInternal();
            clearQueue();
            delete client; client = nullptr;
            pushLog("Disconnected.");
            queueConnection(ConnectionStatus::Disconnected, "");
            resetTelemetryState();
        }

        bool connected() const {
            return client && client->isConnected.load();
        }

        // ============================================================
        // Queries (reflected by the GUI; never mutate the machine)
        // ============================================================

        std::string machineState() const { return machineState_; }

        // Smoothed live position (the value the CAM view tracks).
        bool livePosition(float& x, float& y, float& z, float& a) const {
            if (!telemValid.load()) { return false; }
            x = telemX.load(); y = telemY.load(); z = telemZ.load(); a = telemA.load();
            return true;
        }

        // Alias kept for existing call sites in the CAM world view.
        bool telemetry(float& x, float& y, float& z, float& a) const {
            return livePosition(x, y, z, a);
        }

        // Last confirmed (raw, un-smoothed) machine position.
        bool currentConfirmed(float& x, float& y, float& z, float& a) const {
            if (!confValid) { return false; }
            x = confX; y = confY; z = confZ; a = confA;
            return true;
        }

        bool isArmed()        const { return armed.load() && connected(); }
        bool isSpindleArmed() const { return spindleArmed.load(); }
        bool isExecuting()    const { return executing.load(); }

        ToolChangePhase toolChangePhase() const {
            switch (tcGate.load()) {
                case 1:  return ToolChangePhase::Seeking;
                case 2:  return ToolChangePhase::Standby;
                case 3:  return ToolChangePhase::Confirming;
                default: return ToolChangePhase::None;
            }
        }

        bool toolChangeGated() const { return tcGate.load() != 0; }

        // ============================================================
        // Motion / control  (the GUI asks; Air emits the G-code)
        // ============================================================

        void jog(float dx, float dy, float dz) {
            if (!connected() || !confValid) { return; }
            intentX += dx; intentY += dy; intentZ += dz;
            std::string cmd = "$J=G91";
            if (dx != 0.0f) cmd += std::format(" X{:.3f}", dx);
            if (dy != 0.0f) cmd += std::format(" Y{:.3f}", dy);
            if (dz != 0.0f) cmd += std::format(" Z{:.3f}", dz);
            cmd += std::format(" F{}\n", jogFeedRate);
            rawSend(cmd);
        }

        void jogA(float degrees) {
            if (!connected() || !confValid) { return; }
            intentA += degrees;
            rawSend(std::format("$J=G91 A{:.3f} F{}\n", degrees, jogFeedRateA));
        }

        // Issue a single $J jog as a RELATIVE delta on each axis.  Per the
        // GRBL jog spec, $J=G90 absolute coordinates are interpreted in the
        // active WCS (G54..G59), NOT machine coords — so after a Set Origin
        // there is no safe absolute target we can compute without first
        // pulling the WCS offset.  G91 relative dodges the whole issue: a
        // delta of +25 always moves the axis +25 from wherever the controller
        // currently is, regardless of WCS state.
        //
        // Axes with delta 0 are omitted so the controller doesn't interpret a
        // present-but-zero axis as "go to here on this axis", which can lock
        // up its planner.  Returns silently if nothing to send.
        void jogRel(
            float dx, float dy, float dz, float da,
            int   feedMmMin
        ) {
            if (!connected()) { return; }

            std::string cmd = "$J=G91";
            bool any = false;
            if (dx != 0.0f) { cmd += std::format(" X{:.3f}", dx); any = true; }
            if (dy != 0.0f) { cmd += std::format(" Y{:.3f}", dy); any = true; }
            if (dz != 0.0f) { cmd += std::format(" Z{:.3f}", dz); any = true; }
            if (da != 0.0f) { cmd += std::format(" A{:.3f}", da); any = true; }
            if (!any) { return; }

            cmd += std::format(" F{}\n", feedMmMin);
            rawSend(cmd);

            // Nudge intent so the chase display leads in the right direction.
            if (confValid) {
                intentX += dx; intentY += dy;
                intentZ += dz; intentA += da;
            }
        }

        // GRBL real-time jog cancel (0x85): decelerates the current jog and
        // flushes any queued jogs.  After it lands the controller is back at
        // Idle, and a subsequent $J will execute from wherever the deceleration
        // ended up.
        void jogCancel() {
            if (!connected()) { return; }
            const char b = static_cast<char>(0x85);
            client->send(std::string(1, b));
        }

        // Rapid to an absolute MACHINE position (G53 — independent of any work
        // offset).  Used by the GUI's origin "Goto".
        void goTo(float x, float y, float z, float a) {
            if (!connected() || !confValid) {
                pushLog("Connect and wait for position before moving.");
                return;
            }
            sendLine(std::format("G53 G0 X{:.3f} Y{:.3f} Z{:.3f} A{:.3f}\n", x, y, z, a));
            intentX = x; intentY = y; intentZ = z; intentA = a;
        }

        void home()   { if (requireConnected()) { sendLine("$H\n");    pushLog("Homing..."); } }
        void unlock() { if (requireConnected()) { sendLine("$X\n");    pushLog("Unlocked.");  } }
        void reset()  { if (requireConnected()) { sendLine("reset\n"); } }

        // Immediate stop: feed-hold + soft-reset in one write, then flush.
        void stop() {
            if (connected()) {
                client->send("!\x18");   // 0x21 feed hold, 0x18 soft reset
            }
            clearQueue();
            pushLog("Program stopped.");
        }

        // Zero the active work coordinate system at the current position and
        // capture the machine origin for the CAM view's coordinate mapping.
        void setWorkOrigin() {
            if (!connected() || !confValid) {
                pushLog("Connect and wait for position before setting origin.");
                return;
            }
            sendLine("G10 L20 P1 X0 Y0 Z0 A0\n");
            captureMachineOrigin(confX, confY, confZ, confA);
            pushLog(std::format(
                "Work origin set at current position (X{:.3f} Y{:.3f} Z{:.3f} A{:.3f}).",
                confX, confY, confZ, confA));
        }

        // ============================================================
        // Arming
        // ============================================================

        bool arm() {
            if (!connected()) { pushLog("Connect to the machine before arming."); return false; }
            armed.store(true);
            beep();
            pushLog("ARMED - execution enabled.");
            emitArm();
            return true;
        }

        void disarm() {
            disarmInternal();
            pushLog("DISARMED.");
            emitArm();
        }

        void toggleArm() { if (isArmed()) { disarm(); } else { arm(); } }

        void setSpindleArmed(bool value) {
            spindleArmed.store(value);
            pushLog(value ? "SPINDLE ARMED." : "Spindle disarmed.");
            emitArm();
        }

        void toggleSpindleArm() { setSpindleArmed(!isSpindleArmed()); }

        void beep() {
            std::putchar('\a');
            std::fflush(stdout);
            if (connected()) { client->send("M300 S660 P200\n"); }
        }

        // ============================================================
        // Tool change
        // ============================================================

        // Begin a tool change for the given slot.  Sends M6 and engages the
        // gate; the lifecycle events (begin / standby / confirm / complete) are
        // all emitted from updateToolChangeGate as the gate advances, so
        // streamed (automatic) M6 changes get the same events.
        //
        // autoConfirm: when true, Air presses "Ok" for the operator the instant
        // the machine reaches Standby — a one-click "load this tool and touch
        // off" used when nothing is loaded yet (the tool is already fitted).
        void changeTool(int slot, bool autoConfirm = false) {
            if (!connected()) { pushLog("Not connected - cannot change tool."); return; }
            sendLine(std::format("M6 T{}\n", slot));
            tcSlot = slot;
            tcAutoConfirm = autoConfirm;
            tcGate.store(1);            // Seeking → drainTelemetry emits Begin
            tcSentFrames.store(0);
            pushLog(std::format("Tool change requested: T{}{}.",
                slot, autoConfirm ? " (auto-confirm)" : ""));
        }

        // The slot of the tool currently loaded + touched-off (0 = none/unknown,
        // e.g. right after connecting).  Set when a change completes.
        int loadedToolSlot() const { return loadedSlot.load(); }

        // Confirm a tool change — the protocol equivalent of pressing the
        // physical button so the controller proceeds to touch-off.  Only
        // meaningful while the machine is in Standby (waiting on the operator).
        // The Carvera honours the GRBL realtime cycle-start byte (0x7E '~').
        // The Confirm event is emitted from updateToolChangeGate on the 2 → 3
        // transition.
        void confirmToolChange() {
            if (tcGate.load() != 2) { return; }     // not waiting on us
            if (connected()) { client->send("~"); }
            tcGate.store(3);            // Confirming (finishing / touch-off)
            pushLog("Tool change confirmed - proceeding to touch-off.");
        }

        // ============================================================
        // Program execution — the orchestration / "action queue" model
        // ============================================================
        //
        // Callers do NOT hand Air a flat list of raw G-code.  They hand it a
        // queue of high-level intents (moves, spindle changes, tool changes),
        // and Air trickle-sends the controller a few lines at a time under flow
        // control.  Crucially, Air OWNS the dangerous bits: when it dequeues a
        // tool change it drives the whole cycle itself — spindle down, M6, wait
        // for the machine to finish the carousel + touch-off, then go on a
        // "tangent" to return the machine to exactly where it was BEFORE the
        // change — and only then resumes chewing through the queue.  It watches
        // telemetry the whole time rather than trusting the controller to keep
        // its place (neocortex vs. motor cortex: plan, but watch the body).

        struct Step {
            enum class Kind { Move, ToolChange, Spindle, Dwell, Raw };
            Kind kind = Kind::Raw;

            // Move (WCS): feed <= 0 → rapid G0, feed > 0 → G1 F<feed>.
            double x = 0, y = 0, z = 0, a = 0;
            double feed = 0;

            int    slot    = 0;   // ToolChange
            double rpm     = 0;   // Spindle (> 0 → M3 S<rpm>, else M5)
            double seconds = 0;   // Dwell (G4 P<seconds>)
            std::string raw;      // Raw G-code line (include trailing newline)

            static Step moveTo(double x, double y, double z, double a, double feed) {
                Step s; s.kind = Kind::Move; s.x = x; s.y = y; s.z = z; s.a = a; s.feed = feed; return s;
            }
            static Step toolChange(int slot) { Step s; s.kind = Kind::ToolChange; s.slot = slot; return s; }
            static Step spindle(double rpm)   { Step s; s.kind = Kind::Spindle;    s.rpm  = rpm;  return s; }
            static Step dwell(double seconds) { Step s; s.kind = Kind::Dwell;      s.seconds = seconds; return s; }
            static Step raw_(std::string g)   { Step s; s.kind = Kind::Raw;        s.raw  = std::move(g); return s; }
        };

        // Hand Air the full program to execute.  Requires arming.
        bool enqueueProgram(const std::vector<Step>& program) {
            if (!isArmed()) { return false; }
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                steps_.assign(program.begin(), program.end());
                clearanceZ_       = computeClearance(program);
                haveLast_         = false;
                haveReturn_       = false;
                returnMotionSeen_ = false;
                returnWaitFrames_ = 0;
                resetFlow();
                exec_ = steps_.empty() ? Exec::Idle : Exec::Streaming;
            }
            executing.store(!program.empty());
            pump();
            return true;
        }

        void clearQueue() {
            std::lock_guard<std::mutex> lock(queueMutex);
            steps_.clear();
            resetFlow();
            executing.store(false);
            exec_             = Exec::Idle;
            haveLast_         = false;
            haveReturn_       = false;
            returnMotionSeen_ = false;
            returnWaitFrames_ = 0;
            tcGate.store(0);
            tcSentFrames.store(0);
        }

        // START button: stop if running, else stream the program the CAM view
        // builds in response to onStartRequested / the start event.
        void requestStart() {
            if (isExecuting()) {
                stop();
                return;
            }
            if (!isArmed()) {
                pushLog("Arm the machine before starting execution.");
                return;
            }
            if (onStartRequested) {
                onStartRequested();
                pushLog("Execution started.");
            }
            else {
                pushLog("No execute mode active in the CAM view.");
            }
            StartEvent e{};
            startDispatcher.tell(&Air::startEvent, e);
        }

        // ============================================================
        // Work-origin reference / WCS (consumed by the CAM world view)
        // ============================================================

        void captureMachineOrigin(float mx, float my, float mz, float ma = 0.0f) {
            originMx.store(mx); originMy.store(my); originMz.store(mz); originMa.store(ma);
            originValid.store(true);
        }

        float machineOriginA() const {
            return originValid.load() ? originMa.load() : 0.0f;
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

        void  setWcsA(float wa)  { wcsA.store(wa); wcsAValid.store(true); }
        float getWcsA()    const { return wcsA.load(); }
        bool  wcsAValid_() const { return wcsAValid.load(); }

        // Surface a message through the machine's log channel.  Lets the GUI
        // route its own user-facing notices through the same event stream as
        // machine replies (drained + emitted on the main thread).
        void log(std::string msg) { pushLog(std::move(msg)); }

    protected:

        // Dispatcher key slots (never called — used only as unique keys).
        virtual void telemetryEvent         (TelemetryEvent&)  {}
        virtual void stateEvent             (StateEvent&)      {}
        virtual void connectionEvent        (ConnectionEvent&) {}
        virtual void logEvent               (LogEvent&)        {}
        virtual void armEvent               (ArmEvent&)        {}
        virtual void startEvent             (StartEvent&)      {}
        virtual void toolChangeBeginEvent   (ToolChangeEvent&) {}
        virtual void toolChangeStandbyEvent (ToolChangeEvent&) {}
        virtual void toolChangeConfirmEvent (ToolChangeEvent&) {}
        virtual void toolChangeCompleteEvent(ToolChangeEvent&) {}

    private:

        using Clock = std::chrono::steady_clock;

        // -- Connection / IO ----------------------------------------

        Rev::Client* client = nullptr;

        // -- Animators (main-thread ticks via Process) --------------

        Rev::Core::Animator watchdog    { 500 };
        Rev::Core::Animator displayLoop { 7 };

        // Main-tick periods: brisk while connected, lazy while idle.
        static constexpr uint64_t LivePeriodMs = 7;
        static constexpr uint64_t IdlePeriodMs = 100;

        // -- Jog feeds ----------------------------------------------

        int jogFeedRate  = 1000;
        int jogFeedRateA = 3000;

        // -- Chain-poll state ---------------------------------------

        bool              statusInFlight = false;
        Clock::time_point statusSentAt;
        static constexpr int StatusTimeoutMs = 750;

        // -- Worker → main handoff ----------------------------------

        std::mutex              stateMutex;
        std::deque<std::string> pendingLog;

        std::string pendingState;
        float pendingPosX = 0, pendingPosY = 0, pendingPosZ = 0, pendingPosA = 0;
        bool  pendingPosValid     = false;
        bool  pendingResponseSeen = false;

        float pendingWcsA      = 0;
        bool  pendingWcsAValid = false;

        bool             pendingConnection      = false;
        ConnectionStatus pendingConnectionKind  = ConnectionStatus::Disconnected;
        std::string      pendingConnectionMsg;

        // -- Confirmed machine state (main thread) ------------------

        std::string machineState_;
        std::string lastMachineState_;
        float confX = 0, confY = 0, confZ = 0, confA = 0;
        bool  confValid = false;

        // -- Intent / display chase ---------------------------------

        float intentX = 0, intentY = 0, intentZ = 0, intentA = 0;
        float dispX = 0, dispY = 0, dispZ = 0, dispA = 0;
        bool  dispReady = false;
        Clock::time_point lastFrameTime = Clock::now();

        static constexpr float ChaseSpeedLinear  = 0.040f;  // mm/ms
        static constexpr float ChaseSpeedAngular = 0.120f;  // deg/ms

        // -- Telemetry mirror (atomic, read by the CAM view) --------

        std::atomic<float> telemX { 0 }, telemY { 0 }, telemZ { 0 }, telemA { 0 };
        std::atomic<bool>  telemValid { false };

        std::atomic<float> wcsA { 0 };
        std::atomic<bool>  wcsAValid { false };

        // -- Arming -------------------------------------------------

        std::atomic<bool> armed        { false };
        std::atomic<bool> spindleArmed { false };
        std::atomic<bool> executing    { false };

        // -- Program execution (action queue) + ATC gate -----------

        static constexpr int MaxInFlight = 2;

        // High-level execution phase.
        enum class Exec { Idle, Streaming, ToolChanging, Returning };

        std::mutex        queueMutex;
        std::deque<Step>  steps_;
        Exec              exec_ = Exec::Idle;
        int               inFlight = 0;
        std::atomic<int>  pendingOk { 0 };

        // Safe clearance height (WCS), computed from the program's max Z.
        double clearanceZ_ = 5.0;

        // Last commanded WCS position — the point to return to after a change.
        double lastX_ = 0, lastY_ = 0, lastZ_ = 0, lastA_ = 0;
        bool   haveLast_ = false;

        // The return target captured at the moment a tool change began.
        double retX_ = 0, retY_ = 0, retZ_ = 0, retA_ = 0;
        bool   haveReturn_ = false;

        // While Returning: watch the machine actually move, then settle.
        bool   returnMotionSeen_ = false;
        int    returnWaitFrames_ = 0;

        std::atomic<int> tcGate       { 0 };   // 0 None, 1 Seeking, 2 Standby, 3 Confirming
        std::atomic<int> tcSentFrames { 0 };   // ~7 ms ticks since M6 (Seeking timeout)
        int              tcSlot       = 0;     // slot of the change in progress
        int              lastGate_    = 0;     // last gate value an event was emitted for
        bool             tcAutoConfirm = false;// auto-press Ok at Standby for this change
        std::atomic<int> loadedSlot    { 0 };  // slot currently loaded + touched off (0 = none)

        // -- Work-origin reference ----------------------------------

        std::atomic<float> originMx { 0 }, originMy { 0 }, originMz { 0 }, originMa { 0 };
        std::atomic<bool>  originValid { false };
        std::atomic<float> cadOriginX { 0 }, cadOriginY { 0 }, cadOriginZ { 0 };
        std::atomic<bool>  cadOriginValid { false };

        // -- RX line assembly ---------------------------------------

        std::string rxBuffer_;

        // -- Dispatchers --------------------------------------------

        Rev::Core::Dispatcher<TelemetryEvent>  telemetryDispatcher;
        Rev::Core::Dispatcher<StateEvent>      stateDispatcher;
        Rev::Core::Dispatcher<ConnectionEvent> connectionDispatcher;
        Rev::Core::Dispatcher<LogEvent>        logDispatcher;
        Rev::Core::Dispatcher<ArmEvent>        armDispatcher;
        Rev::Core::Dispatcher<StartEvent>      startDispatcher;
        Rev::Core::Dispatcher<ToolChangeEvent> tcBeginDispatcher;
        Rev::Core::Dispatcher<ToolChangeEvent> tcStandbyDispatcher;
        Rev::Core::Dispatcher<ToolChangeEvent> tcConfirmDispatcher;
        Rev::Core::Dispatcher<ToolChangeEvent> tcCompleteDispatcher;

        // ============================================================
        // Internal helpers
        // ============================================================

        bool requireConnected() {
            if (!connected()) { pushLog("Not connected."); return false; }
            return true;
        }

        void disarmInternal() {
            armed.store(false);
            spindleArmed.store(false);
        }

        // Direct, un-logged send (jogging spams these).
        void rawSend(const std::string& line) {
            if (connected()) { client->send(line); }
        }

        // Logged send for discrete commands.
        void sendLine(const std::string& line) {
            if (!connected()) { pushLog("Not connected."); return; }
            client->send(line);
            std::string d = line;
            while (!d.empty() && (d.back() == '\n' || d.back() == '\r')) { d.pop_back(); }
            pushLog(std::format("> {}", d));
        }

        // Chain-poll primitive: issues "?" at most once until answered.
        void sendStatus() {
            if (!connected()) { return; }
            if (statusInFlight) { return; }
            client->send("?");
            statusSentAt   = Clock::now();
            statusInFlight = true;
        }

        // Thread-safe log append (worker or main).
        void pushLog(std::string msg) {
            std::lock_guard lock(stateMutex);
            pendingLog.push_back(std::move(msg));
        }

        void queueConnection(ConnectionStatus kind, const std::string& msg) {
            std::lock_guard lock(stateMutex);
            pendingConnection     = true;
            pendingConnectionKind = kind;
            pendingConnectionMsg  = msg;
        }

        void emitArm() {
            ArmEvent e{ isArmed(), isSpindleArmed() };
            armDispatcher.tell(&Air::armEvent, e);
        }

        void resetTelemetryState() {
            std::lock_guard lock(stateMutex);
            pendingState        = "";
            pendingPosValid     = false;
            pendingResponseSeen = false;
            pendingWcsAValid    = false;
            confValid           = false;
            dispReady           = false;
            machineState_.clear();
            lastMachineState_.clear();
            statusInFlight      = false;

            // A fresh connection knows nothing about what is physically in the
            // spindle — start as "no tool loaded" so the panel prompts for a
            // confirm before the first cut.
            loadedSlot.store(0);
            tcAutoConfirm = false;
        }

        // -- Worker-thread RX ---------------------------------------

        void onData(Rev::Client::DataEvent& e) {

            rxBuffer_.append(e.data.begin(), e.data.end());

            size_t start = 0;
            while (true) {
                size_t nl = rxBuffer_.find_first_of("\r\n", start);
                if (nl == std::string::npos) { break; }
                if (nl > start) { processLine(rxBuffer_.substr(start, nl - start)); }
                start = nl + 1;
            }
            if (start > 0) { rxBuffer_.erase(0, start); }
        }

        void processLine(const std::string& msg) {

            if (msg.size() > 1 && msg.front() == '<') {

                size_t delim = msg.find_first_of(",|>", 1);
                std::string state = (delim != std::string::npos) ? msg.substr(1, delim - 1) : "";

                float x = 0, y = 0, z = 0, a = 0;
                size_t mp  = msg.find("MPos:");
                bool posOk = (mp != std::string::npos) &&
                             sscanf(msg.c_str() + mp + 5, "%f,%f,%f,%f", &x, &y, &z, &a) >= 3;

                float wx = 0, wy = 0, wz = 0, wa = 0;
                size_t wp   = msg.find("WPos:");
                bool wposOk = (wp != std::string::npos) &&
                              sscanf(msg.c_str() + wp + 5, "%f,%f,%f,%f", &wx, &wy, &wz, &wa) >= 4;

                std::lock_guard lock(stateMutex);
                pendingState = state;
                if (posOk) {
                    pendingPosX = x; pendingPosY = y; pendingPosZ = z; pendingPosA = a;
                    pendingPosValid = true;
                }
                if (wposOk) {
                    pendingWcsA      = wa;
                    pendingWcsAValid = true;
                }
                pendingResponseSeen = true;
                return;
            }

            if (msg == "ok" || msg.starts_with("ok - ignore:")) {
                pendingOk.fetch_add(1);
                return;
            }

            pushLog(std::format("< {}", msg));
        }

        // -- Main-thread drain + events -----------------------------

        // Returns true if a fresh telemetry frame was committed.
        bool drainTelemetry() {

            bool        logDirty  = false;
            bool        posDirty  = false;
            bool        replySeen = false;
            float       nx = 0, ny = 0, nz = 0, na = 0;
            float       nwa = 0;
            bool        nwaValid = false;
            std::string nstate;

            bool             connDirty = false;
            ConnectionStatus connKind  = ConnectionStatus::Disconnected;
            std::string      connMsg;

            std::deque<std::string> drainedLog;

            {
                std::lock_guard lock(stateMutex);

                if (!pendingLog.empty()) {
                    drainedLog.swap(pendingLog);
                    logDirty = true;
                }

                nstate    = pendingState;
                replySeen = pendingResponseSeen;
                if (pendingPosValid) {
                    nx = pendingPosX; ny = pendingPosY; nz = pendingPosZ; na = pendingPosA;
                    pendingPosValid = false;
                    posDirty = true;
                }
                if (pendingWcsAValid) {
                    nwa = pendingWcsA; nwaValid = true;
                    pendingWcsAValid = false;
                }
                pendingResponseSeen = false;

                if (pendingConnection) {
                    connDirty = true;
                    connKind  = pendingConnectionKind;
                    connMsg   = pendingConnectionMsg;
                    pendingConnection = false;
                }
            }

            // Emit drained log lines.
            if (logDirty) {
                for (auto& l : drainedLog) {
                    LogEvent e{ std::move(l) };
                    logDispatcher.tell(&Air::logEvent, e);
                }
            }

            // Emit connection change.
            if (connDirty) {
                ConnectionEvent e{ connKind, connMsg };
                connectionDispatcher.tell(&Air::connectionEvent, e);
            }

            // Machine-state change → event.
            if (machineState_ != nstate) {
                machineState_ = nstate;
                StateEvent e{ machineState_ };
                stateDispatcher.tell(&Air::stateEvent, e);
            }

            // Advance the tool-change lifecycle from the latest state; this
            // fires the Standby / Complete events at their transitions.
            updateToolChangeGate(machineState_);

            if (replySeen) { statusInFlight = false; }

            if (posDirty) {
                confX = nx; confY = ny; confZ = nz; confA = na;
                confValid = true;
            }

            if (nwaValid) { setWcsA(nwa); }

            return posDirty;
        }

        void tickDisplay() {

            auto now = Clock::now();
            float dtMs = std::chrono::duration<float, std::milli>(now - lastFrameTime).count();
            lastFrameTime = now;

            bool freshFrame = drainTelemetry();

            if (!statusInFlight) { sendStatus(); }

            if (!confValid) { dispReady = false; return; }

            if (!dispReady) {
                dispX = intentX = confX;
                dispY = intentY = confY;
                dispZ = intentZ = confZ;
                dispA = intentA = confA;
                dispReady = true;
                publishLivePosition();
                return;
            }

            if (freshFrame) {
                dispX = confX; dispY = confY; dispZ = confZ; dispA = confA;

                const bool nowQuiet = (machineState_     == "Idle" || machineState_     == "Alarm");
                const bool wasQuiet = (lastMachineState_ == "Idle" || lastMachineState_ == "Alarm");
                if (nowQuiet && !wasQuiet) {
                    intentX = confX; intentY = confY; intentZ = confZ; intentA = confA;
                }
            }

            if (!machineState_.empty()) { lastMachineState_ = machineState_; }

            stepToward(dispX, intentX, ChaseSpeedLinear  * dtMs);
            stepToward(dispY, intentY, ChaseSpeedLinear  * dtMs);
            stepToward(dispZ, intentZ, ChaseSpeedLinear  * dtMs);
            stepToward(dispA, intentA, ChaseSpeedAngular * dtMs);

            publishLivePosition();
        }

        // Store + broadcast the smoothed live position.
        void publishLivePosition() {
            if (!dispReady) { return; }

            telemX.store(dispX); telemY.store(dispY);
            telemZ.store(dispZ); telemA.store(dispA);
            telemValid.store(true);

            if (onTelemetry) { onTelemetry(dispX, dispY, dispZ, dispA); }

            TelemetryEvent e{ dispX, dispY, dispZ, dispA };
            telemetryDispatcher.tell(&Air::telemetryEvent, e);
        }

        static void stepToward(float& cur, float target, float maxStep) {
            float diff = target - cur;
            if (std::abs(diff) <= maxStep) { cur = target; return; }
            cur += (diff > 0 ? maxStep : -maxStep);
        }

        // -- Tool-change gate + lifecycle events --------------------
        //
        // Called every drain (main thread, no locks held).  First advances the
        // gate from the latest machine state, then emits one discrete event for
        // whatever transition occurred since the previous drain.  All four
        // lifecycle events funnel through here so manual and streamed (ATC)
        // changes behave identically:
        //
        //   gate:  0 None → 1 Seeking → 2 Standby → 3 Confirming → 0 None
        //   event:        begin        standby      confirm        complete
        //
        // The gate is set to 1 by changeTool()/pump() (begin), to 3 by
        // confirmToolChange() (confirm); the 1→2, 1→0, 2→0 and 3→0 advances
        // happen here from the reported state / timeout.
        void updateToolChangeGate(const std::string& state) {

            const bool isToolState = (state == "Tool");
            int g = tcGate.load();

            if (g == 1) {                                   // Seeking
                const int f = tcSentFrames.fetch_add(1);
                if (isToolState)   { tcGate.store(2); g = 2; }      // → Standby
                else if (f > 4285) { tcGate.store(0); g = 0; }      // ~30 s timeout
            }
            else if ((g == 2 || g == 3) && !isToolState) {  // Standby/Confirming
                tcGate.store(0); g = 0;                            // → Complete
            }

            if (g != lastGate_) {

                const int from = lastGate_;
                lastGate_ = g;

                switch (g) {
                    case 1: emitToolChange(tcBeginDispatcher,    &Air::toolChangeBeginEvent,    ToolChangePhase::Seeking);    break;
                    case 2: emitToolChange(tcStandbyDispatcher,  &Air::toolChangeStandbyEvent,  ToolChangePhase::Standby);    break;
                    case 3: emitToolChange(tcConfirmDispatcher,  &Air::toolChangeConfirmEvent,  ToolChangePhase::Confirming); break;
                    case 0:
                        if (from != 0) {
                            loadedSlot.store(tcSlot);   // this slot is now loaded + touched off
                            tcAutoConfirm = false;
                            emitToolChange(tcCompleteDispatcher, &Air::toolChangeCompleteEvent, ToolChangePhase::None);
                        }
                        break;
                }
            }

            // Auto-confirm: if this change requested it, press "Ok" for the
            // operator the moment the machine reaches Standby.
            if (tcAutoConfirm && tcGate.load() == 2) {
                confirmToolChange();   // → Confirming; Confirm event fires next drain
            }
        }

        void emitToolChange(
            Rev::Core::Dispatcher<ToolChangeEvent>& dispatcher,
            void (Air::*key)(ToolChangeEvent&),
            ToolChangePhase phase
        ) {
            ToolChangeEvent e{ tcSlot, phase };
            dispatcher.tell(key, e);
        }

        // -- Execution helpers --------------------------------------

        void resetFlow() { inFlight = 0; pendingOk.store(0); }

        static double computeClearance(const std::vector<Step>& program) {
            double maxZ = 0.0;
            bool   any  = false;
            for (const Step& s : program) {
                if (s.kind == Step::Kind::Move) {
                    maxZ = any ? std::max(maxZ, s.z) : s.z;
                    any  = true;
                }
            }
            return (any ? maxZ : 0.0) + 5.0;
        }

        // Translate a non-tool-change step to a G-code line, tracking the last
        // commanded position so we know where to return after a change.
        std::string gcodeForStep(const Step& s) {
            switch (s.kind) {
                case Step::Kind::Move:
                    lastX_ = s.x; lastY_ = s.y; lastZ_ = s.z; lastA_ = s.a; haveLast_ = true;
                    return s.feed > 0.0
                        ? std::format("G1 X{:.3f} Y{:.3f} Z{:.3f} A{:.3f} F{:.1f}\n", s.x, s.y, s.z, s.a, s.feed)
                        : std::format("G0 X{:.3f} Y{:.3f} Z{:.3f} A{:.3f}\n", s.x, s.y, s.z, s.a);
                case Step::Kind::Spindle:
                    return s.rpm > 0.0 ? std::format("M3 S{:.0f}\n", s.rpm) : std::string("M5\n");
                case Step::Kind::Dwell:
                    return std::format("G4 P{:.3f}\n", s.seconds);
                case Step::Kind::Raw:
                    return s.raw;
                default:
                    return std::string();
            }
        }

        // Kick off a tool change inside a running program: stop the spindle,
        // let it settle, request the change, and engage the gate.  The gate
        // (driven from telemetry in updateToolChangeGate) carries it through
        // Standby/Confirm/Complete; pump() waits on it.
        void beginToolChangeOrchestration(int slot) {
            client->send("M5\n");
            client->send("G4 P2\n");
            client->send(std::format("M6 T{}\n", slot));
            tcSlot        = slot;
            tcAutoConfirm = false;   // ATC auto-proceeds; a manual tool waits for the operator
            tcGate.store(1);
            tcSentFrames.store(0);
            resetFlow();             // the gate, not ok-counting, paces the change
            pushLog(std::format("Program tool change: T{}.", slot));
        }

        // After the change completes, the controller is parked over the touch-
        // off pad.  Lift clear, then rapid back to the pre-change position so
        // the next cut resumes exactly where it left off.
        void sendReturnMoves() {
            resetFlow();
            client->send(std::format("G90 G0 Z{:.3f}\n", clearanceZ_));
            inFlight++;
            if (haveReturn_) {
                client->send(std::format("G0 X{:.3f} Y{:.3f} A{:.3f}\n", retX_, retY_, retA_));
                inFlight++;
                client->send(std::format("G0 Z{:.3f}\n", retZ_));
                inFlight++;
                pushLog(std::format(
                    "Returning to pre-change position (X{:.3f} Y{:.3f} Z{:.3f}).",
                    retX_, retY_, retZ_));
            }
            returnMotionSeen_ = false;
            returnWaitFrames_ = 0;
        }

        // -- Execution pump (main thread) ---------------------------

        void pump() {

            std::lock_guard<std::mutex> lock(queueMutex);

            inFlight -= pendingOk.exchange(0);
            if (inFlight < 0) { inFlight = 0; }

            if (!connected()) { return; }

            switch (exec_) {

                case Exec::Idle:
                    break;

                case Exec::Streaming: {

                    while (inFlight < MaxInFlight && !steps_.empty()) {

                        Step& s = steps_.front();

                        if (s.kind == Step::Kind::ToolChange) {
                            // Drain everything already commanded first, so the
                            // machine is physically AT the pre-change point.
                            if (inFlight > 0) { break; }

                            haveReturn_ = haveLast_;
                            if (haveReturn_) {
                                retX_ = lastX_; retY_ = lastY_; retZ_ = lastZ_; retA_ = lastA_;
                            }
                            beginToolChangeOrchestration(s.slot);
                            steps_.pop_front();
                            exec_ = Exec::ToolChanging;
                            return;   // hand off; nothing more streams this tick
                        }

                        client->send(gcodeForStep(s));
                        steps_.pop_front();
                        inFlight++;
                    }

                    if (steps_.empty() && inFlight == 0) {
                        exec_ = Exec::Idle;
                        executing.store(false);
                        pushLog("Program complete.");
                    }
                    break;
                }

                case Exec::ToolChanging:
                    // Wait for the WHOLE change (carousel + touch-off) to finish
                    // — i.e. the gate to fully clear — before touching anything.
                    if (tcGate.load() == 0) {
                        sendReturnMoves();
                        exec_ = Exec::Returning;
                    }
                    break;

                case Exec::Returning:
                    // Don't trust the buffer: watch the machine actually move
                    // back, then settle to Idle, before resuming the cut.
                    if (!returnMotionSeen_) {
                        if (machineState_ == "Run" || machineState_ == "Jog") {
                            returnMotionSeen_ = true;
                        }
                        else if (++returnWaitFrames_ > 600) {   // ~4 s: nothing to do / missed
                            returnMotionSeen_ = true;
                        }
                    }
                    else if (inFlight == 0 &&
                             (machineState_ == "Idle" || machineState_ == "Alarm")) {
                        exec_ = Exec::Streaming;
                    }
                    break;
            }
        }
    };

    // Transitional alias: existing call sites (CAM world view, preview bar)
    // refer to Carvera::MachineLink.  Air is its sole successor.
    using MachineLink = Air;
}
