module;

#include <algorithm>
#include <atomic>
#include <cctype>
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

        // Tool-change lifecycle.  A change walks None -> Seeking -> Standby ->
        // (Confirming) -> None:
        //
        //   None        no change in progress.
        //   Seeking     M6 issued; the machine is travelling to the change
        //               position (it has not yet reported "Tool" state).
        //   Standby     the machine is at the change position and waiting --
        //               for the ATC carousel, or for the operator to fit the
        //               tool and confirm.  This is the blue-light state.
        //   Confirming  the operator confirmed; the machine is finishing the
        //               cycle (tool touch-off / height measurement).
        //
        // Each transition emits a discrete event (begin / standby / confirm /
        // complete) so the GUI can react precisely.
        enum class ToolChangePhase { None, Seeking, Standby, Confirming };

        // Internal orchestration phase.  MORE granular than the public
        // ToolChangePhase because robustness demands separating "M6 sent,
        // awaiting machine ack" (Requested) from "machine confirmed it is
        // beginning the ATC" (Seeking).  Every transition out of a non-None
        // phase requires positive evidence (a specific machine reply or a
        // verified state change), never just an inference from having sent
        // a command.  Mapped onto ToolChangePhase by toolChangePhase().
        enum class TcPhase {
            None,         // No tool change in progress.
            Requested,    // M5/G4/M6 sent.  Awaiting "Please change..." ack.
            Seeking,      // M6 acked.  Awaiting machine to enter Tool state.
            Standby,      // Machine at ATC prompt (state == Tool).  Awaiting confirm.
            Confirming,   // Confirm sent.  Awaiting state to leave Tool.
            Finishing,    // Touch-off + return in progress.  Awaiting sustained Idle.
            Aborted,      // Operation failed.  Operator must acknowledge / reset.
        };

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
        // `aborted` is set on the Complete channel when the change ended
        // because of a failure rather than success; `reason` is a single
        // operator-facing sentence explaining what went wrong.
        struct ToolChangeEvent {
            int             slot = 0;
            ToolChangePhase phase = ToolChangePhase::None;
            bool            aborted = false;
            std::string     reason;
        };

        // Result of a precondition-checked operation.  The GUI surfaces
        // `reason` directly to the operator when ok is false.
        struct OperationResult {
            bool        ok = true;
            std::string reason;

            static OperationResult success()                       { return { true, "" }; }
            static OperationResult failure(std::string r)          { return { false, std::move(r) }; }
        };

        // -- Singleton -----------------------------------------------

        static Air& instance() {
            static Air air;
            return air;
        }

        Air() {

            // Watchdog -- re-issues "?" if a chain-polled reply never arrived.
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
        // Event registration (dispatcher-based -- many listeners allowed)
        // ============================================================

        void onTelemetryFrame(const std::function<void(TelemetryEvent&)>& f) { telemetryDispatcher.listen(&Air::telemetryEvent, f); }
        void onState         (const std::function<void(StateEvent&)>&      f) { stateDispatcher.listen(&Air::stateEvent, f); }
        void onConnection    (const std::function<void(ConnectionEvent&)>& f) { connectionDispatcher.listen(&Air::connectionEvent, f); }
        void onLog           (const std::function<void(LogEvent&)>&        f) { logDispatcher.listen(&Air::logEvent, f); }
        void onArm           (const std::function<void(ArmEvent&)>&        f) { armDispatcher.listen(&Air::armEvent, f); }

        // Tool-change lifecycle channels:
        //   Begin     -- a change was just requested (M6 sent, machine seeking).
        //   Standby   -- the machine reached the change position and is waiting
        //               (carousel, or operator confirmation -- the blue light).
        //   Confirm   -- the operator confirmed; the machine is finishing.
        //   Complete  -- the change is fully done; back to normal operation.
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
                // Ask the controller for its parser state -- the response (a
                // bracketed line like "[G0 G54 ... T1 F0 S0]") is parsed by
                // processLine and tells us which tool is already in the
                // spindle.  Without this we wouldn't know after a reconnect.
                client->send("$G\n");
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
            switch (tcPhase_) {
                case TcPhase::Requested:  return ToolChangePhase::Seeking;
                case TcPhase::Seeking:    return ToolChangePhase::Seeking;
                case TcPhase::Standby:    return ToolChangePhase::Standby;
                case TcPhase::Confirming: return ToolChangePhase::Confirming;
                case TcPhase::Finishing:  return ToolChangePhase::Confirming;
                case TcPhase::None:
                case TcPhase::Aborted:    return ToolChangePhase::None;
            }
            return ToolChangePhase::None;
        }

        // The currently-active orchestration phase, useful for UI/diagnostics.
        const std::string& toolChangeAbortReason() const { return tcAbortReason_; }

        bool toolChangeGated() const {
            return tcPhase_ != TcPhase::None && tcPhase_ != TcPhase::Aborted;
        }

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
        // active WCS (G54..G59), NOT machine coords -- so after a Set Origin
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
            dbg("[Air] jog cancel (0x85) sent");
        }

        // Rapid to an absolute MACHINE position (G53 -- independent of any work
        // offset).  Used by the GUI's origin "Goto".
        void goTo(float x, float y, float z, float a) {
            if (!connected() || !confValid) {
                pushLog("Connect and wait for position before moving.");
                return;
            }
            sendLine(std::format("G53 G0 X{:.3f} Y{:.3f} Z{:.3f} A{:.3f}\n", x, y, z, a));
            intentX = x; intentY = y; intentZ = z; intentA = a;
        }

        void home()   { if (requireConnected()) { sendLine("$H\n"); pushLog("Homing..."); } }
        void unlock() {
            if (!requireConnected()) { return; }
            sendLine("$X\n");
            pushLog("Unlock sent.");
            // Unlock is the operator's way of saying "I cleared the alarm" --
            // any aborted tool change can be acknowledged now.
            if (tcPhase_ == TcPhase::Aborted) { clearAbortedToolChange(); }
        }

        // Soft reset (Ctrl-X / 0x18).  Clears controller state, including any
        // stuck ATC cycle.  After a reset our orchestration view is invalid:
        // drop any in-flight tool change and start fresh.
        void reset() {
            if (!connected()) { return; }
            client->send(std::string(1, char(0x18)));
            pushLog("Reset sent (Ctrl-X / 0x18).");
            if (isPhaseActive(tcPhase_)) {
                tcAbort("Reset issued by operator.");
            }
            clearAbortedToolChange();
        }

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
        // The operator must explicitly confirm at the machine OR via the GUI's
        // confirmToolChange() -- Air never auto-confirms.
        //
        // Robust changeTool: every precondition is checked before any byte
        // hits the wire, and the result tells the caller exactly why a refusal
        // happened so the GUI can show a precise message.  Successful return
        // means M5/G4/M6 went out and we are now in the Requested phase,
        // awaiting positive acknowledgement from the machine ("Please change
        // the tool to: T<n>") before advancing.
        //
        // The phase ONLY advances on evidence -- not on optimism.  If the M6
        // is silently no-op'd or rejected, processLine catches the rejection
        // message and aborts the change with a clear reason; if no reply
        // arrives within kRequestedTimeoutMs, the phase tick aborts on
        // timeout.  Either way we do NOT pretend the change is happening.
        OperationResult changeTool(int slot) {
            if (auto r = preflightToolChange(slot); !r.ok) {
                pushLog(std::format("[changeTool] refused: {}", r.reason));
                return r;
            }
            beginToolChangeOrchestration(slot, /*resetStreamFlow=*/false);
            return OperationResult::success();
        }

        // The slot of the tool currently loaded + touched-off (0 = none/unknown,
        // e.g. right after connecting).  Set when a change Completes.
        int loadedToolSlot() const { return loadedSlot.load(); }

        // Confirm a tool change -- the protocol equivalent of pressing the
        // physical button on the machine so the controller leaves the M490.1
        // wait state and proceeds to touch-off.  Only meaningful while the
        // machine is in Standby (waiting on the operator).
        //
        // PROBE MODE: until we know what command Carvera's M490.1 actually
        // listens for, each call to confirmToolChange tries the NEXT candidate
        // in the table and logs which one it sent.  See CarveraREADME.md.
        // Important: this does NOT poke the phase forward.  The phase ONLY
        // advances when we observe the machine actually leave Tool state in
        // response to the byte we sent -- in updateToolChangeGate.
        OperationResult confirmToolChange() {
            if (!connected()) {
                return OperationResult::failure("Not connected to machine.");
            }
            if (tcPhase_ != TcPhase::Standby) {
                return OperationResult::failure(std::format(
                    "Confirm ignored: not at the ATC prompt (current phase: {}).",
                    tcPhaseName(tcPhase_)));
            }

            struct Candidate {
                std::string payload;
                const char* desc;
            };
            static const Candidate kCandidates[] = {
                { std::string(1, char(0x2A)),       "'*'  (0x2A)  Smoothie play/continue"     },
                { std::string(1, char(0x7E)),       "'~'  (0x7E)  Grbl cycle start"            },
                { std::string("\n"),                "'\\n' bare newline"                       },
                { std::string("M600\n"),            "M600 (Marlin/Smoothie filament-change resume)" },
                { std::string("M601\n"),            "M601 (Smoothie continue from pause)"      },
                { std::string("M0\n"),              "M0   (program stop / skip pause)"         },
                { std::string("M6\n"),              "M6   (re-issue bare tool change)"         },
                { std::string("M491\n"),            "M491 (Carvera M-code adjacent to M490)"   },
                { std::string("M492\n"),            "M492 (Carvera M-code adjacent to M490)"   },
                { std::string("M493\n"),            "M493 (Carvera tool-length probe)"         },
                { std::string("M495\n"),            "M495 (Carvera ATC sub-op)"                },
                { std::string("M496\n"),            "M496 (Carvera ATC sub-op)"                },
            };
            static constexpr size_t kCount = sizeof(kCandidates) / sizeof(kCandidates[0]);

            if (confirmProbeStandbyGen_ != tcStandbyGen_) {
                confirmProbeStandbyGen_ = tcStandbyGen_;
                confirmProbeIndex_      = 0;
            }

            const size_t i = confirmProbeIndex_ % kCount;
            const Candidate& c = kCandidates[i];

            client->send(c.payload);
            pushLog(std::format(
                "[confirm probe {}/{}] {} - if the next line is "
                "\"machine state 'Tool' -> 'Run'\", this is the command",
                i + 1, kCount, c.desc));

            confirmProbeIndex_ = i + 1;
            return OperationResult::success();
        }

        // Operator-driven abort: clears any pending tool-change phase so the
        // GUI can be unstuck without a full machine reset.  Use when the
        // operator has dealt with whatever went wrong (e.g. pressed the
        // physical button to clear a stuck ATC, did the change by hand at the
        // machine, etc.).  Does NOT send anything to the machine -- it only
        // resets OUR view of the orchestration.
        void clearAbortedToolChange() {
            if (tcPhase_ == TcPhase::Aborted) {
                tcAbortReason_.clear();
                tcTransition(TcPhase::None, "operator cleared");
            }
        }

        // Pre-flight precondition checks.  These are the single source of
        // truth for "can we do X right now?" -- called both by GUI actions
        // before the user even invokes them (to enable/disable buttons) and
        // by the action methods themselves (defence in depth).
        OperationResult preflightToolChange(int slot) const {
            if (!connected())                { return OperationResult::failure("Not connected to machine."); }
            if (slot < 1 || slot > 6)        { return OperationResult::failure(std::format("Invalid slot T{}. Carvera ATC has slots 1-6.", slot)); }
            if (tcPhase_ == TcPhase::Aborted){ return OperationResult::failure("Previous tool change was aborted. Acknowledge and reset before retrying."); }
            if (tcPhase_ != TcPhase::None)   { return OperationResult::failure(std::format("Another tool change is in progress (phase: {}).", tcPhaseName(tcPhase_))); }
            if (machineState_ == "Alarm")    { return OperationResult::failure("Machine is in Alarm. Press Unlock ($X) or Reset before changing tool."); }
            if (machineState_ == "Tool")     { return OperationResult::failure("Machine is ALREADY in an ATC cycle from a previous session. Press Reset (Ctrl-X / 0x18) to clear it, then retry."); }
            if (machineState_ == "Hold")     { return OperationResult::failure("Machine is in Hold. Resume or Reset before changing tool."); }
            if (executing.load())            { return OperationResult::failure("A program is executing. Stop it before changing tool."); }
            if (slot == loadedSlot.load())   { return OperationResult::failure(std::format("T{} is already the loaded tool.", slot)); }
            return OperationResult::success();
        }

        OperationResult preflightStart() const {
            if (!connected())              { return OperationResult::failure("Not connected to machine."); }
            if (!armed.load())             { return OperationResult::failure("Arm the machine before starting execution."); }
            if (tcPhase_ != TcPhase::None) { return OperationResult::failure(std::format("Tool change in progress (phase: {}).", tcPhaseName(tcPhase_))); }
            if (machineState_ == "Alarm")  { return OperationResult::failure("Machine is in Alarm. Press Unlock or Reset first."); }
            if (machineState_ == "Hold")   { return OperationResult::failure("Machine is in Hold. Resume or Reset first."); }
            if (executing.load())          { return OperationResult::failure("A program is already executing."); }
            return OperationResult::success();
        }

        // Internal helper -- name a phase for diagnostic messages.
        static const char* tcPhaseName(TcPhase p) {
            switch (p) {
                case TcPhase::None:       return "None";
                case TcPhase::Requested:  return "Requested";
                case TcPhase::Seeking:    return "Seeking";
                case TcPhase::Standby:    return "Standby";
                case TcPhase::Confirming: return "Confirming";
                case TcPhase::Finishing:  return "Finishing";
                case TcPhase::Aborted:    return "Aborted";
            }
            return "?";
        }

        // ============================================================
        // Program execution -- the orchestration / "action queue" model
        // ============================================================
        //
        // Callers do NOT hand Air a flat list of raw G-code.  They hand it a
        // queue of high-level intents (moves, spindle changes, tool changes),
        // and Air trickle-sends the controller a few lines at a time under flow
        // control.  Crucially, Air OWNS the dangerous bits: when it dequeues a
        // tool change it drives the whole cycle itself -- spindle down, M6, wait
        // for the machine to finish the carousel + touch-off, then go on a
        // "tangent" to return the machine to exactly where it was BEFORE the
        // change -- and only then resumes chewing through the queue.  It watches
        // telemetry the whole time rather than trusting the controller to keep
        // its place (neocortex vs. motor cortex: plan, but watch the body).

        struct Step {
            enum class Kind { Move, ToolChange, Spindle, Dwell, Raw };
            Kind kind = Kind::Raw;

            // Move (WCS): feed <= 0 -> rapid G0, feed > 0 -> G1 F<feed>.
            double x = 0, y = 0, z = 0, a = 0;
            double feed = 0;

            int    slot    = 0;   // ToolChange
            double rpm     = 0;   // Spindle (> 0 -> M3 S<rpm>, else M5)
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
            if (!isArmed()) {
                dbg("[Air] enqueueProgram refused (not armed); %zu steps", program.size());
                return false;
            }
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
            dbg("[Air] enqueueProgram: %zu steps, clearanceZ=%.3f",
                program.size(), clearanceZ_);
            executing.store(!program.empty());
            pump();
            return true;
        }

        // Public entry: takes the queue mutex.  Safe to call from outside any
        // locked context.  Do NOT call this from anywhere that already holds
        // queueMutex (e.g. pump's tool-change-aborted branch) -- use
        // clearQueueLocked() there instead.  std::mutex is non-recursive and a
        // re-entrant lock throws std::system_error.
        void clearQueue() {
            std::lock_guard<std::mutex> lock(queueMutex);
            clearQueueLocked();
        }

        // Same cleanup as clearQueue() but assumes the caller already holds
        // queueMutex.  Splitting the implementation lets pump() purge the
        // queue from inside its own locked region without deadlocking.
        void clearQueueLocked() {
            steps_.clear();
            resetFlow();
            executing.store(false);
            exec_             = Exec::Idle;
            haveLast_         = false;
            haveReturn_       = false;
            returnMotionSeen_ = false;
            returnWaitFrames_ = 0;
            if (isPhaseActive(tcPhase_)) {
                tcAbort("Program queue cleared while tool change was active.");
            }
            else {
                tcPhase_           = TcPhase::None;
                lastEmittedPhase_  = TcPhase::None;
                tcAbortReason_.clear();
            }
            tcFinishIdleFrames_ = 0;
        }

        // START button: stop if running, else preflight + stream the program
        // the CAM view builds in response to onStartRequested / the start
        // event.  Returns the preflight result so the GUI can show the exact
        // refusal reason instead of a generic "didn't work".
        OperationResult requestStart() {
            if (isExecuting()) { stop(); return OperationResult::success(); }
            if (auto r = preflightStart(); !r.ok) {
                pushLog(std::format("[start] refused: {}", r.reason));
                return r;
            }
            if (onStartRequested) {
                onStartRequested();
                pushLog("Execution started.");
            }
            else {
                pushLog("No execute mode active in the CAM view.");
                return OperationResult::failure("No execute mode active in the CAM view.");
            }
            StartEvent e{};
            startDispatcher.tell(&Air::startEvent, e);
            return OperationResult::success();
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

        // Dispatcher key slots (never called -- used only as unique keys).
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

        // -- Worker -> main handoff ----------------------------------

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

        // Last commanded WCS position -- the point to return to after a change.
        double lastX_ = 0, lastY_ = 0, lastZ_ = 0, lastA_ = 0;
        bool   haveLast_ = false;

        // The return target captured at the moment a tool change began.
        double retX_ = 0, retY_ = 0, retZ_ = 0, retA_ = 0;
        bool   haveReturn_ = false;

        // While Returning: watch the machine actually move, then settle.
        bool   returnMotionSeen_ = false;
        int    returnWaitFrames_ = 0;

        TcPhase           tcPhase_           = TcPhase::None;
        TcPhase           lastEmittedPhase_  = TcPhase::None;
        int               tcSlot             = 0;          // slot of the change in progress
        std::string       tcAbortReason_;                  // operator-facing failure message
        Clock::time_point tcPhaseEnteredAt_;               // wall clock when tcPhase_ last changed
        int               tcFinishIdleFrames_ = 0;         // sustained-Idle counter during Finishing

        // Phase timeouts.  Each one is a budget for the machine to make
        // progress before we declare the operation stuck and abort.  Times
        // chosen generously to cover slow ATC carousels and long touch-offs.
        static constexpr int kRequestedTimeoutMs   = 5000;     // M6 ack
        static constexpr int kSeekingTimeoutMs     = 30000;    // reach ATC prompt
        static constexpr int kFinishingTimeoutMs   = 90000;    // touch-off + return
        static constexpr int kFinishIdleSettleFr   = 70;       // ~500 ms in Idle = done
        // Number of consecutive Idle telemetry frames seen during Confirming.
        // We need a sustained Idle to declare Complete (the controller briefly
        // visits non-Tool states during touch-off motion that aren't "done").
        // At the ~7 ms display tick the constant below works out to ~500 ms.
        // confirmToolChange probe state.  Each Standby entry bumps
        // `tcStandbyGen_` (cheaply, on the main thread); confirmToolChange
        // resets `confirmProbeIndex_` when it notices the generation changed.
        int confirmProbeIndex_      = 0;
        int confirmProbeStandbyGen_ = 0;
        int tcStandbyGen_           = 0;
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

        // Thread-safe log append (worker or main).  Every line that the
        // operator sees in the panel's log also goes to the IDE debug console
        // via dbg(), prefixed so it's easy to grep through a mixed stream.
        // pushLog is the right hook for both because every UI-visible log line
        // is by construction non-spammy (machine replies, lifecycle events,
        // user actions) -- high-frequency stuff like telemetry never comes
        // through here.
        void pushLog(std::string msg) {
            dbg("[Air] %s", msg.c_str());
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
            // spindle -- start as "no tool loaded" so the panel prompts for a
            // confirm before the first cut.
            loadedSlot.store(0);

            // Drop any in-flight orchestration state.  After a fresh
            // connection we re-derive everything from the controller's
            // first status frame + $G reply.
            tcPhase_           = TcPhase::None;
            lastEmittedPhase_  = TcPhase::None;
            tcAbortReason_.clear();
            tcFinishIdleFrames_ = 0;
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

                // Some Carvera/Smoothie builds include the loaded tool number
                // in the status frame as "|T:<n>" or "|TLO:..." -- parse it so
                // we never have to guess what's currently in the spindle.
                pickToolFromAnywhere(msg);

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

            // Parser-state replies from `$G` (and similar inspect lines) come
            // back wrapped in square brackets, e.g.
            //   [G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]
            // We scan them for the current tool number so reconnecting to a
            // machine that already has a tool loaded immediately reflects that.
            if (!msg.empty() && msg.front() == '[') {
                pickToolFromAnywhere(msg);
            }

            // === Evidence-driven tool-change state machine advances ===
            //
            // The state machine ONLY advances out of Requested when we see
            // a positive ack from the machine; it ONLY aborts when we see an
            // explicit failure reply.  Both inputs are right here.

            // Positive ack for M6: Carvera prints this once the ATC sequence
            // is accepted.  This is what advances us out of Requested.
            if (tcPhase_ == TcPhase::Requested &&
                msg.find("Please change the tool to:") != std::string::npos)
            {
                tcTransition(TcPhase::Seeking, "machine acknowledged M6");
            }

            // Carvera-specific reject: M6 was sent while ATC is already mid-
            // cycle.  Hard abort with the precise recovery instruction.
            if (msg.find("ATC already begun") != std::string::npos &&
                isPhaseActive(tcPhase_))
            {
                tcAbort(
                    "Machine rejected M6 with 'ATC already begun'. A previous "
                    "ATC cycle is still pending on the controller. Press Reset "
                    "(Ctrl-X / 0x18) to clear it before retrying.");
            }

            // Grbl-style error replies abort whatever we were trying to do.
            // We surface the controller's exact code so the operator can look
            // it up.
            if ((msg.rfind("error:", 0) == 0 || msg.find(" error:") != std::string::npos) &&
                isPhaseActive(tcPhase_))
            {
                tcAbort(std::format(
                    "Machine rejected a command with '{}'. Tool change aborted.",
                    msg));
            }

            // ALARM messages.  The Alarm STATE is handled in
            // updateToolChangeGate, but explicit ALARM:N replies tell us
            // WHICH alarm; capture that for the abort reason.
            if (msg.rfind("ALARM", 0) == 0 && isPhaseActive(tcPhase_)) {
                tcAbort(std::format("Machine alarm: '{}'", msg));
            }

            pushLog(std::format("< {}", msg));
        }

        // Look for "T<n>" or "T:<n>" anywhere in `msg` and, if present,
        // record it as the currently-loaded tool slot.  Whitespace, '|', '['
        // and ']' are valid delimiters before T -- but T must NOT be preceded
        // by a letter/digit (otherwise we'd match e.g. "STAT").  Called from
        // both the status-frame parser and the bracketed parser-state reply.
        void pickToolFromAnywhere(const std::string& msg) {
            for (size_t i = 0; i < msg.size(); i++) {
                if (msg[i] != 'T') { continue; }
                if (i > 0) {
                    const char p = msg[i - 1];
                    const bool boundary = p == ' ' || p == '|' || p == '['
                                       || p == ',' || p == ':' || p == '>'
                                       || p == '<' || p == ';';
                    if (!boundary) { continue; }
                }
                size_t j = i + 1;
                if (j < msg.size() && msg[j] == ':') { j++; }    // T:<n> form
                if (j >= msg.size() || !std::isdigit((unsigned char)msg[j])) { continue; }
                int n = 0;
                while (j < msg.size() && std::isdigit((unsigned char)msg[j])) {
                    n = n * 10 + (msg[j] - '0');
                    j++;
                }
                // Plausibility check: Carvera ATC has a handful of slots, not
                // hundreds.  Anything > 99 is almost certainly a different
                // field we matched by accident (e.g. a time value).
                if (n < 0 || n > 99) { continue; }
                recordLoadedTool(n);
                return;
            }
        }

        // Update the cached loaded-slot from observed telemetry / parser state.
        // Quiet (no log) when value didn't change to avoid log spam; noisy
        // (one-line log) on every real change.
        void recordLoadedTool(int slot) {
            const int prev = loadedSlot.exchange(slot);
            if (prev != slot) {
                pushLog(std::format("Loaded tool detected: T{}.", slot));
            }
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
                dbg("[Air] connection -> %d (%s)", (int)connKind, connMsg.c_str());
                ConnectionEvent e{ connKind, connMsg };
                connectionDispatcher.tell(&Air::connectionEvent, e);
            }

            // Machine-state change -> event.
            if (machineState_ != nstate) {
                dbg("[Air] machine state '%s' -> '%s'",
                    machineState_.c_str(), nstate.c_str());
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
        // Evidence-driven tool-change state machine.  Every transition out of
        // a non-None phase requires positive evidence -- a verified machine
        // reply or a state transition we actually witnessed -- not just an
        // inference that something we sent must have worked.  Every active
        // phase has a budget; if no evidence arrives in time, we abort with a
        // clear reason instead of waiting forever.
        //
        //   None        -- no change in progress (terminal idle).
        //   Requested   -- M6 has been sent, awaiting "Please change the
        //                  tool to: T<n>" ack.  Timeout -> Abort.  Receipt of
        //                  "ATC already begun" / error: -> Abort.
        //   Seeking     -- M6 acked; machine moving to ATC.  Awaiting state
        //                  to enter Tool.  Timeout -> Abort.  Alarm -> Abort.
        //   Standby    -- machine at the M490.1 prompt.  Awaiting the
        //                  operator's confirm (GUI or physical button) which
        //                  is the only way out.  No timeout (operator may be
        //                  slow).  Alarm -> Abort.
        //   Confirming  -- confirm sent.  Awaiting state to leave Tool, which
        //                  proves the confirm was received and touch-off has
        //                  begun.  Timeout here means our confirm command is
        //                  wrong for this firmware -- Abort with a hint to
        //                  press the physical button.
        //   Finishing   -- touch-off + return in progress.  Awaiting sustained
        //                  Idle.  Timeout -> Abort.  Alarm -> Abort.
        //   Aborted     -- operator must clear via clearAbortedToolChange()
        //                  (or a successful subsequent operation).
        void updateToolChangeGate(const std::string& state) {

            const bool isToolState = (state == "Tool");
            const bool isIdle      = (state == "Idle" || state == "Alarm");
            const bool isAlarm     = (state == "Alarm");

            // Alarm during any active phase is an unconditional abort.  Any
            // further machine progress is meaningless until the operator
            // resets/unlocks.
            if (isAlarm && isPhaseActive(tcPhase_)) {
                tcAbort("Machine entered Alarm state during tool change.");
                return;
            }

            const int sincePhaseMs = msSincePhaseEntered();

            switch (tcPhase_) {
                case TcPhase::None:
                case TcPhase::Aborted:
                    break;

                case TcPhase::Requested:
                    // Wait for "Please change the tool to:" message; advance
                    // to Seeking from processLine, not from here.  Timeout
                    // catches the case where the machine never acks (M6 lost,
                    // network glitch, firmware refusal we didn't catch).
                    if (sincePhaseMs > kRequestedTimeoutMs) {
                        tcAbort(std::format(
                            "Machine did not acknowledge M6 within {} ms. "
                            "Check the connection and the machine state.",
                            kRequestedTimeoutMs));
                    }
                    break;

                case TcPhase::Seeking:
                    if (isToolState) {
                        tcStandbyGen_++;
                        tcTransition(TcPhase::Standby,
                            "machine reached ATC prompt (state -> Tool)");
                    }
                    else if (sincePhaseMs > kSeekingTimeoutMs) {
                        tcAbort(std::format(
                            "Machine did not reach the ATC prompt within {} s. "
                            "Carousel may be stuck or M6 was rejected.",
                            kSeekingTimeoutMs / 1000));
                    }
                    break;

                case TcPhase::Standby:
                    // Operator must confirm.  No timeout -- they may be slow.
                    // Physical-button confirm also satisfies us here because
                    // the machine will leave Tool state, and the confirming
                    // branch below catches that.
                    if (!isToolState) {
                        // Confirmed via physical button on the machine itself.
                        tcTransition(TcPhase::Finishing,
                            std::format("machine left Tool state (operator pressed "
                                        "physical button) -> '{}'", state));
                    }
                    break;

                case TcPhase::Confirming:
                    // Our GUI confirm has been sent.  Evidence that it took
                    // effect = state leaving Tool.
                    if (!isToolState) {
                        tcTransition(TcPhase::Finishing,
                            std::format("confirm received by machine (state -> '{}')",
                                        state));
                    }
                    else if (sincePhaseMs > kRequestedTimeoutMs) {
                        // The confirm byte/command we sent was not acted on.
                        // This is the diagnostic for "Carvera ignores ~ / *"
                        // and similar -- we know the right answer must be a
                        // different command, OR the only path is the physical
                        // button.
                        tcAbort(
                            "Machine did not respond to the confirm signal. "
                            "The firmware may require a different confirm command, "
                            "or the only release path is the physical button on "
                            "the machine. Try the physical button; otherwise see "
                            "CarveraREADME.md and adjust the probe.");
                    }
                    break;

                case TcPhase::Finishing:
                    // Touch-off + return in progress.  Wait for sustained Idle.
                    if (isIdle) {
                        if (++tcFinishIdleFrames_ > kFinishIdleSettleFr) {
                            tcComplete();
                        }
                    }
                    else {
                        tcFinishIdleFrames_ = 0;
                    }
                    if (sincePhaseMs > kFinishingTimeoutMs) {
                        tcAbort(std::format(
                            "Touch-off + return did not complete within {} s.",
                            kFinishingTimeoutMs / 1000));
                    }
                    break;
            }

            // Emit lifecycle events on PUBLIC phase change (the internal
            // Requested/Seeking distinction is for our own bookkeeping; the
            // GUI only cares about the four public phases).
            emitPublicPhaseTransitions(state);
        }

        // -- State machine helpers --------------------------------------

        static bool isPhaseActive(TcPhase p) {
            return p != TcPhase::None && p != TcPhase::Aborted;
        }

        int msSincePhaseEntered() const {
            return (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - tcPhaseEnteredAt_).count();
        }

        // Single chokepoint for moving between phases.  Updates the entered-at
        // timestamp, resets the finishing-settle counter when leaving
        // Finishing, and logs a clean transition line for diagnostics.
        void tcTransition(TcPhase to, const std::string& reason) {
            if (tcPhase_ == to) { return; }
            const TcPhase from = tcPhase_;
            tcPhase_ = to;
            tcPhaseEnteredAt_ = Clock::now();
            if (to != TcPhase::Finishing) { tcFinishIdleFrames_ = 0; }
            dbg("[Air] tcPhase %s -> %s : %s (slot=%d)",
                tcPhaseName(from), tcPhaseName(to), reason.c_str(), tcSlot);
        }

        // Successful end of a tool change.  Sets the new loaded slot, fires
        // the Complete event with aborted=false.
        void tcComplete() {
            const int slot = tcSlot;
            loadedSlot.store(slot);
            pushLog(std::format("Tool change complete (T{} loaded and touched off).", slot));
            tcTransition(TcPhase::None, "settled in Idle after touch-off");
            // Public phase emission picks this up below.
        }

        // Failure end of a tool change.  Logs the reason loudly, stores it
        // for UI display, transitions to Aborted, and fires Complete with
        // aborted=true so the UI can show the failure prominently.
        void tcAbort(std::string reason) {
            pushLog(std::format("TOOL CHANGE ABORTED: {}", reason));
            tcAbortReason_ = std::move(reason);
            tcTransition(TcPhase::Aborted, "abort path");
        }

        // Emit Begin / Standby / Confirm / Complete dispatcher events when
        // the PUBLIC phase mapping crosses a boundary.  Decoupled from the
        // internal Requested/Seeking split so the UI sees a clean four-phase
        // lifecycle and only ever gets one event per real transition.
        void emitPublicPhaseTransitions(const std::string& state) {
            const TcPhase now = tcPhase_;
            if (now == lastEmittedPhase_) { return; }

            const TcPhase from = lastEmittedPhase_;
            lastEmittedPhase_ = now;

            dbg("[Air] tool-change public phase %s -> %s (state='%s', slot=%d)",
                tcPhaseName(from), tcPhaseName(now), state.c_str(), tcSlot);

            ToolChangePhase publicPhase = toolChangePhase();

            if (from == TcPhase::None && now == TcPhase::Requested) {
                emitToolChangeEvent(tcBeginDispatcher, &Air::toolChangeBeginEvent,
                                    ToolChangePhase::Seeking, false, "");
                return;
            }
            if (now == TcPhase::Standby) {
                emitToolChangeEvent(tcStandbyDispatcher, &Air::toolChangeStandbyEvent,
                                    ToolChangePhase::Standby, false, "");
                return;
            }
            if (now == TcPhase::Confirming || now == TcPhase::Finishing) {
                // Only emit Confirm event ONCE (on entry to Confirming or
                // Finishing -- whichever the public phase first sees).
                if (from != TcPhase::Confirming && from != TcPhase::Finishing) {
                    emitToolChangeEvent(tcConfirmDispatcher, &Air::toolChangeConfirmEvent,
                                        ToolChangePhase::Confirming, false, "");
                }
                return;
            }
            if (now == TcPhase::None || now == TcPhase::Aborted) {
                const bool aborted = (now == TcPhase::Aborted);
                emitToolChangeEvent(tcCompleteDispatcher, &Air::toolChangeCompleteEvent,
                                    ToolChangePhase::None, aborted,
                                    aborted ? tcAbortReason_ : std::string());
                return;
            }
            (void)publicPhase;
        }

        void emitToolChangeEvent(
            Rev::Core::Dispatcher<ToolChangeEvent>& dispatcher,
            void (Air::*key)(ToolChangeEvent&),
            ToolChangePhase phase,
            bool aborted,
            std::string reason
        ) {
            ToolChangeEvent e{ tcSlot, phase, aborted, std::move(reason) };
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
        // Standby/Confirm/Complete; pump() waits on it.  A program path
        // (autonomous) still requires whoever is driving the machine to
        // confirm at the prompt -- Air does not auto-confirm.
        void beginToolChangeOrchestration(int slot, bool resetStreamFlow = true) {
            sendLine("M5\n");
            sendLine("G4 P2\n");
            sendLine(std::format("M6 T{}\n", slot));
            tcSlot = slot;
            tcAbortReason_.clear();
            tcFinishIdleFrames_ = 0;
            tcTransition(TcPhase::Requested,
                std::format("M6 T{} sent; awaiting machine ack", slot));
            if (resetStreamFlow) { resetFlow(); }
            pushLog(std::format("Tool change requested: T{}. Awaiting machine ack.", slot));
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
                            // Skip a tool change to the slot that is already
                            // loaded.  The Carvera silently no-ops `M6 T<n>`
                            // when n is the current tool -- no "Please change"
                            // ack ever arrives, so our state machine would
                            // sit in Requested until the 5 s ack timeout
                            // aborts the program for no reason.  Drop the
                            // step and continue with the rest of the queue.
                            if (s.slot == loadedSlot.load()) {
                                dbg("[Air] exec: skipping tool change to T%d "
                                    "(already loaded)", s.slot);
                                pushLog(std::format(
                                    "Skipping tool change to T{}: already loaded.",
                                    s.slot));
                                steps_.pop_front();
                                continue;
                            }

                            // Drain everything already commanded first, so the
                            // machine is physically AT the pre-change point.
                            if (inFlight > 0) { break; }

                            haveReturn_ = haveLast_;
                            if (haveReturn_) {
                                retX_ = lastX_; retY_ = lastY_; retZ_ = lastZ_; retA_ = lastA_;
                                dbg("[Air] exec: Streaming -> ToolChanging "
                                    "(slot=%d, return XYZ=%.3f,%.3f,%.3f)",
                                    s.slot, retX_, retY_, retZ_);
                            }
                            else {
                                dbg("[Air] exec: Streaming -> ToolChanging "
                                    "(slot=%d, no return point recorded)", s.slot);
                            }
                            beginToolChangeOrchestration(s.slot, /*resetStreamFlow=*/true);
                            steps_.pop_front();
                            exec_ = Exec::ToolChanging;
                            return;   // hand off; nothing more streams this tick
                        }

                        client->send(gcodeForStep(s));
                        steps_.pop_front();
                        inFlight++;
                    }

                    if (steps_.empty() && inFlight == 0) {
                        dbg("[Air] exec: Streaming -> Idle (program complete)");
                        exec_ = Exec::Idle;
                        executing.store(false);
                        pushLog("Program complete.");
                    }
                    break;
                }

                case Exec::ToolChanging:
                    // Wait for the WHOLE change to FINISH SUCCESSFULLY before
                    // resuming the program.  If the change aborted, do not
                    // queue return moves: stop the program, surface the abort
                    // reason, and let the operator decide.
                    if (tcPhase_ == TcPhase::None) {
                        dbg("[Air] exec: ToolChanging -> Returning (change complete)");
                        sendReturnMoves();
                        exec_ = Exec::Returning;
                    }
                    else if (tcPhase_ == TcPhase::Aborted) {
                        dbg("[Air] exec: ToolChanging -> Idle (change aborted: %s)",
                            tcAbortReason_.c_str());
                        pushLog(std::format(
                            "Program stopped: tool change failed ({}).",
                            tcAbortReason_));
                        // We already hold queueMutex -- use the locked variant
                        // to avoid a re-entrant lock on std::mutex (which would
                        // throw std::system_error).
                        clearQueueLocked();
                    }
                    break;

                case Exec::Returning:
                    // Don't trust the buffer: watch the machine actually move
                    // back, then settle to Idle, before resuming the cut.
                    if (!returnMotionSeen_) {
                        if (machineState_ == "Run" || machineState_ == "Jog") {
                            returnMotionSeen_ = true;
                            dbg("[Air] exec: return motion observed (state='%s')",
                                machineState_.c_str());
                        }
                        else if (++returnWaitFrames_ > 600) {   // ~4 s: nothing to do / missed
                            returnMotionSeen_ = true;
                            dbg("[Air] exec: return motion timeout (state='%s')",
                                machineState_.c_str());
                        }
                    }
                    else if (inFlight == 0 &&
                             (machineState_ == "Idle" || machineState_ == "Alarm")) {
                        dbg("[Air] exec: Returning -> Streaming (settled, state='%s')",
                            machineState_.c_str());
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
