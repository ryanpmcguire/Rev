module;

#include <string>

export module Gui.Machine.Connect;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;

import Rev.Core.Observable;

import Machine.Base;
import Machine.Events;   // Event::State (run-state token)

export namespace Gui {

    using namespace Rev;
    using namespace Rev::Element;
    using namespace Rev::Appearance;

    // Pin Text to the element (there is also a Rev::Primitive::Text in scope).
    using Text = Rev::Element::Text;

    // The Connect section: the machine's connection controls -- a thin
    // reflection of the machine's connection state. It DECLARES its structure
    // (status, address, the four IO controls) once, in the constructor; it never
    // rebuilds. Reflecting live state (dot colour, address, enablement) is the
    // job of computeChildren -- which here just holds the disconnected default.
    //
    // Indentation in the three sections below (styles, pointers, construction)
    // mirrors the element tree: each level of nesting is one level of indent, so
    // all three read as the same shape.
    struct ConnectSection : public Box {

        // Styles
        //--------------------------------------------------

        // Full-width dark panel: rounded, hairline-bordered, padded column.
        static inline Style Section = {
            .layout     = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size       = { .width = 100_pct },
            .padding    = { 14_px, 14_px, 14_px, 14_px },
            .background = { .color = rgba(255, 255, 255, 0.03) },
            .border     = { .color = rgba(255, 255, 255, 0.08), .radius = 6_px, .width = 1_px }
        };

        // Status ring overlaid on the panel -- its colour tracks the run-state.
        static inline Style RingNominal = { .border = { .color = rgba( 64, 200, 120, 0.85), .radius = 6_px, .width = 1_px } };  // idle / nominal
        static inline Style RingHoming  = { .border = { .color = rgba(238, 222, 130, 0.85), .radius = 6_px, .width = 1_px } };  // homing
        static inline Style RingTool    = { .border = { .color = rgba(244, 184, 120, 0.85), .radius = 6_px, .width = 1_px } };  // tool change
        static inline Style RingRun     = { .border = { .color = rgba(128, 206, 230, 0.85), .radius = 6_px, .width = 1_px } };  // executing
        static inline Style RingAlarm   = { .border = { .color = rgba(236, 128, 128, 0.85), .radius = 6_px, .width = 1_px } };  // alarm

            // Full-width horizontal strip, vertically centred, gap below. Height
            // comes from its tallest child, not a fixed value.
            static inline Style Row = {
                .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False, CrossAlign::True },
                .size   = { .width = 100_pct },
                .margin = { .top = 4_px, .bottom = 4_px }
            };

                // Round dot with right margin.
                static inline Style Dot = {
                    .size   = { .width = 12_px, .height = 12_px },
                    .margin = { .right = 8_px },
                    .border = { .radius = 4_px }
                };

                // Disconnected default -- a dim, inert indicator.
                static inline Style DotDisconnected = {
                    .background = { .color = rgba(96, 102, 112, 1.0) }
                };

                // Connected -- colour tracks the run-state (matches the ring).
                static inline Style DotNominal = { .background = { .color = rgba( 64, 200, 120, 1.0) } };  // idle / nominal
                static inline Style DotHoming  = { .background = { .color = rgba(238, 222, 130, 1.0) } };  // homing
                static inline Style DotTool    = { .background = { .color = rgba(244, 184, 120, 1.0) } };  // tool change
                static inline Style DotRun     = { .background = { .color = rgba(128, 206, 230, 1.0) } };  // executing
                static inline Style DotAlarm   = { .background = { .color = rgba(236, 128, 128, 1.0) } };  // alarm

                // Machine name -- bright, slightly larger.
                static inline Style Title = {
                    .text = { .color = rgba(236, 238, 242, 1.0), .size = 16_px }
                };

                // Address readout -- muted and small, set off from the name.
                static inline Style Address = {
                    .margin = { .left = 8_px },
                    .text   = { .color = rgba(140, 146, 156, 1.0), .size = 12_px }
                };

                // Equal-width control: faint fill, hairline border, hand cursor.
                // Height comes from the label plus vertical padding, not a fixed
                // value. Fill/border transition so hover eases in and out.
                static inline Style Btn = {
                    .layout     = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
                    .size       = { .width = Grow() },
                    .margin     = { .left = 4_px, .right = 4_px },
                    .padding    = { .top = 8_px, .bottom = 8_px },
                    .background = { .color = rgba(255, 255, 255, 0.06), .transition = 120_ms },
                    .border     = { .color = rgba(255, 255, 255, 0.10), .radius = 4_px, .width = 1_px, .transition = 120_ms },
                    .cursor     = Cursor::Hand
                };

                    // Hover: brighter fill + border to read as interactive (the
                    // hand cursor already comes from Btn).
                    static inline Style BtnHover = {
                        .applies    = { .hover = true },
                        .background = { .color = rgba(255, 255, 255, 0.12) },
                        .border     = { .color = rgba(255, 255, 255, 0.22) }
                    };

                    // Press: momentary brighter flash for tactile feedback.
                    static inline Style BtnPress = {
                        .applies    = { .press = true },
                        .background = { .color = rgba(255, 255, 255, 0.18) }
                    };

                    // Light label legible on the dark button fill.
                    static inline Style BtnLabel = {
                        .text = { .color = rgba(220, 224, 230, 1.0), .size = 13_px }
                    };

                // Disabled control: dimmed fill/border, inert cursor.
                static inline Style BtnDisabled = {
                    .applies    = { .disabled = true },
                    .background = { .color = rgba(255, 255, 255, 0.02) },
                    .border     = { .color = rgba(255, 255, 255, 0.05), .radius = 4_px, .width = 1_px },
                    .cursor     = Cursor::NotAllowed
                };

                    // Dimmed label for a disabled control.
                    static inline Style BtnLabelDisabled = {
                        .applies = { .disabled = true },
                        .text    = { .color = rgba(110, 114, 122, 1.0) }
                    };

        // Elements
        //--------------------------------------------------

            // Status indicator, machine name, and network address -- all inline.
            Box* titleRow = nullptr;
                Box*  statusDot = nullptr;
                Text* title     = nullptr;
                Text* address   = nullptr;

            // Open / close the connection.
            Box* ioRow = nullptr;
                Button* connectButton    = nullptr;
                Button* disconnectButton = nullptr;

            // Clear an alarm / soft-reset the controller.
            Box* ctrlRow = nullptr;
                Button* unlockButton = nullptr;
                Button* resetButton  = nullptr;

        // State
        //--------------------------------------------------

        // Watches the connection flag so the button states are only re-applied
        // when it actually flips -- connection changes are rare.
        Core::Observer<bool> connectionObserver;

        // The machine this section reflects and drives.
        Machine::MachineBase& machine;

        // Latest run-state token, plus the status styles currently applied -- so the
        // dot/ring only re-style on a real change.
        std::string stateText;
        Style*      appliedDot  = &DotDisconnected;
        Style*      appliedRing = nullptr;

        // Create
        //--------------------------------------------------

        ConnectSection(Element* parent, Machine::MachineBase& machine)
            : Box(parent, { &Section }, "ConnectSection"), machine(machine) {

            // Status indicator, machine name, and network address -- all inline.
            titleRow = new Box(this, { &Row }, "TitleRow");
            titleRow->style->margin.bottom = 12_px;

                statusDot = new Box(titleRow, { &Dot, &DotDisconnected }, "StatusDot");
                title     = new Text(titleRow, "Carvera Air",   { &Title });
                address   = new Text(titleRow, "Not connected", { &Address });

            // Open / close the connection.
            ioRow = new Box(this, { &Row }, "IoRow");
                connectButton    = new Button(ioRow, { .label = "Connect",    .labelStyles = { &BtnLabel, &BtnLabelDisabled } }, { &Btn, &BtnHover, &BtnPress, &BtnDisabled });
                disconnectButton = new Button(ioRow, { .label = "Disconnect", .labelStyles = { &BtnLabel, &BtnLabelDisabled } }, { &Btn, &BtnHover, &BtnPress, &BtnDisabled });

            // Clear an alarm / soft-reset the controller.
            ctrlRow = new Box(this, { &Row }, "CtrlRow");
                unlockButton = new Button(ctrlRow, { .label = "Unlock", .labelStyles = { &BtnLabel, &BtnLabelDisabled } }, { &Btn, &BtnHover, &BtnPress, &BtnDisabled });
                resetButton  = new Button(ctrlRow, { .label = "Reset",  .labelStyles = { &BtnLabel, &BtnLabelDisabled } }, { &Btn, &BtnHover, &BtnPress, &BtnDisabled });

            // Drive the machine.
            connectButton->onClick   ([this](Event&) { this->machine.connect();    });
            disconnectButton->onClick([this](Event&) { this->machine.disconnect(); });
            unlockButton->onClick    ([this](Event&) { this->machine.unlock();      });
            resetButton->onClick     ([this](Event&) { this->machine.reset();       });

            // Reflect connection edges: flip our state and let computeChildren do
            // the rest. We subscribe AS `this`, so ~ConnectSection can unsubscribe.
            machine.info.network.onConnect   (this, [this]() { if (shared && shared->event) { this->refresh(*shared->event); } });
            machine.info.network.onDisconnect(this, [this]() { stateText.clear(); if (shared && shared->event) { this->refresh(*shared->event); } });

            // Reflect the run-state: re-render only when the token actually changes
            // (it arrives on every status frame, ~20x/s, mostly unchanged).
            machine.onState(this, [this](Machine::Event::State& e) {
                if (e.state == stateText) { return; }
                stateText = e.state;
                if (shared && shared->event) { this->refresh(*shared->event); }
            });
        }

        // Destroy
        //--------------------------------------------------

        // Drop our subscriptions so the machine -- which outlives us -- never fires
        // a now-dangling callback into a destroyed section.
        ~ConnectSection() { machine.unsubscribe(this); }

        // Reflect
        //--------------------------------------------------

        // The dot + ring styles for a given connection / run-state.
        struct Look { Style* dot; Style* ring; };   // ring is null when there is none

        Look lookFor(bool connected, const std::string& state) const {
            if (!connected)         { return { &DotDisconnected, nullptr }; }
            if (state == "Alarm")   { return { &DotAlarm,  &RingAlarm  }; }
            if (state == "Home")    { return { &DotHoming, &RingHoming }; }
            if (state == "Tool")    { return { &DotTool,   &RingTool   }; }
            if (state == "Run" ||
                state == "Jog")     { return { &DotRun,    &RingRun    }; }
            return { &DotNominal, &RingNominal };   // Idle / Hold / Sleep / not yet reported
        }

        // Reflect the machine's live state onto the declared structure. Button
        // enablement follows the connection (rare); the dot + ring follow the
        // run-state colour. Each swaps only on a real change.
        void computeChildren(Event& e) override {

            const bool connected = machine.connected();

            if (connectionObserver.changed(connected)) {
                connectButton->setDisabled(connected);
                disconnectButton->setDisabled(!connected);
                unlockButton->setDisabled(!connected);
                resetButton->setDisabled(!connected);
            }

            const Look look = lookFor(connected, stateText);

            if (look.dot != appliedDot) {
                statusDot->styles.remove(appliedDot);
                statusDot->styles.add(look.dot);
                appliedDot = look.dot;
            }

            if (look.ring != appliedRing) {
                if (appliedRing) { this->styles.remove(appliedRing); }
                if (look.ring)   { this->styles.add(look.ring); }
                appliedRing = look.ring;
            }

            Box::computeChildren(e);
        }
    };
}
