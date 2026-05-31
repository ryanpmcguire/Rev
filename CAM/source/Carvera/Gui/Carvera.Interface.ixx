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
    }

    namespace Theme = Cam::Gui::Theme;

    enum class CarveraCommand { Connect, Disconnect, Unlock, Reset };

    // ------------------------------------------------------------------
    // Interface
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

        Rev::Core::Animator keepalive   { 200 };  // 5 Hz status poll
        Rev::Core::Animator interpolator {   7 };  // 140 fps dead reckoning

        // -- Thread-safe state (worker -> main) ----------------------

        std::mutex              logMutex;
        std::deque<std::string> pendingLog;
        std::string             pendingState;
        float  pendingPosX = 0, pendingPosY = 0, pendingPosZ = 0, pendingPosA = 0;
        bool   pendingPosValid = false;

        // -- Machine truth (main thread) -----------------------------

        std::string machineState;
        float posX = 0, posY = 0, posZ = 0, posA = 0;

        // -- Dead-reckoning command queue ----------------------------
        //
        // Each entry describes one jog command we have issued.
        // The interpolator walks this queue to produce a precise
        // position estimate at any point in time, independent of
        // how often telemetry arrives.
        //
        // Analogy: if you told the machine "move to X=5 at F=1000,
        // starting now", you know exactly where it is 30ms later
        // (X = 0.5) without asking it — the physics are deterministic.

        using Clock = std::chrono::steady_clock;

        struct JogCmd {
            float sx, sy, sz, sa;        // position when command starts executing
            float dx, dy, dz, da;        // commanded delta
            Clock::time_point t0;        // expected start time
            float durMs;                 // expected execution time (ms)

            float ex() const { return sx + dx; }
            float ey() const { return sy + dy; }
            float ez() const { return sz + dz; }
            float ea() const { return sa + da; }
        };

        std::deque<JogCmd> jogQueue;

        // Interpolated display position (what the text shows).
        bool  dispReady = false;
        float dispX = 0, dispY = 0, dispZ = 0, dispA = 0;

        static constexpr size_t MaxLogLines = 64;
        static constexpr size_t MaxQueueLen = 128; // safety cap

        // -- UI nodes ------------------------------------------------

        Box*  connectionSection = nullptr;
        Box*  statusDot         = nullptr;
        Text* statusLabel       = nullptr;
        Box*  connectBtn        = nullptr;
        Text* connectBtnLabel   = nullptr;

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
            buildJogSection();
            buildLogSection();

            // Keepalive: poll machine state + drive UI refresh.
            keepalive.onFrame([this](Rev::Core::AnimationEvent&) {
                sendStatus();
                if (shared && shared->event) refresh(*shared->event);
            });

            // Interpolator: dead-reckons position at 140 fps while the
            // command queue has pending work; backs off when idle.
            interpolator.onFrame([this](Rev::Core::AnimationEvent& frame) {

                if (!dispReady) {
                    frame.delayNext(500);
                    return;
                }

                if (jogQueue.empty()) {
                    // Nothing in flight — display is already at rest.
                    frame.delayNext(200);
                    return;
                }

                // Walk the queue, consuming completed commands and
                // interpolating the current one.
                auto now = Clock::now();

                while (!jogQueue.empty()) {

                    JogCmd& cmd = jogQueue.front();
                    float elapsed = std::chrono::duration<float, std::milli>(now - cmd.t0).count();

                    if (elapsed >= cmd.durMs) {
                        // Command fully executed — advance to its end position.
                        dispX = cmd.ex(); dispY = cmd.ey();
                        dispZ = cmd.ez(); dispA = cmd.ea();
                        jogQueue.pop_front();

                    } else {
                        // Command in progress — linear interpolation.
                        float t = elapsed / cmd.durMs; // 0..1
                        dispX = cmd.sx + t * cmd.dx;
                        dispY = cmd.sy + t * cmd.dy;
                        dispZ = cmd.sz + t * cmd.dz;
                        dispA = cmd.sa + t * cmd.da;
                        break;
                    }
                }

                updatePosDisplay();

                // Back off once the queue drains.
                if (jogQueue.empty()) frame.delayNext(200);
            });

            interpolator.play();

            gestures.onGesture = [this](CarveraCommand cmd, Event& e) {
                switch (cmd) {
                    case CarveraCommand::Connect:    onConnectClick(e);   break;
                    case CarveraCommand::Disconnect: onDisconnectClick(e); break;
                    case CarveraCommand::Unlock:     unlock(e);           break;
                    case CarveraCommand::Reset:      sendLine("reset\n"); break;
                }
            };
        }

        ~Interface() { if (client) { delete client; client = nullptr; } }

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
            //   [-X]   [+Z|-Z]  [+X]
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
                dispReady = false;
                jogQueue.clear();
                sendStatus();
                keepalive.play();
            });

            client->onDisconnect([this](Rev::Client::DisconnectEvent& e) {
                keepalive.stop();
                jogQueue.clear();
                dispReady = false;
                pushLog("Connection closed by remote.");
                std::lock_guard lock(logMutex);
                pendingState = ""; pendingPosValid = false;
            });

            client->onData([this](Rev::Client::DataEvent& e) {

                std::string msg(e.data.begin(), e.data.end());
                while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();

                if (msg.size() > 1 && msg.front() == '<') {
                    size_t delim  = msg.find_first_of(",|>", 1);
                    std::string state = (delim != std::string::npos) ? msg.substr(1, delim - 1) : "";
                    float x = 0, y = 0, z = 0, a = 0;
                    size_t mp  = msg.find("MPos:");
                    bool posOk = (mp != std::string::npos) &&
                                 sscanf(msg.c_str() + mp + 5, "%f,%f,%f,%f", &x, &y, &z, &a) >= 3;
                    {
                        std::lock_guard lock(logMutex);
                        pendingState = state;
                        if (posOk) {
                            pendingPosX = x; pendingPosY = y;
                            pendingPosZ = z; pendingPosA = a;
                            pendingPosValid = true;
                        }
                    }
                    return;
                }

                if (msg == "ok" || msg.starts_with("ok - ignore:")) return;
                pushLog(std::format("< {}", msg));
            });

            client->onError([this](Rev::Client::ErrorEvent& e) {
                keepalive.stop();
                jogQueue.clear();
                dispReady = false;
                pushLog(std::format("Error: {}", e.reason));
            });

            client->connect(targetHost, targetPort);
        }

        void onDisconnectClick(Event& e) {
            if (!client) return;
            keepalive.stop();
            jogQueue.clear();
            dispReady = false;
            delete client; client = nullptr;
            pushLog("Disconnected.");
            refresh(e);
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

            if (!client || !client->isConnected.load() || !dispReady) return;
            if (jogQueue.size() >= MaxQueueLen) return;

            // Determine when this command will start and what position it begins from.
            // Commands are chained: each starts when the previous one ends.
            Clock::time_point t0;
            float sx, sy, sz, sa;

            if (jogQueue.empty()) {
                t0 = Clock::now();
                sx = dispX; sy = dispY; sz = dispZ; sa = dispA;
            } else {
                const JogCmd& last = jogQueue.back();
                t0 = last.t0 + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<float, std::milli>(last.durMs));
                sx = last.ex(); sy = last.ey(); sz = last.ez(); sa = last.ea();
            }

            float feedMmMs = jogFeedRate / 60000.0f;
            float dist     = std::sqrt(dx*dx + dy*dy + dz*dz);
            float durMs    = (dist > 0.0f && feedMmMs > 0.0f) ? dist / feedMmMs : 1.0f;

            jogQueue.push_back({ sx, sy, sz, sa, dx, dy, dz, 0.0f, t0, durMs });

            // Send incremental command to machine.
            std::string cmd = "$J=G91";
            if (dx != 0.0f) cmd += std::format(" X{:.3f}", dx);
            if (dy != 0.0f) cmd += std::format(" Y{:.3f}", dy);
            if (dz != 0.0f) cmd += std::format(" Z{:.3f}", dz);
            cmd += std::format(" F{}\n", jogFeedRate);
            client->send(cmd);
        }

        void jogA(float degrees) {

            if (!client || !client->isConnected.load() || !dispReady) return;
            if (jogQueue.size() >= MaxQueueLen) return;

            Clock::time_point t0;
            float sx, sy, sz, sa;

            if (jogQueue.empty()) {
                t0 = Clock::now();
                sx = dispX; sy = dispY; sz = dispZ; sa = dispA;
            } else {
                const JogCmd& last = jogQueue.back();
                t0 = last.t0 + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<float, std::milli>(last.durMs));
                sx = last.ex(); sy = last.ey(); sz = last.ez(); sa = last.ea();
            }

            float feedDegMs = jogFeedRateA / 60000.0f;
            float durMs     = (feedDegMs > 0.0f) ? std::abs(degrees) / feedDegMs : 1.0f;

            jogQueue.push_back({ sx, sy, sz, sa, 0.0f, 0.0f, 0.0f, degrees, t0, durMs });

            client->send(std::format("$J=G91 A{:.3f} F{}\n", degrees, jogFeedRateA));
        }

        void sendStatus() {
            if (!client || !client->isConnected.load()) return;
            client->send("?");
        }

        void unlock(Event& e) {
            if (!client || !client->isConnected.load()) { pushLog("Not connected."); return; }
            client->send("$X\n");
            pushLog("> $X  (unlock)");
            refresh(e);
        }

        void updatePosDisplay() {
            if (!dispReady) return;
            if (posXText) posXText->content = std::format("{:.3f}", dispX);
            if (posYText) posYText->content = std::format("{:.3f}", dispY);
            if (posZText) posZText->content = std::format("{:.3f}", dispZ);
            if (posAText) posAText->content = std::format("{:.3f}", dispA);
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

        void drainLog() {

            bool logDirty = false;
            bool posDirty = false;

            {
                std::lock_guard lock(logMutex);

                while (!pendingLog.empty()) {
                    logLines.push_back(std::move(pendingLog.front()));
                    pendingLog.pop_front();
                    logDirty = true;
                }

                machineState = pendingState;

                if (pendingPosValid) {
                    posX = pendingPosX; posY = pendingPosY;
                    posZ = pendingPosZ; posA = pendingPosA;
                    pendingPosValid = false;
                    posDirty = true;
                }
            }

            while (logLines.size() > MaxLogLines) logLines.pop_front();

            if (logDirty && logText) {
                std::string combined;
                for (auto& l : logLines) { combined += l; combined += '\n'; }
                logText->content = combined;
            }

            if (posDirty) {

                if (!dispReady) {
                    // First telemetry report — initialize dead reckoning from machine truth.
                    dispX = posX; dispY = posY; dispZ = posZ; dispA = posA;
                    dispReady = true;
                    updatePosDisplay();

                } else if (jogQueue.empty()) {
                    // No commands in flight — machine is at rest.
                    // Accept telemetry directly: it's the definitive source of truth.
                    dispX = posX; dispY = posY; dispZ = posZ; dispA = posA;
                    updatePosDisplay();

                } else {
                    // Commands are in flight.
                    // Treat telemetry as a calibration signal: compute the difference
                    // between what the machine reports and where we expected it to be
                    // at this moment, then shift the entire queue by that error.
                    // This corrects for network latency and minor timing drift without
                    // interrupting the smooth interpolation.

                    auto  now     = Clock::now();
                    float elapsedFront = std::chrono::duration<float, std::milli>(
                        now - jogQueue.front().t0).count();
                    float t = std::clamp(elapsedFront / jogQueue.front().durMs, 0.0f, 1.0f);

                    float predictedX = jogQueue.front().sx + t * jogQueue.front().dx;
                    float predictedY = jogQueue.front().sy + t * jogQueue.front().dy;
                    float predictedZ = jogQueue.front().sz + t * jogQueue.front().dz;
                    float predictedA = jogQueue.front().sa + t * jogQueue.front().da;

                    float errX = posX - predictedX;
                    float errY = posY - predictedY;
                    float errZ = posZ - predictedZ;
                    float errA = posA - predictedA;

                    // Only apply correction if error is significant (not just noise).
                    constexpr float kCorrThreshold = 0.5f; // mm
                    if (std::abs(errX) > kCorrThreshold || std::abs(errY) > kCorrThreshold ||
                        std::abs(errZ) > kCorrThreshold || std::abs(errA) > kCorrThreshold) {

                        for (JogCmd& cmd : jogQueue) {
                            cmd.sx += errX; cmd.sy += errY;
                            cmd.sz += errZ; cmd.sa += errA;
                        }
                    }
                }
            }
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
                else if (shift && arrows.down)  { activeJogBtn = btnNZ; jog(0, 0, -step); handled = true; }
                else if (arrows.left)           { activeJogBtn = btnNX; jog(-step, 0, 0); handled = true; }
                else if (arrows.right)          { activeJogBtn = btnPX; jog(+step, 0, 0); handled = true; }
                else if (arrows.up)             { activeJogBtn = btnPY; jog(0, +step, 0); handled = true; }
                else if (arrows.down)           { activeJogBtn = btnNY; jog(0, -step, 0); handled = true; }
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

            drainLog();

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

                if      (!isConnected)            connectionSection->styles.add(&Style::PanelBorderNone);
                else if (machineState == "Alarm")  connectionSection->styles.add(&Style::PanelBorderAlarm);
                else if (machineState == "Tool")   connectionSection->styles.add(&Style::PanelBorderToolChange);
                else                               connectionSection->styles.add(&Style::PanelBorderConnected);
            }

            Box* allBtns[] = { btnPX, btnNX, btnPY, btnNY, btnPZ, btnNZ, btnAP, btnAM };
            for (Box* btn : allBtns) if (btn) btn->styles.remove(&Style::JogBtnActive);
            if (activeJogBtn) activeJogBtn->styles.add(&Style::JogBtnActive);

            Box::computeChildren(e);
        }

    private:
        std::deque<std::string> logLines;
    };
}
