module;

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <functional>
#include <format>
#include <cstdio>
#include <algorithm>

#include <dbg.hpp>

export module Carvera.Gui.Interface;

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

        Rev::Element::Style Label = {
            .text = { .size = 13_px }
        };

        Rev::Element::Style MutedLabel = {
            .text = { .size = 11_px }
        };

        Rev::Element::Style Title = {
            .text = { .size = 16_px }
        };

        Rev::Element::Style Btn = {
            .layout  = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size    = { .width = 80_px, .height = 30_px },
            .margin  = { .right = 6_px },
            .border  = { .radius = 5_px },
            .cursor  = Cursor::Hand
        };

        Rev::Element::Style BtnHover = {
            .applies = { .hover = true, .focus = true }
        };

        Rev::Element::Style BtnPress = {
            .applies = { .press = true }
        };

        Rev::Element::Style JogBtn = {
            .layout  = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size    = { .width = 56_px, .height = 40_px },
            .margin  = { 3_px, 3_px, 3_px, 3_px },
            .border  = { .radius = 5_px },
            .cursor  = Cursor::Hand
        };

        // Jog section body — positions left, grid right
        Rev::Element::Style JogBody = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size   = { .width = 100_pct }
        };

        // Position readout panel (left side of jog section)
        Rev::Element::Style PosPanel = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size   = { .width = 120_px },
            .margin = { .right = 12_px }
        };

        Rev::Element::Style PosRow = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 100_pct, .height = 30_px }
        };

        Rev::Element::Style PosAxis = {
            .size = { .width = 14_px },
            .text = { .size = 10_px }
        };

        Rev::Element::Style PosValue = {
            .size = { .width = Grow() },
            .text = { .size = 15_px }
        };

        Rev::Element::Style PosUnit = {
            .margin = { .left = 3_px },
            .text   = { .size = 10_px }
        };

        Rev::Element::Style StepRow = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 100_pct, .height = 24_px },
            .margin = { .top = 8_px }
        };

        Rev::Element::Style StepLabel = {
            .text = { .size = 12_px }
        };

        // Jog grid (right side of jog section)
        Rev::Element::Style JogGrid = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False }
        };

        Rev::Element::Style JogGridRow = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False }
        };

        // Center cell — same outer footprint as a JogBtn, split into ±Z
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

        Rev::Element::Style ZHalfGap = {
            .size = { .width = 100_pct, .height = 2_px }
        };

        Rev::Element::Style LogBox = {
            .layout  = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size    = { .width = 100_pct, .height = Grow() },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            .border  = { .radius = 6_px }
        };

        Rev::Element::Style LogLine = {
            .text = { .size = 11_px }
        };

        Rev::Element::Style StatusDot = {
            .size   = { .width = 8_px, .height = 8_px },
            .margin = { .right = 8_px },
            .border = { .radius = 4_px }
        };

        Rev::Element::Style StatusDotConnected = {
            .background = { .color = rgba(34, 197, 94, 1.0) }
        };

        Rev::Element::Style StatusDotDisconnected = {
            .background = { .color = rgba(148, 163, 184, 1.0) }
        };

        // Panel border — reflects machine state at a glance.
        Rev::Element::Style PanelBorderConnected = {
            .border = { .color = rgba(34, 197, 94,  1.0), .width = 2_px, .radius = 6_px }
        };

        Rev::Element::Style PanelBorderAlarm = {
            .border = { .color = rgba(239, 68,  68,  1.0), .width = 2_px, .radius = 6_px }
        };

        Rev::Element::Style PanelBorderToolChange = {
            .border = { .color = rgba(245, 158, 11,  1.0), .width = 2_px, .radius = 6_px }
        };

        Rev::Element::Style PanelBorderNone = {
            .border = { .color = rgba(0, 0, 0, 0.0), .width = 0_px }
        };
    }

    namespace Theme = Cam::Gui::Theme;

    enum class CarveraCommand {
        Connect,
        Disconnect,
        Unlock,
        Reset,
    };

    // ------------------------------------------------------------------
    // Interface — portable CarveraAir control panel.
    // ------------------------------------------------------------------

    struct Interface : public Box {

        // -- Connection state ----------------------------------------

        Rev::Client* client = nullptr;

        std::string  targetHost = "192.168.1.104";
        int          targetPort = 2222;

        // -- Jog parameters ------------------------------------------

        static constexpr float kStepPresets[] = { 0.01f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
        static constexpr int   kStepCount     = 6;

        int   stepIndex   = 3;       // index into kStepPresets (1.0 mm default)
        float jogStepMm   = 1.0f;   // kept in sync with kStepPresets[stepIndex]
        float jogStepDeg  = 5.0f;   // A axis step (degrees)
        int   jogFeedRate = 1000;   // XYZ feed rate (mm/min)
        int   jogFeedRateA = 3000;  // A axis feed rate (deg/min)

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

        // -- Keepalive -----------------------------------------------

        Rev::Core::Animator keepalive { 200 };

        // -- Thread-safe state (worker → main) -----------------------

        std::mutex              logMutex;
        std::deque<std::string> pendingLog;
        std::string             pendingState;
        float  pendingPosX = 0, pendingPosY = 0, pendingPosZ = 0, pendingPosA = 0;
        bool   pendingPosValid = false;

        // Main-thread state
        std::string machineState;
        float  posX = 0, posY = 0, posZ = 0, posA = 0;

        static constexpr size_t MaxLogLines = 64;

        // -- UI nodes ------------------------------------------------

        Box*  connectionSection = nullptr;
        Box*  statusDot         = nullptr;
        Text* statusLabel       = nullptr;
        Box*  connectBtn        = nullptr;
        Text* connectBtnLabel   = nullptr;

        // Position readout
        Text* posXText  = nullptr;
        Text* posYText  = nullptr;
        Text* posZText  = nullptr;
        Text* posAText  = nullptr;
        Text* stepText  = nullptr;

        // Log
        Box*  logBox  = nullptr;
        Text* logText = nullptr;

        // -- Helpers -------------------------------------------------

        Box* makeBtn(Element* parent, const std::string& label, Rev::Element::Style& btnStyle) {
            Box* btn = new Box(
                parent,
                Theme::withButton({ &btnStyle, &Style::BtnHover, &Style::BtnPress }),
                "Btn"
            );
            new Text(btn, label, Theme::withText({ &Style::Label }));
            return btn;
        }

        Box* makeJogBtn(Element* parent, const std::string& label) {
            Box* btn = new Box(
                parent,
                Theme::withButton({ &Style::JogBtn, &Style::BtnHover, &Style::BtnPress }),
                "JogBtn"
            );
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

            keepalive.onFrame([this](Rev::Core::AnimationEvent& e) {
                sendStatus();
                if (shared && shared->event) {
                    refresh(*shared->event);
                }
            });

            gestures.onGesture = [this](CarveraCommand cmd, Event& e) {
                switch (cmd) {
                    case CarveraCommand::Connect:    onConnectClick(e);    break;
                    case CarveraCommand::Disconnect: onDisconnectClick(e); break;
                    case CarveraCommand::Unlock:     unlock(e);            break;
                    case CarveraCommand::Reset:      sendLine("reset\n");  break;
                }
            };
        }

        ~Interface() {
            if (client) { delete client; client = nullptr; }
        }

        // -- UI construction -----------------------------------------

        void buildConnectionSection() {

            connectionSection = new Box(
                this,
                Theme::withPanel({ &Style::Section }),
                "ConnectionSection"
            );

            Box* section = connectionSection;

            Box* titleRow = new Box(section, { &Style::Row }, "TitleRow");
            statusDot = new Box(titleRow, { &Style::StatusDot, &Style::StatusDotDisconnected }, "StatusDot");
            new Text(titleRow, "Carvera Air", Theme::withText({ &Style::Title }));

            Box* statusRow = new Box(section, { &Style::Row }, "StatusRow");
            statusLabel = new Text(
                statusRow,
                std::format("{}:{}", targetHost, targetPort),
                Theme::withMutedText({ &Style::MutedLabel })
            );

            Box* btnRow = new Box(section, { &Style::Row }, "BtnRow");
            connectBtn = makeBtn(btnRow, "Connect", Style::Btn);
            connectBtn->onClick([this](Event& e) { onConnectClick(e);    e.propagate = false; });
            connectBtnLabel = static_cast<Text*>(connectBtn->children.front());

            auto* disconnectBtn = makeBtn(btnRow, "Disconnect", Style::Btn);
            disconnectBtn->onClick([this](Event& e) { onDisconnectClick(e); e.propagate = false; });

            Box* ctrlRow = new Box(section, { &Style::Row }, "CtrlRow");
            auto* unlockBtn = makeBtn(ctrlRow, "Unlock", Style::Btn);
            unlockBtn->onClick([this](Event& e) { unlock(e);              e.propagate = false; });

            auto* resetBtn = makeBtn(ctrlRow, "Reset", Style::Btn);
            resetBtn->onClick([this](Event& e) { sendLine("reset\n");     e.propagate = false; });
        }

        void buildJogSection() {

            Box* section = new Box(
                this,
                Theme::withPanel({ &Style::Section }),
                "JogSection"
            );

            new Text(section, "Jog", Theme::withText({ &Style::Label }));

            Box* body = new Box(section, { &Style::JogBody }, "JogBody");

            // -- Position panel (left) --------------------------------

            Box* posPanel = new Box(body, { &Style::PosPanel }, "PosPanel");

            auto makeAxisRow = [&](
                const std::string& axis,
                Text*&             valueOut,
                const std::string& unit
            ) {
                Box* row = new Box(posPanel, { &Style::PosRow }, "PosRow");
                new Text(row, axis, Theme::withMutedText({ &Style::PosAxis }));
                valueOut = new Text(row, "---", Theme::withText({ &Style::PosValue }));
                new Text(row, unit,  Theme::withMutedText({ &Style::PosUnit }));
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
            Box* row0 = new Box(grid, { &Style::JogGridRow }, "JogGridRow0");

            auto* amBtn = makeJogBtn(row0, "A-");
            amBtn->onClick([this](Event& e) { jogA(-jogStepDeg); e.propagate = false; });

            auto* pyBtn = makeJogBtn(row0, "+Y");
            pyBtn->onClick([this](Event& e) { jog(0.0f, +jogStepMm, 0.0f); e.propagate = false; });

            auto* apBtn = makeJogBtn(row0, "A+");
            apBtn->onClick([this](Event& e) { jogA(+jogStepDeg); e.propagate = false; });

            // Row 1: -X | [+Z / -Z] | +X
            Box* row1 = new Box(grid, { &Style::JogGridRow }, "JogGridRow1");

            auto* nxBtn = makeJogBtn(row1, "-X");
            nxBtn->onClick([this](Event& e) { jog(-jogStepMm, 0.0f, 0.0f); e.propagate = false; });

            // Split Z cell
            Box* zCell = new Box(row1, { &Style::ZCell }, "ZCell");

            Box* pzBtn = new Box(
                zCell,
                Theme::withButton({ &Style::ZHalfBtn, &Style::BtnHover, &Style::BtnPress }),
                "ZPlusBtn"
            );
            new Text(pzBtn, "+Z", Theme::withText({ &Style::MutedLabel }));
            pzBtn->onClick([this](Event& e) { jog(0.0f, 0.0f, +jogStepMm); e.propagate = false; });

            new Box(zCell, { &Style::ZHalfGap }, "ZGap");

            Box* nzBtn = new Box(
                zCell,
                Theme::withButton({ &Style::ZHalfBtn, &Style::BtnHover, &Style::BtnPress }),
                "ZMinusBtn"
            );
            new Text(nzBtn, "-Z", Theme::withText({ &Style::MutedLabel }));
            nzBtn->onClick([this](Event& e) { jog(0.0f, 0.0f, -jogStepMm); e.propagate = false; });

            auto* pxBtn = makeJogBtn(row1, "+X");
            pxBtn->onClick([this](Event& e) { jog(+jogStepMm, 0.0f, 0.0f); e.propagate = false; });

            // Row 2: step- | -Y | step+
            Box* row2 = new Box(grid, { &Style::JogGridRow }, "JogGridRow2");

            auto* smBtn = makeJogBtn(row2, "-");
            smBtn->onClick([this](Event& e) { adjustStep(-1); e.propagate = false; });

            auto* nyBtn = makeJogBtn(row2, "-Y");
            nyBtn->onClick([this](Event& e) { jog(0.0f, -jogStepMm, 0.0f); e.propagate = false; });

            auto* spBtn = makeJogBtn(row2, "+");
            spBtn->onClick([this](Event& e) { adjustStep(+1); e.propagate = false; });
        }

        void buildLogSection() {

            logBox = new Box(this, Theme::withPanel({ &Style::LogBox }), "LogBox");

            logText = new Text(
                logBox,
                "(no messages yet)",
                Theme::withMutedText({ &Style::LogLine })
            );
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
                sendStatus();
                keepalive.play();
            });

            client->onDisconnect([this](Rev::Client::DisconnectEvent& e) {
                keepalive.stop();
                pushLog("Connection closed by remote.");
                std::lock_guard lock(logMutex);
                pendingState    = "";
                pendingPosValid = false;
            });

            client->onData([this](Rev::Client::DataEvent& e) {

                std::string msg(e.data.begin(), e.data.end());
                while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'))
                    msg.pop_back();

                // Status response — parse state + position, don't log
                if (msg.size() > 1 && msg.front() == '<') {

                    size_t delim = msg.find_first_of(",|>", 1);
                    std::string state = (delim != std::string::npos)
                        ? msg.substr(1, delim - 1)
                        : "";

                    float x = 0, y = 0, z = 0, a = 0;
                    size_t mp = msg.find("MPos:");
                    bool   posValid = false;

                    if (mp != std::string::npos) {
                        int n = sscanf(msg.c_str() + mp + 5, "%f,%f,%f,%f", &x, &y, &z, &a);
                        posValid = (n >= 3);
                    }

                    {
                        std::lock_guard lock(logMutex);
                        pendingState = state;
                        if (posValid) {
                            pendingPosX = x; pendingPosY = y;
                            pendingPosZ = z; pendingPosA = a;
                            pendingPosValid = true;
                        }
                    }
                    return; // suppress from log
                }

                // Suppress noisy "ok" acknowledgements
                if (msg == "ok" || msg.starts_with("ok - ignore:")) return;

                pushLog(std::format("< {}", msg));
            });

            client->onError([this](Rev::Client::ErrorEvent& e) {
                keepalive.stop();
                pushLog(std::format("Error: {}", e.reason));
            });

            client->connect(targetHost, targetPort);
        }

        void onDisconnectClick(Event& e) {
            if (!client) return;
            keepalive.stop();
            delete client;
            client = nullptr;
            pushLog("Disconnected.");
            refresh(e);
        }

        // -- Machine commands ----------------------------------------

        void sendLine(const std::string& line) {
            if (!client || !client->isConnected.load()) {
                pushLog("Not connected.");
                return;
            }
            client->send(line);
            std::string display = line;
            while (!display.empty() && (display.back() == '\n' || display.back() == '\r'))
                display.pop_back();
            pushLog(std::format("> {}", display));
        }

        void jog(float dx, float dy, float dz) {
            if (!client || !client->isConnected.load()) { pushLog("Not connected."); return; }
            std::string cmd = "$J=G91";
            if (dx != 0.0f) cmd += std::format(" X{:.3f}", dx);
            if (dy != 0.0f) cmd += std::format(" Y{:.3f}", dy);
            if (dz != 0.0f) cmd += std::format(" Z{:.3f}", dz);
            cmd += std::format(" F{}\n", jogFeedRate);
            client->send(cmd);
        }

        void jogA(float degrees) {
            if (!client || !client->isConnected.load()) { pushLog("Not connected."); return; }
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

            while (logLines.size() > MaxLogLines)
                logLines.pop_front();

            if (logDirty && logText) {
                std::string combined;
                for (auto& line : logLines) { combined += line; combined += '\n'; }
                logText->content = combined;
            }

            if (posDirty) {
                if (posXText) posXText->content = std::format("{:.3f}", posX);
                if (posYText) posYText->content = std::format("{:.3f}", posY);
                if (posZText) posZText->content = std::format("{:.3f}", posZ);
                if (posAText) posAText->content = std::format("{:.3f}", posA);
            }
        }

        // -- Keyboard ------------------------------------------------

        void keyDown(Event& e) override {

            auto& arrows = e.keyboard.arrows;
            bool  shift  = e.keyboard.shift;
            bool  ctrl   = e.keyboard.ctrl;
            bool  alt    = e.keyboard.alt;

            // A axis — Alt + Up/Down
            //   Alt          →  5°
            //   Alt+Ctrl     →  0.1°
            //   Alt+Shift    →  45°
            if (alt && (arrows.up || arrows.down)) {
                float step = jogStepDeg;
                if (ctrl)  step =  0.1f;
                if (shift) step = 45.0f;
                if (arrows.up)   { jogA(+step); e.propagate = false; return; }
                if (arrows.down) { jogA(-step); e.propagate = false; return; }
            }

            if (alt) { Box::keyDown(e); return; }

            // XYZ step size from modifier
            //   plain  →  1× (jogStepMm)
            //   Ctrl   →  0.1×
            //   Shift  →  10×
            float step = jogStepMm;
            if (shift) step = jogStepMm * 10.0f;
            if (ctrl)  step = jogStepMm *  0.1f;

            // Z: Shift+Up/Down
            if (shift && arrows.up)   { jog(0.0f, 0.0f, +step); e.propagate = false; return; }
            if (shift && arrows.down) { jog(0.0f, 0.0f, -step); e.propagate = false; return; }

            if (arrows.left)  { jog(-step,  0.0f,  0.0f); e.propagate = false; return; }
            if (arrows.right) { jog(+step,  0.0f,  0.0f); e.propagate = false; return; }
            if (arrows.up)    { jog( 0.0f, +step,  0.0f); e.propagate = false; return; }
            if (arrows.down)  { jog( 0.0f, -step,  0.0f); e.propagate = false; return; }

            if (gestures.track(e)) { e.propagate = false; return; }

            Box::keyDown(e);
        }

        // -- Compute / render ----------------------------------------

        void computeChildren(Event& e) override {

            drainLog();

            bool isConnected = client && client->isConnected.load();

            // Reset position display when disconnected
            if (!isConnected) {
                if (posXText) posXText->content = "---";
                if (posYText) posYText->content = "---";
                if (posZText) posZText->content = "---";
                if (posAText) posAText->content = "---";
            }

            // Status dot
            if (statusDot) {
                statusDot->styles.remove(&Style::StatusDotConnected);
                statusDot->styles.remove(&Style::StatusDotDisconnected);
                statusDot->styles.add(isConnected
                    ? &Style::StatusDotConnected
                    : &Style::StatusDotDisconnected);
            }

            // Connection section border
            if (connectionSection) {
                connectionSection->styles.remove(&Style::PanelBorderConnected);
                connectionSection->styles.remove(&Style::PanelBorderAlarm);
                connectionSection->styles.remove(&Style::PanelBorderToolChange);
                connectionSection->styles.remove(&Style::PanelBorderNone);

                if (!isConnected)              connectionSection->styles.add(&Style::PanelBorderNone);
                else if (machineState == "Alarm") connectionSection->styles.add(&Style::PanelBorderAlarm);
                else if (machineState == "Tool")  connectionSection->styles.add(&Style::PanelBorderToolChange);
                else                              connectionSection->styles.add(&Style::PanelBorderConnected);
            }

            Box::computeChildren(e);
        }

    private:

        std::deque<std::string> logLines;
    };
}
