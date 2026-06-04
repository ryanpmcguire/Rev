module;

#include <string>
#include <managed.hpp>

export module Carvera.Gui.Style;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Style;
import Rev.Element.Box;
import Rev.Element.Text;

import Cam.Gui.Theme;

export namespace Carvera::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace Theme = Cam::Gui::Theme;

    // ------------------------------------------------------------------
    // Styles shared by every section of the Carvera control panel.
    // Each section file imports this module so adjustments here propagate
    // everywhere consistently.
    // ------------------------------------------------------------------

    namespace Style {

        inline Rev::Element::Style Root = {
            .layout  = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size    = { .width = 100_pct, .height = 100_pct },
            .padding = { 16_px, 16_px, 16_px, 16_px }
        };

        inline Rev::Element::Style Section = {
            .layout  = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size    = { .width = 100_pct },
            .margin  = { .bottom = 16_px },
            .padding = { 12_px, 12_px, 12_px, 12_px },
            .border  = { .radius = 6_px }
        };

        // Focus indicator for sections that act as keyboard targets (e.g. the
        // jog section).  Subtle blue ring + tint so the operator can see at a
        // glance that arrow keys will be consumed for jogging.
        inline Rev::Element::Style SectionFocus = {
            .applies    = { .focus = true },
            .background = { .color = rgba(79, 99, 255, 0.08) },
            .border     = { .color = rgba(79, 99, 255, 1.0), .radius = 6_px, .width = 2_px }
        };

        inline Rev::Element::Style Row = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 100_pct, .height = 32_px },
            .margin = { .bottom = 6_px }
        };

        inline Rev::Element::Style Label      = { .text = { .size = 13_px } };
        inline Rev::Element::Style MutedLabel = { .text = { .size = 11_px } };
        inline Rev::Element::Style Title      = { .text = { .size = 16_px } };

        inline Rev::Element::Style Btn = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 80_px, .height = 30_px },
            .margin = { .right = 6_px },
            .border = { .radius = 5_px },
            .cursor = Cursor::Hand
        };

        inline Rev::Element::Style BtnHover = { .applies = { .hover = true, .focus = true } };
        inline Rev::Element::Style BtnPress = { .applies = { .press = true } };

        inline Rev::Element::Style JogBtn = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 56_px, .height = 40_px },
            .margin = { 3_px, 3_px, 3_px, 3_px },
            .border = { .radius = 5_px },
            .cursor = Cursor::Hand
        };

        inline Rev::Element::Style JogBtnActive = {
            .background = { .color = rgba(79, 99, 255, 0.45), .transition = 60_ms }
        };

        inline Rev::Element::Style JogBody    = { .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False }, .size = { .width = 100_pct } };
        inline Rev::Element::Style PosPanel   = { .layout = { Axis::Vertical,   Align::Start, Align::Start, Wrap::False }, .size = { .width = 120_px }, .margin = { .right = 12_px } };
        inline Rev::Element::Style PosRow     = { .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False }, .size = { .width = 100_pct, .height = 30_px } };
        inline Rev::Element::Style PosAxis    = { .size = { .width = 14_px }, .text = { .size = 10_px } };
        inline Rev::Element::Style PosValue   = { .size = { .width = Grow() }, .text = { .font = File("Rev/resources/Fonts/IBMPlexMono/IBMPlexMono-Light.ttf"), .size = 15_px } };
        inline Rev::Element::Style PosUnit    = { .margin = { .left = 3_px }, .text = { .size = 10_px } };
        inline Rev::Element::Style StepRow    = { .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False }, .size = { .width = 100_pct, .height = 24_px }, .margin = { .top = 8_px } };
        inline Rev::Element::Style StepLabel  = { .text = { .font = File("Rev/resources/Fonts/IBMPlexMono/IBMPlexMono-Light.ttf"), .size = 12_px } };
        inline Rev::Element::Style JogGrid    = { .layout = { Axis::Vertical,   Align::Start, Align::Start,  Wrap::False } };
        inline Rev::Element::Style JogGridRow = { .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False } };

        inline Rev::Element::Style ZCell = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size   = { .width = 56_px, .height = 40_px },
            .margin = { 3_px, 3_px, 3_px, 3_px }
        };

        inline Rev::Element::Style ZHalfBtn = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 100_pct, .height = Grow() },
            .border = { .radius = 3_px },
            .cursor = Cursor::Hand
        };

        inline Rev::Element::Style ZHalfGap = { .size = { .width = 100_pct, .height = 2_px } };

        // -- Origin section --------------------------------------------

        inline Rev::Element::Style OriginCoordRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size   = { .width = 100_pct },
            .margin = { .bottom = 6_px }
        };

        inline Rev::Element::Style OriginCell = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size   = { .width = Grow() },
            .margin = { .right = 6_px }
        };

        inline Rev::Element::Style OriginName = {
            .size = { .width = Grow() },
            .text = { .size = 14_px }
        };

        inline Rev::Element::Style LoadedLabel = {
            .margin = { .right = 6_px },
            .text   = { .size = 12_px }
        };

        inline Rev::Element::Style LogBox = {
            .layout  = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size    = { .width = 100_pct, .height = Grow() },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            .border  = { .radius = 6_px }
        };

        inline Rev::Element::Style LogLine = { .text = { .font = File("Rev/resources/Fonts/IBMPlexMono/IBMPlexMono-Regular.ttf"), .size = 11_px } };

        inline Rev::Element::Style StatusDot             = { .size = { .width = 8_px, .height = 8_px }, .margin = { .right = 8_px }, .border = { .radius = 4_px } };
        inline Rev::Element::Style StatusDotConnected    = { .background = { .color = rgba(34,  197, 94,  1.0) } };
        inline Rev::Element::Style StatusDotDisconnected = { .background = { .color = rgba(148, 163, 184, 1.0) } };

        inline Rev::Element::Style PanelBorderConnected  = { .border = { .color = rgba(34,  197, 94,  1.0), .radius = 6_px, .width = 2_px } };
        inline Rev::Element::Style PanelBorderAlarm      = { .border = { .color = rgba(239, 68,  68,  1.0), .radius = 6_px, .width = 2_px } };
        inline Rev::Element::Style PanelBorderToolChange = { .border = { .color = rgba(245, 158, 11,  1.0), .radius = 6_px, .width = 2_px } };
        inline Rev::Element::Style PanelBorderNone       = { .border = { .color = rgba(0,   0,   0,   0.0), .width = 0_px } };

        // Tool-change confirm cue: the Carvera lights blue when it is waiting
        // for the operator to confirm the change.  Mirror that with a blue
        // section border and a blue "Ok" button.
        inline Rev::Element::Style PanelBorderBlue = { .border = { .color = rgba(59, 130, 246, 1.0), .radius = 6_px, .width = 2_px } };

        inline Rev::Element::Style ToolConfirmButton = {
            .background = { .color = rgba(59, 130, 246, 1.0) },
            .border     = { .color = rgba(59, 130, 246, 1.0), .radius = 5_px, .width = 1_px }
        };

        // Arm control: a prominent full-width toggle.  Disarmed it is a normal
        // button; armed it becomes a stark white-on-pastel-red banner.
        inline Rev::Element::Style ArmButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = Grow(), .height = 46_px },
            .margin = { .left = 3_px, .right = 3_px, .bottom = 6_px },
            .border = { .radius = 6_px },
            .cursor = Cursor::Hand
        };

        inline Rev::Element::Style ArmRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size   = { .width = 100_pct }
        };

        inline Rev::Element::Style ArmLabel = { .text = { .size = 15_px } };

        inline Rev::Element::Style ArmedBanner = {
            .background = { .color = rgba(255, 0, 0, 0.25) },
            .border     = { .color = rgba(255, 0, 0, 1.0), .radius = 6_px, .width = 2_px }
        };

        // START/STOP run button -- full-width below the arm row.
        // "Stop" state reuses ArmedBanner (red tint); "Start" uses this green tint.
        inline Rev::Element::Style RunBanner = {
            .background = { .color = rgba(34, 197, 94, 0.2) },
            .border     = { .color = rgba(34, 197, 94, 1.0), .radius = 6_px, .width = 2_px }
        };
    }

    // -- Small construction helpers ---------------------------------

    // Helper-signature note: `Element` (unqualified) collides with the
    // `Rev::Element` namespace that this module imports, so we spell the type
    // out fully here.  Inside the bodies it's unambiguous (Box/Text resolve
    // directly), so only the parameter declarations need qualification.
    inline Box* makeBtn(Rev::Element::Element* parent, const std::string& label, Rev::Element::Style& s) {
        Box* btn = new Box(parent, Theme::withButton({ &s, &Style::BtnHover, &Style::BtnPress }), "Btn");
        new Text(btn, label, Theme::withText({ &Style::Label }));
        return btn;
    }

    inline Box* makeJogBtn(Rev::Element::Element* parent, const std::string& label) {
        Box* btn = new Box(parent, Theme::withButton({ &Style::JogBtn, &Style::BtnHover, &Style::BtnPress }), "JogBtn");
        new Text(btn, label, Theme::withText({ &Style::Label }));
        return btn;
    }
}
