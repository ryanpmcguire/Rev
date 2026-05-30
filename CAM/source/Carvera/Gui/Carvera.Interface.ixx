module;

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <functional>
#include <format>

#include <dbg.hpp>

export module Carvera.Gui.Interface;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;
import Rev.Element.Text;

import Rev.Client;

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

        Rev::Element::Style JogRow = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 100_pct, .height = 46_px }
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
    }

    // ------------------------------------------------------------------
    // Interface — portable CarveraAir control panel.
    //
    // Drop this Box into any window.  It owns the network Client and
    // all connection state; the parent window is just a host.
    // ------------------------------------------------------------------

    struct Interface : public Box {

        // -- Connection state ----------------------------------------

        Rev::Client* client    = nullptr;
        bool         connected = false;

        std::string  targetHost = "127.0.0.1";
        int          targetPort = 9999;

        // Thread-safe log queue.
        std::mutex           logMutex;
        std::deque<std::string> pendingLog;

        static constexpr size_t MaxLogLines = 64;

        // -- UI nodes ------------------------------------------------

        // Connection section
        Box*  statusDot       = nullptr;
        Text* statusLabel     = nullptr;
        Box*  connectBtn      = nullptr;
        Text* connectBtnLabel = nullptr;

        // Log section
        Box*  logBox          = nullptr;
        Text* logText         = nullptr;   // single multiline text, rebuilt on drain

        // -- Helpers -------------------------------------------------

        namespace Namespace = Cam::Gui::Theme;

        Box* makeBtn(Element* parent, const std::string& label, Rev::Element::Style& btnStyle) {

            Box* btn = new Box(
                parent,
                Namespace::withButton({ &btnStyle, &Style::BtnHover, &Style::BtnPress }),
                "Btn"
            );

            new Text(btn, label, Namespace::withText({ &Style::Label }));

            return btn;
        }

        Box* makeJogBtn(Element* parent, const std::string& label) {

            Box* btn = new Box(
                parent,
                Namespace::withButton({ &Style::JogBtn, &Style::BtnHover, &Style::BtnPress }),
                "JogBtn"
            );

            new Text(btn, label, Namespace::withText({ &Style::Label }));

            return btn;
        }

        // -- Construction --------------------------------------------

        Interface(Element* parent) : Box(parent, {}, "CarveraInterface") {

            Namespace::applyMode(Namespace::currentMode());

            this->styles.add(&Style::Root);
            this->styles.add(&Namespace::Styles::Background);

            buildConnectionSection();
            buildJogSection();
            buildLogSection();
        }

        ~Interface() {
            if (client) {
                delete client;
                client = nullptr;
            }
        }

        // -- UI construction -----------------------------------------

        void buildConnectionSection() {

            Box* section = new Box(
                this,
                Namespace::withPanel({ &Style::Section }),
                "ConnectionSection"
            );

            // Title row
            Box* titleRow = new Box(section, { &Style::Row }, "TitleRow");

            statusDot = new Box(
                titleRow,
                { &Style::StatusDot, &Style::StatusDotDisconnected },
                "StatusDot"
            );

            new Text(titleRow, "Carvera Air", Namespace::withText({ &Style::Title }));

            // Status row
            Box* statusRow = new Box(section, { &Style::Row }, "StatusRow");

            statusLabel = new Text(
                statusRow,
                std::format("{}:{}", targetHost, targetPort),
                Namespace::withMutedText({ &Style::MutedLabel })
            );

            // Button row
            Box* btnRow = new Box(section, { &Style::Row }, "BtnRow");

            connectBtn = makeBtn(btnRow, "Connect", Style::Btn);
            connectBtn->onClick([this](Event& e) {
                onConnectClick(e);
                e.propagate = false;
            });

            connectBtnLabel = static_cast<Text*>(connectBtn->children.front());

            Box* disconnectBtn = makeBtn(btnRow, "Disconnect", Style::Btn);
            disconnectBtn->onClick([this](Event& e) {
                onDisconnectClick(e);
                e.propagate = false;
            });

            // Send hello button (quick smoke test)
            Box* helloBtn = makeBtn(btnRow, "Hello", Style::Btn);
            helloBtn->onClick([this](Event& e) {
                sendLine("Hello from Rev!\n");
                e.propagate = false;
            });
        }

        void buildJogSection() {

            Box* section = new Box(
                this,
                Namespace::withPanel({ &Style::Section }),
                "JogSection"
            );

            new Text(section, "Jog", Namespace::withText({ &Style::Label }));

            // +Y row
            Box* row0 = new Box(section, { &Style::JogRow }, "JogRow0");
            auto* pyBtn = makeJogBtn(row0, "+Y");
            pyBtn->onClick([this](Event& e) { jog(0.0f, 1.0f, 0.0f); e.propagate = false; });

            // -X / +X row
            Box* row1 = new Box(section, { &Style::JogRow }, "JogRow1");
            auto* nxBtn = makeJogBtn(row1, "-X");
            nxBtn->onClick([this](Event& e) { jog(-1.0f, 0.0f, 0.0f); e.propagate = false; });
            auto* pxBtn = makeJogBtn(row1, "+X");
            pxBtn->onClick([this](Event& e) { jog(1.0f, 0.0f, 0.0f); e.propagate = false; });

            // -Y row
            Box* row2 = new Box(section, { &Style::JogRow }, "JogRow2");
            auto* nyBtn = makeJogBtn(row2, "-Y");
            nyBtn->onClick([this](Event& e) { jog(0.0f, -1.0f, 0.0f); e.propagate = false; });

            // Z row
            Box* row3 = new Box(section, { &Style::JogRow }, "JogRow3");
            auto* nzBtn = makeJogBtn(row3, "-Z");
            nzBtn->onClick([this](Event& e) { jog(0.0f, 0.0f, -1.0f); e.propagate = false; });
            auto* pzBtn = makeJogBtn(row3, "+Z");
            pzBtn->onClick([this](Event& e) { jog(0.0f, 0.0f, 1.0f); e.propagate = false; });
        }

        void buildLogSection() {

            logBox = new Box(
                this,
                Namespace::withPanel({ &Style::LogBox }),
                "LogBox"
            );

            logText = new Text(
                logBox,
                "(no messages yet)",
                Namespace::withMutedText({ &Style::LogLine })
            );
        }

        // -- Networking ----------------------------------------------

        void onConnectClick(Event& e) {

            if (client) return; // already connected

            pushLog(std::format("Connecting to {}:{}...", targetHost, targetPort));
            refresh(e);

            client = new Rev::Client(
                targetHost, targetPort,
                [this](Rev::Client::NetEvent& ne) { onNetEvent(ne); }
            );
        }

        void onDisconnectClick(Event& e) {
            if (!client) return;
            delete client;
            client    = nullptr;
            connected = false;
            pushLog("Disconnected.");
            refresh(e);
        }

        // Called from the network worker thread — only touch thread-safe state.
        void onNetEvent(Rev::Client::NetEvent& ne) {

            switch (ne.type) {

                case Rev::Client::NetEvent::Connect:
                    connected = true;
                    pushLog(std::format("Connected to {}:{}", targetHost, targetPort));
                    break;

                case Rev::Client::NetEvent::Disconnect:
                    connected = false;
                    pushLog("Connection closed by remote.");
                    if (client) { delete client; client = nullptr; }
                    break;

                case Rev::Client::NetEvent::Data: {
                    std::string msg(ne.data.begin(), ne.data.end());
                    // Strip trailing newlines for display.
                    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
                        msg.pop_back();
                    }
                    pushLog(std::format("< {}", msg));
                    break;
                }

                case Rev::Client::NetEvent::Error:
                    connected = false;
                    pushLog("Connection error.");
                    break;
            }
        }

        void sendLine(const std::string& line) {
            if (!client) {
                pushLog("Not connected.");
                return;
            }
            client->send(line);
            std::string display = line;
            while (!display.empty() && (display.back() == '\n' || display.back() == '\r')) {
                display.pop_back();
            }
            pushLog(std::format("> {}", display));
        }

        void jog(float dx, float dy, float dz) {
            // 1 mm relative move in the requested direction.
            std::string cmd = std::format("G91\nG0 X{:.3f} Y{:.3f} Z{:.3f}\nG90\n",
                                          dx, dy, dz);
            sendLine(cmd);
        }

        // -- Log helpers (thread-safe) --------------------------------

        void pushLog(std::string msg) {
            std::lock_guard lock(logMutex);
            pendingLog.push_back(std::move(msg));
        }

        // Drain pending log into the display Text — call from main thread only.
        void drainLog() {

            bool dirty = false;

            {
                std::lock_guard lock(logMutex);
                while (!pendingLog.empty()) {
                    logLines.push_back(std::move(pendingLog.front()));
                    pendingLog.pop_front();
                    dirty = true;
                }
            }

            // Trim to max.
            while (logLines.size() > MaxLogLines) {
                logLines.pop_front();
            }

            if (dirty && logText) {
                std::string combined;
                for (auto& line : logLines) {
                    combined += line;
                    combined += '\n';
                }
                logText->content = combined;
            }
        }

        // -- Compute / render ----------------------------------------

        void computeChildren(Event& e) override {

            drainLog();

            // Keep status dot color in sync.
            if (statusDot) {
                statusDot->styles.remove(&Style::StatusDotConnected);
                statusDot->styles.remove(&Style::StatusDotDisconnected);
                statusDot->styles.add(connected
                    ? &Style::StatusDotConnected
                    : &Style::StatusDotDisconnected);
            }

            Box::computeChildren(e);
        }

    private:

        std::deque<std::string> logLines;
    };
}
