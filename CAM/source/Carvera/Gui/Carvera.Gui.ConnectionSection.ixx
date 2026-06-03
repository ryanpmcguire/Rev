module;

#include <string>
#include <format>

#include <managed.hpp>

export module Carvera.Gui.ConnectionSection;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;
import Rev.Element.Text;

import CarveraAir;

import Cam.Gui.Theme;
import Carvera.Gui.Style;

export namespace Carvera::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace Theme = Cam::Gui::Theme;

    // Connection section — title row, host:port readout, and the four
    // primary machine-IO buttons.  Reflects connection + state border.
    struct ConnectionSection : public Box {

        static Carvera::Air& air() { return Carvera::Air::instance(); }

        Box*  statusDot = nullptr;
        Text* statusLabel = nullptr;

        // Track what the section LAST applied to the DOM so computeChildren
        // only mutates styles when the resolved value would actually change.
        // Touching styles.add/remove on every frame marks the element dirty
        // even when nothing changed, which keeps the window in a perpetual
        // redraw loop — see ArmSection / ToolSection for the same pattern.
        enum class BorderKind { Unset, None, Connected, Alarm, ToolChange };
        bool       lastConnApplied_   = false;
        bool       lastConnValid_     = false;
        BorderKind lastBorderApplied_ = BorderKind::Unset;

        ConnectionSection(Element* parent)
            : Box(parent, Theme::withPanel({ &Style::Section }), "ConnectionSection")
        {
            build();
            subscribe();
        }

        void build() {

            Box* titleRow = new Box(this, { &Style::Row }, "TitleRow");
            statusDot = new Box(titleRow, { &Style::StatusDot, &Style::StatusDotDisconnected }, "StatusDot");
            new Text(titleRow, "Carvera Air", Theme::withText({ &Style::Title }));

            Box* statusRow = new Box(this, { &Style::Row }, "StatusRow");
            statusLabel = new Text(
                statusRow,
                std::format("{}:{}", air().targetHost, air().targetPort),
                Theme::withMutedText({ &Style::MutedLabel })
            );

            Box* btnRow = new Box(this, { &Style::Row }, "BtnRow");
            makeBtn(btnRow, "Connect",    Style::Btn)->onClick([this](Event& e) { air().connect();    refresh(e); e.propagate = false; });
            makeBtn(btnRow, "Disconnect", Style::Btn)->onClick([this](Event& e) { air().disconnect(); refresh(e); e.propagate = false; });

            Box* ctrlRow = new Box(this, { &Style::Row }, "CtrlRow");
            makeBtn(ctrlRow, "Unlock", Style::Btn)->onClick([this](Event& e) { air().unlock(); refresh(e); e.propagate = false; });
            makeBtn(ctrlRow, "Reset",  Style::Btn)->onClick([this](Event& e) { air().reset();  refresh(e); e.propagate = false; });
        }

        void subscribe() {
            auto bump = [this](auto&) { if (shared && shared->event) { refresh(*shared->event); } };
            air().onConnection([bump](Carvera::Air::ConnectionEvent& e) { bump(e); });
            air().onState     ([bump](Carvera::Air::StateEvent&      e) { bump(e); });
        }

        // Reflect connection status (dot colour) and machine state (border).
        // Only mutates styles when the resolved value differs from what is
        // already applied — see field declarations for the rationale.
        void computeChildren(Event& e) override {

            const bool        isConnected = air().connected();
            const std::string state       = air().machineState();

            // -- Status dot --
            if (statusDot && (!lastConnValid_ || lastConnApplied_ != isConnected)) {
                statusDot->styles.remove(&Style::StatusDotConnected);
                statusDot->styles.remove(&Style::StatusDotDisconnected);
                statusDot->styles.add(isConnected ? &Style::StatusDotConnected
                                                  : &Style::StatusDotDisconnected);
                lastConnApplied_ = isConnected;
                lastConnValid_   = true;
            }

            // -- Section border --
            BorderKind kind;
            if      (!isConnected)     kind = BorderKind::None;
            else if (state == "Alarm") kind = BorderKind::Alarm;
            else if (state == "Tool")  kind = BorderKind::ToolChange;
            else                       kind = BorderKind::Connected;

            if (kind != lastBorderApplied_) {
                styles.remove(&Style::PanelBorderConnected);
                styles.remove(&Style::PanelBorderAlarm);
                styles.remove(&Style::PanelBorderToolChange);
                styles.remove(&Style::PanelBorderNone);
                switch (kind) {
                    case BorderKind::None:       styles.add(&Style::PanelBorderNone);       break;
                    case BorderKind::Connected:  styles.add(&Style::PanelBorderConnected);  break;
                    case BorderKind::Alarm:      styles.add(&Style::PanelBorderAlarm);      break;
                    case BorderKind::ToolChange: styles.add(&Style::PanelBorderToolChange); break;
                    case BorderKind::Unset:      break;
                }
                lastBorderApplied_ = kind;
            }

            Box::computeChildren(e);
        }
    };
}
