module;

#include <string>

export module Gui.Machine.Connect;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;

import Rev.Core.Observable;

import App.Machine;

export namespace Gui {

    using namespace Rev;
    using namespace Rev::Element;
    using namespace Rev::Appearance;

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
            .border     = { .color = rgba(255, 255, 255, 0.08), .radius = 8_px, .width = 1_px }
        };

        // Connected -- a green ring overlaid on the panel.
        static inline Style SectionConnected = {
            .border = { .color = rgba(64, 200, 120, 0.85), .radius = 8_px, .width = 1_px }
        };

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
                    .border = { .radius = 6_px }
                };

                // Disconnected default -- a dim, inert indicator.
                static inline Style DotDisconnected = {
                    .background = { .color = rgba(96, 102, 112, 1.0) }
                };

                // Connected -- bright green.
                static inline Style DotConnected = {
                    .background = { .color = rgba(64, 200, 120, 1.0) }
                };

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
                    .border     = { .color = rgba(255, 255, 255, 0.10), .radius = 6_px, .width = 1_px, .transition = 120_ms },
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
                    .border     = { .color = rgba(255, 255, 255, 0.05), .radius = 6_px, .width = 1_px },
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
        App::Machine& machine;

        // Create
        //--------------------------------------------------

        ConnectSection(Element* parent, App::Machine& machine)
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
            machine.onConnect   (this, [this]() { if (shared && shared->event) { this->refresh(*shared->event); } });
            machine.onDisconnect(this, [this]() { if (shared && shared->event) { this->refresh(*shared->event); } });
        }

        // Destroy
        //--------------------------------------------------

        // Drop our subscriptions so the machine -- which outlives us -- never fires
        // a now-dangling callback into a destroyed section.
        ~ConnectSection() { machine.unsubscribe(this); }

        // Reflect
        //--------------------------------------------------

        // Reflect the machine's live connection state onto the declared
        // structure -- but only when it actually changes (see connectionObserver).
        void computeChildren(Event& e) override {

            const bool connected = machine.connected();

            if (connectionObserver.changed(connected)) {

                // Status dot: green when connected, dim grey otherwise.
                statusDot->styles.remove(&DotDisconnected);
                statusDot->styles.remove(&DotConnected);
                statusDot->styles.add(connected ? &DotConnected : &DotDisconnected);

                // Panel border: green ring while connected.
                if (connected) { this->styles.add(&SectionConnected); }
                else           { this->styles.remove(&SectionConnected); }

                connectButton->setDisabled(connected);
                disconnectButton->setDisabled(!connected);
                unlockButton->setDisabled(!connected);
                resetButton->setDisabled(!connected);
            }

            Box::computeChildren(e);
        }

        void computeStyle(Event& e) override {
            Box::computeStyle(e);
        }
    };
}
