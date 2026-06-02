module;

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <functional>
#include <format>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cmath>

#include <managed.hpp>
#include <dbg.hpp>

export module Carvera.Gui.Interface;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;
import Rev.Element.Text;

import Rev.Client;
import Rev.Core.Animator;
import Rev.Element.Event.GestureTracker;

import Carvera.MachineLink;

import Cam.Gui.Theme;

export namespace Carvera::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    // ------------------------------------------------------------------
    // Styles
    // ------------------------------------------------------------------

    namespace Style {

        Rev::Element::Style Root = {
            .layout  = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size    = { .width = 100_pct, .height = 100_pct },
            .padding = { 16_px, 16_px, 16_px, 16_px }
        };

        Rev::Element::Style Section = {
            .layout  = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size    = { .width = 100_pct },
            .margin  = { .bottom = 16_px },
            .padding = { 12_px, 12_px, 12_px, 12_px },
            .border  = { .radius = 6_px }
        };

        Rev::Element::Style Row = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 100_pct, .height = 32_px },
            .margin = { .bottom = 6_px }
        };

        Rev::Element::Style Label      = { .text = { .size = 13_px } };
        Rev::Element::Style MutedLabel = { .text = { .size = 11_px } };
        Rev::Element::Style Title      = { .text = { .size = 16_px } };

        Rev::Element::Style Btn = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 80_px, .height = 30_px },
            .margin = { .right = 6_px },
            .border = { .radius = 5_px },
            .cursor = Cursor::Hand
        };

        Rev::Element::Style BtnHover = { .applies = { .hover = true, .focus = true } };
        Rev::Element::Style BtnPress = { .applies = { .press = true } };

        Rev::Element::Style JogBtn = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 56_px, .height = 40_px },
            .margin = { 3_px, 3_px, 3_px, 3_px },
            .border = { .radius = 5_px },
            .cursor = Cursor::Hand
        };

        Rev::Element::Style JogBtnActive = {
            .background = { .color = rgba(79, 99, 255, 0.45), .transition = 60_ms }
        };

        Rev::Element::Style JogBody    = { .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False }, .size = { .width = 100_pct } };
        Rev::Element::Style PosPanel   = { .layout = { Axis::Vertical,   Align::Start, Align::Start, Wrap::False }, .size = { .width = 120_px }, .margin = { .right = 12_px } };
        Rev::Element::Style PosRow     = { .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False }, .size = { .width = 100_pct, .height = 30_px } };
        Rev::Element::Style PosAxis    = { .size = { .width = 14_px }, .text = { .size = 10_px } };
        Rev::Element::Style PosValue   = { .size = { .width = Grow() }, .text = { .font = File("Rev/resources/Fonts/IBMPlexMono/IBMPlexMono-Light.ttf"), .size = 15_px } };
        Rev::Element::Style PosUnit    = { .margin = { .left = 3_px }, .text = { .size = 10_px } };
        Rev::Element::Style StepRow    = { .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False }, .size = { .width = 100_pct, .height = 24_px }, .margin = { .top = 8_px } };
        Rev::Element::Style StepLabel  = { .text = { .font = File("Rev/resources/Fonts/IBMPlexMono/IBMPlexMono-Light.ttf"), .size = 12_px } };
        Rev::Element::Style JogGrid    = { .layout = { Axis::Vertical,   Align::Start, Align::Start,  Wrap::False } };
        Rev::Element::Style JogGridRow = { .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False } };

        Rev::Element::Style ZCell = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size   = { .width = 56_px, .height = 40_px },
            .margin = { 3_px, 3_px, 3_px, 3_px }
        };

        Rev::Element::Style ZHalfBtn = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 100_pct, .height = Grow() },
            .border = { .radius = 3_px },
            .cursor = Cursor::Hand
        };

        Rev::Element::Style ZHalfGap = { .size = { .width = 100_pct, .height = 2_px } };

        Rev::Element::Style LogBox = {
            .layout  = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size    = { .width = 100_pct, .height = Grow() },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            .border  = { .radius = 6_px }
        };

        Rev::Element::Style LogLine = { .text = { .font = File("Rev/resources/Fonts/IBMPlexMono/IBMPlexMono-Regular.ttf"), .size = 11_px } };

        Rev::Element::Style StatusDot             = { .size = { .width = 8_px, .height = 8_px }, .margin = { .right = 8_px }, .border = { .radius = 4_px } };
        Rev::Element::Style StatusDotConnected    = { .background = { .color = rgba(34,  197, 94,  1.0) } };
        Rev::Element::Style StatusDotDisconnected = { .background = { .color = rgba(148, 163, 184, 1.0) } };

        Rev::Element::Style PanelBorderConnected  = { .border = { .color = rgba(34,  197, 94,  1.0), .width = 2_px, .radius = 6_px } };
        Rev::Element::Style PanelBorderAlarm      = { .border = { .color = rgba(239, 68,  68,  1.0), .width = 2_px, .radius = 6_px } };
        Rev::Element::Style PanelBorderToolChange = { .border = { .color = rgba(245, 158, 11,  1.0), .width = 2_px, .radius = 6_px } };
        Rev::Element::Style PanelBorderNone       = { .border = { .color = rgba(0,   0,   0,   0.0), .width = 0_px } };

        // Arm control: a prominent full-width toggle.  Disarmed it is a normal
        // button; armed it becomes a stark white-on-pastel-red banner.
        Rev::Element::Style ArmButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = Grow(), .height = 46_px },
            .margin = { .left = 3_px, .right = 3_px, .bottom = 6_px },
            .border = { .radius = 6_px },
            .cursor = Cursor::Hand
        };

        Rev::Element::Style ArmRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size   = { .width = 100_pct }
        };

        Rev::Element::Style ArmLabel = { .text = { .size = 15_px } };

        Rev::Element::Style ArmedBanner = {
            .background = { .color = rgba(255, 0, 0, 0.25) },
            .border     = { .color = rgba(255, 0, 0, 1.0), .width = 2_px, .radius = 6_px }
        };

        // START/STOP run button — full-width below the arm row.
        // "Stop" state reuses ArmedBanner (red tint); "Start" uses this green tint.
        Rev::Element::Style RunBanner = {
            .background = { .color = rgba(34, 197, 94, 0.2) },
            .border     = { .color = rgba(34, 197, 94, 1.0), .width = 2_px, .radius = 6_px }
        };
    }

    namespace Theme = Cam::Gui::Theme;

    enum class CarveraCommand { Connect, Disconnect, Unlock, Reset };

    // ------------------------------------------------------------------
    // Interface — Carvera Air control panel.
    //
    // Position display strategy:
    //
    //   - Telemetry is the single source of truth.  We chain-poll the
    //     machine with "?", sending the next request only after the
    //     previous response is parsed.  This guarantees we are always
    //     reading fresh data, never queued-up stale frames.
    //
    //   - We track an "intent" (intentX/Y/Z/A) that accumulates every
    //     commanded delta.  The display smoothly chases the intent at
    //     a fixed rate, providing instant visual feedback for jogs.
    //
    //   - Whenever telemetry arrives, the display anchor snaps to the
    //     confirmed position; the display then continues chasing the
    //     intent from there.  This means the display always converges
    //     to ground truth and never displays "phantom" motion after
    //     the machine has physically stopped.
    //
    //   - The intent is reset to telemetry truth whenever the machine
    //     reports as Idle, so spamming jogs at boundaries doesn't
    //     accumulate fictional offsets.
    // ------------------------------------------------------------------

    struct Interface : public Box {

        // -- Connection ----------------------------------------------

        Rev::Client* client     = nullptr;
        std::string  targetHost = "192.168.1.104";
        int          targetPort = 2222;

        // -- Step presets --------------------------------------------

        static constexpr float kStepPresets[] = { 0.01f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
        static constexpr int   kStepCount     = 6;

        int   stepIndex    = 3;
        float jogStepMm    = 1.0f;
        float jogStepDeg   = 5.0f;
        int   jogFeedRate  = 1000;
        int   jogFeedRateA = 3000;

        // -- Gestures ------------------------------------------------

        GestureTracker<CarveraCommand> gestures = {{
            { "cn",     CarveraCommand::Connect    },
            { "ctrl+n", CarveraCommand::Connect    },
            { "dc",     CarveraCommand::Disconnect },
            { "ctrl+d", CarveraCommand::Disconnect },
            { "un",     CarveraCommand::Unlock     },
            { "ctrl+u", CarveraCommand::Unlock     },
            { "rs",     CarveraCommand::Reset      },
            { "ctrl+r", CarveraCommand::Reset      },
        }};

        // -- Animators -----------------------------------------------

        // Watchdog: re-issues "?" if a chain-polled response hasn't
        // arrived in StatusTimeoutMs (network hiccup, packet loss).
        Rev::Core::Animator watchdog { 500 };

        // Display chase loop — 140 fps.  Always running while connected.
        Rev::Core::Animator displayLoop { 7 };

        // -- Chain polling state -------------------------------------

        using Clock = std::chrono::steady_clock;

        bool              statusInFlight = false;
        Clock::time_point statusSentAt;
        static constexpr int StatusTimeoutMs = 750;

        // -- Thread-safe handoff (worker -> main) --------------------

        std::mutex              logMutex;
        std::deque<std::string> pendingLog;

        // Latest pending telemetry — overwritten each time a frame arrives.
        // We deliberately only keep the most recent; older frames are noise.
        std::string pendingState;
        float pendingPosX = 0, pendingPosY = 0, pendingPosZ = 0, pendingPosA = 0;
        bool  pendingPosValid     = false;
        bool  pendingResponseSeen = false;  // signal that a "?" reply was processed

        // -- Confirmed machine state (main thread) -------------------

        std::string machineState;
        float confX = 0, confY = 0, confZ = 0, confA = 0;
        bool  confValid = false;

        // -- Intent — sum of commanded deltas ------------------------

        float intentX = 0, intentY = 0, intentZ = 0, intentA = 0;

        // -- Display position — chases intent, anchored to truth -----

        float dispX = 0, dispY = 0, dispZ = 0, dispA = 0;
        bool  dispReady = false;

        Clock::time_point lastFrameTime = Clock::now();

        // Display chase rate: how quickly the display catches up to intent.
        // Linear approach at this many mm/ms or deg/ms.  Tuned to feel
        // immediate without being jumpy.  At F=1000 the machine moves at
        // ~16.67 mm/s = 0.01667 mm/ms; we chase at ~0.04 mm/ms so we lead
        // slightly, then telemetry pulls us back to truth continuously.
        static constexpr float ChaseSpeedLinear  = 0.040f;  // mm/ms
        static constexpr float ChaseSpeedAngular = 0.120f;  // deg/ms

        static constexpr size_t MaxLogLines = 64;

        // -- UI nodes ------------------------------------------------

        Box*  connectionSection = nullptr;
        Box*  statusDot         = nullptr;
        Text* statusLabel       = nullptr;
        Box*  connectBtn        = nullptr;
        Text* connectBtnLabel   = nullptr;

        Box*  armBtn   = nullptr;
        Text* armLabel = nullptr;

        Box*  spindleBtn   = nullptr;
        Text* spindleLabel = nullptr;

        Box*  runBtn   = nullptr;
        Text* runLabel = nullptr;

        Box*  setOriginBtn = nullptr;

        Text* posXText = nullptr, *posYText = nullptr;
        Text* posZText = nullptr, *posAText = nullptr;
        Text* stepText = nullptr;

        Box*  logBox  = nullptr;
        Text* logText = nullptr;

        Box* btnPX    = nullptr; Box* btnNX    = nullptr;
        Box* btnPY    = nullptr; Box* btnNY    = nullptr;
        Box* btnPZ    = nullptr; Box* btnNZ    = nullptr;
        Box* btnAP    = nullptr; Box* btnAM    = nullptr;
        Box* btnStepP = nullptr; Box* btnStepM = nullptr;

        Box* activeJogBtn = nullptr;

        // -- Helpers -------------------------------------------------

        Box* makeBtn(Element* parent, const std::string& label, Rev::Element::Style& s) {
            Box* btn = new Box(parent, Theme::withButton({ &s, &Style::BtnHover, &Style::BtnPress }), "Btn");
            new Text(btn, label, Theme::withText({ &Style::Label }));
            return btn;
        }

        Box* makeJogBtn(Element* parent, const std::string& label) {
            Box* btn = new Box(parent, Theme::withButton({ &Style::JogBtn, &Style::BtnHover, &Style::BtnPress }), "JogBtn");
            new Text(btn, label, Theme::withText({ &Style::Label }));
            return btn;
        }

        // -- Construction --------------------------------------------

        Interface(Element* parent) : Box(parent, {}, "CarveraInterface") {

            Theme::applyMode(Theme::Mode::Dark);
            this->styles.add(&Style::Root);
            this->styles.add(&Theme::Styles::Background);
            this->tabStop = true;

            buildConnectionSection();
            buildArmSection();
            buildJogSection();
            buildLogSection();

            // Watchdog — refires "?" if we never got the reply.
            watchdog.onFrame([this](Rev::Core::AnimationEvent&) {
                if (!client || !client->isConnected.load()) return;
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    Clock::now() - statusSentAt).count();
                if (statusInFlight && elapsed > StatusTimeoutMs) {
                    // The previous request seems lost — issue a fresh one.
                    statusInFlight = false;
                    sendStatus();
                }
                if (!statusInFlight) sendStatus();
                if (shared && shared->event) refresh(*shared->event);
            });

            // Display chase loop — 140 fps, anchors to truth + chases intent.
            displayLoop.onFrame([this](Rev::Core::AnimationEvent& frame) {
                tickDisplay();
                // Drive flow-controlled program streaming on the main thread.
                Carvera::MachineLink::instance().pump();
                if (shared && shared->event) refresh(*shared->event);
            });

            gestures.onGesture = [this](CarveraCommand cmd, Event& e) {
                switch (cmd) {
                    case CarveraCommand::Connect:    onConnectClick(e);   break;
                    case CarveraCommand::Disconnect: onDisconnectClick(e); break;
                    case CarveraCommand::Unlock:     unlock(e);           break;
                    case CarveraCommand::Reset:      sendLine("reset\n"); break;
                }
            };
        }

        ~Interface() {
            Carvera::MachineLink::instance().setClient(nullptr);
            if (client) { delete client; client = nullptr; }
        }

        // -- UI construction -----------------------------------------

        void buildConnectionSection() {

            connectionSection = new Box(this, Theme::withPanel({ &Style::Section }), "ConnectionSection");
            Box* s = connectionSection;

            Box* titleRow = new Box(s, { &Style::Row }, "TitleRow");
            statusDot = new Box(titleRow, { &Style::StatusDot, &Style::StatusDotDisconnected }, "StatusDot");
            new Text(titleRow, "Carvera Air", Theme::withText({ &Style::Title }));

            Box* statusRow = new Box(s, { &Style::Row }, "StatusRow");
            statusLabel = new Text(statusRow, std::format("{}:{}", targetHost, targetPort), Theme::withMutedText({ &Style::MutedLabel }));

            Box* btnRow = new Box(s, { &Style::Row }, "BtnRow");
            connectBtn = makeBtn(btnRow, "Connect", Style::Btn);
            connectBtn->onClick([this](Event& e) { onConnectClick(e);    e.propagate = false; });
            connectBtnLabel = static_cast<Text*>(connectBtn->children.front());
            makeBtn(btnRow, "Disconnect", Style::Btn)->onClick([this](Event& e) { onDisconnectClick(e); e.propagate = false; });

            Box* ctrlRow = new Box(s, { &Style::Row }, "CtrlRow");
            makeBtn(ctrlRow, "Unlock", Style::Btn)->onClick([this](Event& e) { unlock(e);           e.propagate = false; });
            makeBtn(ctrlRow, "Reset",  Style::Btn)->onClick([this](Event& e) { sendLine("reset\n"); e.propagate = false; });
        }

        void buildArmSection() {

            Box* section = new Box(this, Theme::withPanel({ &Style::Section }), "ArmSection");

            // Both arm toggles live in one host, side by side.
            Box* row = new Box(section, { &Style::ArmRow }, "ArmRow");

            armBtn = new Box(
                row,
                Theme::withButton({ &Style::ArmButton, &Style::BtnHover, &Style::BtnPress }),
                "ArmButton"
            );
            armLabel = new Text(armBtn, "ARM", Theme::withText({ &Style::ArmLabel }));
            armBtn->onClick([this](Event& e) { toggleArm(e); e.propagate = false; });

            spindleBtn = new Box(
                row,
                Theme::withButton({ &Style::ArmButton, &Style::BtnHover, &Style::BtnPress }),
                "SpindleArmButton"
            );
            spindleLabel = new Text(spindleBtn, "SPINDLE ARM", Theme::withText({ &Style::ArmLabel }));
            spindleBtn->onClick([this](Event& e) { toggleSpindleArm(e); e.propagate = false; });

            // Full-width START / STOP button directly below the arm row.
            // Width = Grow() in a Vertical parent → fills the section width.
            runBtn = new Box(
                section,
                Theme::withButton({ &Style::ArmButton, &Style::BtnHover, &Style::BtnPress }),
                "RunButton"
            );
            runLabel = new Text(runBtn, "START", Theme::withText({ &Style::ArmLabel }));
            runBtn->onClick([this](Event& e) { onRunClick(e); e.propagate = false; });
        }

        void onRunClick(Event& e) {

            auto& link = Carvera::MachineLink::instance();

            if (link.isExecuting()) {
                link.stop();
                pushLog("Program stopped.");
            }
            else if (!link.isArmed()) {
                pushLog("Arm the machine before starting execution.");
            }
            else if (link.onStartRequested) {
                link.onStartRequested();
                pushLog("Execution started.");
            }
            else {
                pushLog("No execute mode active in the CAM view.");
            }

            refresh(e);
        }

        // Spindle ARM is a gate, not a manual on/off: armed motion can dry-run a
        // toolpath; the spindle only spins during execution if it is also armed.
        void toggleSpindleArm(Event& e) {

            auto& link = Carvera::MachineLink::instance();

            link.setSpindleArmed(!link.isSpindleArmed());
            pushLog(link.isSpindleArmed() ? "SPINDLE ARMED." : "Spindle disarmed.");

            refresh(e);
        }

        void toggleArm(Event& e) {

            auto& link = Carvera::MachineLink::instance();

            if (link.isArmed()) {
                link.disarm();
                pushLog("DISARMED.");
            }
            else if (link.setArmed(true)) {
                link.beep();
                pushLog("ARMED - execution enabled.");
            }
            else {
                pushLog("Connect to the machine before arming.");
            }

            refresh(e);
        }

        void buildJogSection() {

            Box* section = new Box(this, Theme::withPanel({ &Style::Section }), "JogSection");
            new Text(section, "Jog", Theme::withText({ &Style::Label }));
            Box* body = new Box(section, { &Style::JogBody }, "JogBody");

            // -- Position panel (left) --------------------------------

            Box* posPanel = new Box(body, { &Style::PosPanel }, "PosPanel");

            auto makeAxisRow = [&](const std::string& axis, Text*& out, const std::string& unit) {
                Box* row = new Box(posPanel, { &Style::PosRow }, "PosRow");
                new Text(row, axis, Theme::withMutedText({ &Style::PosAxis }));
                out = new Text(row, "---", Theme::withText({ &Style::PosValue }));
                new Text(row, unit, Theme::withMutedText({ &Style::PosUnit }));
            };

            makeAxisRow("X", posXText, "mm");
            makeAxisRow("Y", posYText, "mm");
            makeAxisRow("Z", posZText, "mm");
            makeAxisRow("A", posAText, "deg");

            Box* stepRow = new Box(posPanel, { &Style::StepRow }, "StepRow");
            new Text(stepRow, "step", Theme::withMutedText({ &Style::PosAxis }));
            stepText = new Text(stepRow, formatStep(), Theme::withText({ &Style::StepLabel }));

            // -- Jog grid (right) -------------------------------------
            //
            //   [A-]    [+Y]    [A+]
            //   [-X]   [+Z/-Z]  [+X]
            //  [step-]  [-Y]  [step+]

            Box* grid = new Box(body, { &Style::JogGrid }, "JogGrid");

            // Row 0: A- | +Y | A+
            Box* r0 = new Box(grid, { &Style::JogGridRow }, "R0");
            btnAM = makeJogBtn(r0, "A-"); btnAM->onClick([this](Event& e) { jogA(-jogStepDeg); e.propagate = false; });
            btnPY = makeJogBtn(r0, "+Y"); btnPY->onClick([this](Event& e) { jog(0,+jogStepMm,0); e.propagate = false; });
            btnAP = makeJogBtn(r0, "A+"); btnAP->onClick([this](Event& e) { jogA(+jogStepDeg); e.propagate = false; });

            // Row 1: -X | [+Z/-Z] | +X
            Box* r1 = new Box(grid, { &Style::JogGridRow }, "R1");
            btnNX = makeJogBtn(r1, "-X"); btnNX->onClick([this](Event& e) { jog(-jogStepMm,0,0); e.propagate = false; });

            Box* zCell = new Box(r1, { &Style::ZCell }, "ZCell");
            btnPZ = new Box(zCell, Theme::withButton({ &Style::ZHalfBtn, &Style::BtnHover, &Style::BtnPress }), "ZP");
            new Text(btnPZ, "+Z", Theme::withText({ &Style::MutedLabel }));
            btnPZ->onClick([this](Event& e) { jog(0,0,+jogStepMm); e.propagate = false; });
            new Box(zCell, { &Style::ZHalfGap }, "ZG");
            btnNZ = new Box(zCell, Theme::withButton({ &Style::ZHalfBtn, &Style::BtnHover, &Style::BtnPress }), "ZN");
            new Text(btnNZ, "-Z", Theme::withText({ &Style::MutedLabel }));
            btnNZ->onClick([this](Event& e) { jog(0,0,-jogStepMm); e.propagate = false; });

            btnPX = makeJogBtn(r1, "+X"); btnPX->onClick([this](Event& e) { jog(+jogStepMm,0,0); e.propagate = false; });

            // Row 2: step- | -Y | step+
            Box* r2 = new Box(grid, { &Style::JogGridRow }, "R2");
            btnStepM = makeJogBtn(r2, "-");  btnStepM->onClick([this](Event& e) { adjustStep(-1); e.propagate = false; });
            btnNY    = makeJogBtn(r2, "-Y"); btnNY->onClick([this](Event& e) { jog(0,-jogStepMm,0); e.propagate = false; });
            btnStepP = makeJogBtn(r2, "+");  btnStepP->onClick([this](Event& e) { adjustStep(+1); e.propagate = false; });

            // Set the work origin (G54 zero) at the current position — used to
            // mark the begin-work point on the stock surface.
            Box* originRow = new Box(section, { &Style::Row }, "OriginRow");
            setOriginBtn = makeBtn(originRow, "Set Origin", Style::Btn);
            setOriginBtn->style->size = { .width = 120_px, .height = 30_px };
            setOriginBtn->onClick([this](Event& e) { setWorkOrigin(e); e.propagate = false; });
        }

        void setWorkOrigin(Event& e) {

            if (!client || !client->isConnected.load() || !confValid) {
                pushLog("Connect and wait for position before setting origin.");
                return;
            }

            // Zero all four axes in the active work coordinate system.
            // Including A0 is essential: it makes the machine's WCS A=0 equal
            // the current physical A position, so absolute A commands in the
            // streamed G-code are always relative to this reference — the same
            // reference the IK solver uses for rotaryAngle=0.
            sendLine("G10 L20 P1 X0 Y0 Z0 A0\n");

            // Record all four machine positions as the display reference.
            // The CAM view subtracts these when mapping live MPos back into
            // CAD/work space (XYZ for tool position, A for part rotation).
            Carvera::MachineLink::instance().captureMachineOrigin(
                confX, confY, confZ, confA
            );

            pushLog("Work origin set at current position (X Y Z A zeroed).");
            refresh(e);
        }

        void buildLogSection() {
            logBox  = new Box(this, Theme::withPanel({ &Style::LogBox }), "LogBox");
            logText = new Text(logBox, "(no messages yet)", Theme::withMutedText({ &Style::LogLine }));
        }

        // -- Networking ----------------------------------------------

        void onConnectClick(Event& e) {

            if (client) return;
            client = new Rev::Client();

            client->onConnecting([this](Rev::Client::ConnectingEvent& e) {
                pushLog(std::format("Connecting to {}...", e.address));
            });

            client->onConnect([this](Rev::Client::ConnectEvent& e) {
                pushLog(std::format("Connected to {}", e.address));
                Carvera::MachineLink::instance().setClient(client);
                resetTelemetryState();
                sendStatus();           // kick off the chain
                watchdog.play();
                displayLoop.play();
            });

            client->onDisconnect([this](Rev::Client::DisconnectEvent& e) {
                watchdog.stop();
                displayLoop.stop();
                Carvera::MachineLink::instance().setClient(nullptr);
                pushLog("Connection closed by remote.");
                std::lock_guard lock(logMutex);
                pendingState = "";
                pendingPosValid     = false;
                pendingResponseSeen = false;
            });

            client->onData([this](Rev::Client::DataEvent& e) { onData(e); });

            client->onError([this](Rev::Client::ErrorEvent& e) {
                watchdog.stop();
                displayLoop.stop();
                Carvera::MachineLink::instance().setClient(nullptr);
                pushLog(std::format("Error: {}", e.reason));
            });

            client->connect(targetHost, targetPort);
        }

        void onDisconnectClick(Event& e) {
            if (!client) return;
            watchdog.stop();
            displayLoop.stop();
            Carvera::MachineLink::instance().setClient(nullptr);
            delete client; client = nullptr;
            pushLog("Disconnected.");
            resetTelemetryState();
            refresh(e);
        }

        void resetTelemetryState() {
            std::lock_guard lock(logMutex);
            pendingState = "";
            pendingPosValid     = false;
            pendingResponseSeen = false;
            confValid      = false;
            dispReady      = false;
            machineState.clear();
            lastMachineState_.clear();
            statusInFlight = false;
        }

        // Worker-thread callback.  Splits a TCP chunk on newlines and
        // processes each complete line.  Critically, when multiple "<...>"
        // status frames are present we keep only the freshest one — older
        // frames are intentionally discarded.
        std::string rxBuffer_;
        void onData(Rev::Client::DataEvent& e) {

            rxBuffer_.append(e.data.begin(), e.data.end());

            size_t start = 0;
            while (true) {
                size_t nl = rxBuffer_.find_first_of("\r\n", start);
                if (nl == std::string::npos) break;
                if (nl > start) processLine(rxBuffer_.substr(start, nl - start));
                start = nl + 1;
            }

            if (start > 0) rxBuffer_.erase(0, start);
        }

        void processLine(const std::string& msg) {

            // Status frame — parse silently, never log.  Always overwrite
            // pending fields so the *latest* frame wins.
            if (msg.size() > 1 && msg.front() == '<') {

                size_t delim  = msg.find_first_of(",|>", 1);
                std::string state = (delim != std::string::npos) ? msg.substr(1, delim - 1) : "";

                float x = 0, y = 0, z = 0, a = 0;
                size_t mp = msg.find("MPos:");
                bool posOk = (mp != std::string::npos) &&
                             sscanf(msg.c_str() + mp + 5, "%f,%f,%f,%f", &x, &y, &z, &a) >= 3;

                std::lock_guard lock(logMutex);
                pendingState = state;
                if (posOk) {
                    pendingPosX = x; pendingPosY = y;
                    pendingPosZ = z; pendingPosA = a;
                    pendingPosValid = true;
                }
                pendingResponseSeen = true;
                return;
            }

            if (msg == "ok" || msg.starts_with("ok - ignore:")) {
                Carvera::MachineLink::instance().notifyOk();
                return;
            }

            pushLog(std::format("< {}", msg));
        }

        // -- Machine commands ----------------------------------------

        void sendLine(const std::string& line) {
            if (!client || !client->isConnected.load()) { pushLog("Not connected."); return; }
            client->send(line);
            std::string d = line;
            while (!d.empty() && (d.back() == '\n' || d.back() == '\r')) d.pop_back();
            pushLog(std::format("> {}", d));
        }

        void jog(float dx, float dy, float dz) {
            if (!client || !client->isConnected.load() || !confValid) return;

            // Update intent immediately — the display will chase this.
            intentX += dx; intentY += dy; intentZ += dz;

            std::string cmd = "$J=G91";
            if (dx != 0.0f) cmd += std::format(" X{:.3f}", dx);
            if (dy != 0.0f) cmd += std::format(" Y{:.3f}", dy);
            if (dz != 0.0f) cmd += std::format(" Z{:.3f}", dz);
            cmd += std::format(" F{}\n", jogFeedRate);
            client->send(cmd);
        }

        void jogA(float degrees) {
            if (!client || !client->isConnected.load() || !confValid) return;
            intentA += degrees;
            client->send(std::format("$J=G91 A{:.3f} F{}\n", degrees, jogFeedRateA));
        }

        // Chain-polling primitive — issues "?" exactly once.
        // No-op if a previous request hasn't been answered.
        void sendStatus() {
            if (!client || !client->isConnected.load()) return;
            if (statusInFlight) return;
            client->send("?");
            statusSentAt   = Clock::now();
            statusInFlight = true;
        }

        void unlock(Event& e) {
            if (!client || !client->isConnected.load()) { pushLog("Not connected."); return; }
            client->send("$X\n");
            pushLog("> $X  (unlock)");
            refresh(e);
        }

        // -- Step size -----------------------------------------------

        void adjustStep(int delta) {
            stepIndex = std::clamp(stepIndex + delta, 0, kStepCount - 1);
            jogStepMm = kStepPresets[stepIndex];
            if (stepText) stepText->content = formatStep();
        }

        std::string formatStep() const {
            return std::format("{:.4g} mm", kStepPresets[stepIndex]);
        }

        // -- Log (thread-safe) ---------------------------------------

        void pushLog(std::string msg) {
            std::lock_guard lock(logMutex);
            pendingLog.push_back(std::move(msg));
        }

        // Drain worker-thread state into main-thread state.
        // Returns true if a fresh telemetry frame was committed.
        bool drainTelemetry() {

            bool        logDirty  = false;
            bool        posDirty  = false;
            bool        replySeen = false;
            float       nx = 0, ny = 0, nz = 0, na = 0;
            std::string nstate;

            {
                std::lock_guard lock(logMutex);

                while (!pendingLog.empty()) {
                    logLines_.push_back(std::move(pendingLog.front()));
                    pendingLog.pop_front();
                    logDirty = true;
                }

                nstate    = pendingState;
                replySeen = pendingResponseSeen;
                if (pendingPosValid) {
                    nx = pendingPosX; ny = pendingPosY;
                    nz = pendingPosZ; na = pendingPosA;
                    pendingPosValid = false;
                    posDirty = true;
                }
                pendingResponseSeen = false;
            }

            while (logLines_.size() > MaxLogLines) logLines_.pop_front();

            if (logDirty && logText) {
                std::string combined;
                for (auto& l : logLines_) { combined += l; combined += '\n'; }
                logText->content = combined;
            }

            machineState = nstate;

            if (replySeen) {
                // The chain-poll cycle completes here: response was received,
                // we may issue the next "?".
                statusInFlight = false;
            }

            if (posDirty) {
                confX = nx; confY = ny; confZ = nz; confA = na;
                confValid = true;
            }

            return posDirty;
        }

        // Display tick — runs at 140 Hz.  Implements three rules:
        //
        //   1. Drain any pending telemetry; if a fresh frame arrived,
        //      anchor display to that confirmed position.
        //   2. If the machine reports Idle, reset intent to truth
        //      (the machine has finished all commands).
        //   3. Step the display toward intent at the chase rate.
        void tickDisplay() {

            auto now = Clock::now();
            float dtMs = std::chrono::duration<float, std::milli>(now - lastFrameTime).count();
            lastFrameTime = now;

            bool freshFrame = drainTelemetry();

            // Issue the next status request — chain-polled.
            if (!statusInFlight) sendStatus();

            if (!confValid) {
                dispReady = false;
                return;
            }

            if (!dispReady) {
                // First telemetry — initialize all positions.
                dispX = intentX = confX;
                dispY = intentY = confY;
                dispZ = intentZ = confZ;
                dispA = intentA = confA;
                dispReady = true;
                updatePosDisplay();
                return;
            }

            if (freshFrame) {
                // Re-anchor display to confirmed truth, preserving any
                // user intent above that.
                dispX = confX;
                dispY = confY;
                dispZ = confZ;
                dispA = confA;

                // Snap intent to truth only on the TRANSITION into an idle-like
                // state, not on every frame that happens to be idle.
                //
                // Level-triggering (the old behaviour) re-injects noisy conf
                // into intent on every telemetry tick while the machine is
                // stationary, causing ~±1 mm continuous display jitter and
                // making the part-rotation actor shiver in the 3D view.
                // Edge-triggering fires exactly once per stop, letting the
                // display chase smoothly to the confirmed stop position.
                const bool nowQuiet = (machineState    == "Idle" || machineState    == "Alarm");
                const bool wasQuiet = (lastMachineState_ == "Idle" || lastMachineState_ == "Alarm");

                if (nowQuiet && !wasQuiet) {
                    intentX = confX; intentY = confY;
                    intentZ = confZ; intentA = confA;
                }
            }

            // Track previous state for the edge-trigger above.
            if (!machineState.empty()) {
                lastMachineState_ = machineState;
            }

            // Chase intent at a bounded rate per axis.
            stepToward(dispX, intentX, ChaseSpeedLinear  * dtMs);
            stepToward(dispY, intentY, ChaseSpeedLinear  * dtMs);
            stepToward(dispZ, intentZ, ChaseSpeedLinear  * dtMs);
            stepToward(dispA, intentA, ChaseSpeedAngular * dtMs);

            updatePosDisplay();

            // Mirror the live position to the CAM view (execute-mode preview).
            if (dispReady) {
                Carvera::MachineLink::instance().setTelemetry(dispX, dispY, dispZ, dispA);
            }
        }

        static void stepToward(float& cur, float target, float maxStep) {
            float diff = target - cur;
            if (std::abs(diff) <= maxStep) { cur = target; return; }
            cur += (diff > 0 ? maxStep : -maxStep);
        }

        void updatePosDisplay() {
            if (!dispReady) return;
            if (posXText) posXText->content = std::format("{:.3f}", dispX);
            if (posYText) posYText->content = std::format("{:.3f}", dispY);
            if (posZText) posZText->content = std::format("{:.3f}", dispZ);
            if (posAText) posAText->content = std::format("{:.3f}", dispA);
        }

        // -- Keyboard ------------------------------------------------

        void keyDown(Event& e) override {

            auto& arrows = e.keyboard.arrows;
            bool  shift  = e.keyboard.shift;
            bool  ctrl   = e.keyboard.ctrl;
            bool  alt    = e.keyboard.alt;

            bool handled = false;

            if (alt && (arrows.up || arrows.down)) {
                float step = jogStepDeg;
                if (ctrl)  step =  0.1f;
                if (shift) step = 45.0f;
                if (arrows.up)   { activeJogBtn = btnAP; jogA(+step); handled = true; }
                if (arrows.down) { activeJogBtn = btnAM; jogA(-step); handled = true; }

            } else if (!alt) {
                float step = jogStepMm;
                if (shift) step *= 10.0f;
                if (ctrl)  step *=  0.1f;

                if      (shift && arrows.up)   { activeJogBtn = btnPZ; jog(0, 0, +step); handled = true; }
                else if (shift && arrows.down) { activeJogBtn = btnNZ; jog(0, 0, -step); handled = true; }
                else if (arrows.left)          { activeJogBtn = btnNX; jog(-step, 0, 0); handled = true; }
                else if (arrows.right)         { activeJogBtn = btnPX; jog(+step, 0, 0); handled = true; }
                else if (arrows.up)            { activeJogBtn = btnPY; jog(0, +step, 0); handled = true; }
                else if (arrows.down)          { activeJogBtn = btnNY; jog(0, -step, 0); handled = true; }
            }

            if (handled) { e.propagate = false; refresh(e); return; }

            if (gestures.track(e)) { e.propagate = false; return; }

            Box::keyDown(e);
        }

        void keyUp(Event& e) override {
            activeJogBtn = nullptr;
            refresh(e);
            Box::keyUp(e);
        }

        // -- Compute / render ----------------------------------------

        void computeChildren(Event& e) override {

            bool isConnected = client && client->isConnected.load();

            if (!isConnected || !dispReady) {
                if (posXText) posXText->content = "---";
                if (posYText) posYText->content = "---";
                if (posZText) posZText->content = "---";
                if (posAText) posAText->content = "---";
            }

            if (statusDot) {
                statusDot->styles.remove(&Style::StatusDotConnected);
                statusDot->styles.remove(&Style::StatusDotDisconnected);
                statusDot->styles.add(isConnected ? &Style::StatusDotConnected : &Style::StatusDotDisconnected);
            }

            if (connectionSection) {
                connectionSection->styles.remove(&Style::PanelBorderConnected);
                connectionSection->styles.remove(&Style::PanelBorderAlarm);
                connectionSection->styles.remove(&Style::PanelBorderToolChange);
                connectionSection->styles.remove(&Style::PanelBorderNone);

                if      (!isConnected)             connectionSection->styles.add(&Style::PanelBorderNone);
                else if (machineState == "Alarm")  connectionSection->styles.add(&Style::PanelBorderAlarm);
                else if (machineState == "Tool")   connectionSection->styles.add(&Style::PanelBorderToolChange);
                else                               connectionSection->styles.add(&Style::PanelBorderConnected);
            }

            Box* allBtns[] = { btnPX, btnNX, btnPY, btnNY, btnPZ, btnNZ, btnAP, btnAM };
            for (Box* btn : allBtns) if (btn) btn->styles.remove(&Style::JogBtnActive);
            if (activeJogBtn) activeJogBtn->styles.add(&Style::JogBtnActive);

            if (armBtn && armLabel) {

                const bool armed = Carvera::MachineLink::instance().isArmed();

                armBtn->styles.remove(&Style::ArmedBanner);

                if (armed) {
                    armBtn->styles.add(&Style::ArmedBanner);
                    armLabel->content = "ARMED";
                }
                else {
                    armLabel->content = "ARM";
                }
            }

            if (spindleBtn && spindleLabel) {

                const bool spindleArmed = Carvera::MachineLink::instance().isSpindleArmed();

                spindleBtn->styles.remove(&Style::ArmedBanner);

                if (spindleArmed) {
                    spindleBtn->styles.add(&Style::ArmedBanner);
                    spindleLabel->content = "SPINDLE ARMED";
                }
                else {
                    spindleLabel->content = "SPINDLE ARM";
                }
            }

            if (runBtn && runLabel) {

                const bool executing = Carvera::MachineLink::instance().isExecuting();
                const bool armed     = Carvera::MachineLink::instance().isArmed();

                runBtn->styles.remove(&Style::ArmedBanner);
                runBtn->styles.remove(&Style::RunBanner);

                if (executing) {
                    runBtn->styles.add(&Style::ArmedBanner);    // red — stop is urgent
                    runLabel->content = "STOP";
                }
                else if (armed) {
                    runBtn->styles.add(&Style::RunBanner);      // green — ready to go
                    runLabel->content = "START";
                }
                else {
                    runLabel->content = "START";
                }
            }

            Box::computeChildren(e);
        }

    private:
        std::deque<std::string> logLines_;
        std::string lastMachineState_;  // previous frame's state — for edge-trigger
    };
}
