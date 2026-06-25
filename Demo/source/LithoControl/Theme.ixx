module;
// only needs style system headers
#include <string>
#include <vector>
#include <algorithm>

export module LithoControl.Theme;

import Rev.Element;
import Rev.Element.Style;

export namespace LithoControl {
    using namespace Rev;
    using namespace Rev::Element;

    // -------------------------------------------------------------------------
    // Theme constants
    // -------------------------------------------------------------------------

    namespace Theme {

        // Base styles shared across buttons, inputs, sections
        Style SidebarRoot = {
            .overflow = Overflow::Hide,
            .layout = { Axis::Vertical, Align::Start, Align::Start },
            .size   = { 320_px, 100_pct },
            .background = { .color = rgba(20, 20, 20, 1) }
        };

        Style SectionHdr = {
            .layout   = { Axis::Horizontal, Align::Start, Align::Center },
            .size     = { 100_pct },
            .padding  = { 5_px, 5_px, 8_px, 8_px },
            .background = { .color = rgba(20, 20, 20, 1), .transition = 100_ms },
            .border   = { .top = { .color = rgba(42, 42, 42, 1), .width = 1_px } },
            .cursor   = Cursor::Hand
        };

        Style SectionHdrHover = {
            .applies    = { .hover = true },
            .background = { .color = rgba(28, 28, 28, 1) }
        };

        Style SectionBody = {
            .layout  = { Axis::Vertical, Align::Start, Align::Start },
            .size    = { 100_pct },
            .padding = { 8_px, 8_px, 8_px, 8_px }
        };

        Style RowH = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center },
            .size   = { 100_pct },
            .margin = { .bottom = 6_px }
        };

        Style Btn = {
            .layout   = { Axis::Horizontal, Align::Center, Align::Center },
            .size     = { Grow() },
            .margin   = { 3_px, 3_px, 3_px, 3_px },
            .padding  = { 5_px, 5_px, 12_px, 12_px },
            .background = { .color = rgba(30, 30, 30, 1), .transition = 100_ms },
            .border   = { .color = rgba(42, 42, 42, 1), .radius = 3_px, .width = 1_px },
            .cursor   = Cursor::Hand
        };

        Style BtnHover = {
            .applies    = { .hover = true },
            .background = { .color = rgba(42, 42, 42, 1) },
            .border     = { .color = rgba(0, 87, 255, 1) }
        };

        Style BtnAccent = {
            .layout   = { Axis::Horizontal, Align::Center, Align::Center },
            .size     = { Grow() },
            .margin   = { 3_px, 3_px, 3_px, 3_px },
            .padding  = { 5_px, 5_px, 12_px, 12_px },
            .background = { .color = rgba(0, 87, 255, 1) },
            .border   = { .radius = 3_px },
            .cursor   = Cursor::Hand
        };

        Style BtnAccentHover = {
            .applies    = { .hover = true },
            .background = { .color = rgba(0, 200, 255, 1) }
        };

        Style BtnDanger = {
            .layout   = { Axis::Horizontal, Align::Center, Align::Center },
            .size     = { Grow() },
            .margin   = { 3_px, 3_px, 3_px, 3_px },
            .padding  = { 5_px, 5_px, 12_px, 12_px },
            .background = { .color = rgba(255, 59, 48, 1) },
            .border   = { .radius = 3_px },
            .cursor   = Cursor::Hand
        };

        Style BtnSuccess = {
            .layout   = { Axis::Horizontal, Align::Center, Align::Center },
            .size     = { Grow() },
            .margin   = { 3_px, 3_px, 3_px, 3_px },
            .padding  = { 5_px, 5_px, 12_px, 12_px },
            .background = { .color = rgba(48, 209, 88, 1) },
            .border   = { .radius = 3_px },
            .cursor   = Cursor::Hand
        };

        Style BtnWarning = {
            .layout   = { Axis::Horizontal, Align::Center, Align::Center },
            .size     = { Grow() },
            .margin   = { 3_px, 3_px, 3_px, 3_px },
            .padding  = { 5_px, 5_px, 12_px, 12_px },
            .background = { .color = rgba(255, 159, 10, 1) },
            .border   = { .radius = 3_px },
            .cursor   = Cursor::Hand
        };

        Style BtnTxt     = { .text = { .color = rgba(232, 232, 232, 1), .size = 11_px } };
        Style BtnTxtBold = { .text = { .color = rgba(255, 255, 255, 1), .size = 11_px } };

        Style RightPanel = {
            .layout = { Axis::Vertical, Align::Start, Align::Start },
            .size   = { Grow(), 100_pct },
            .background = { .color = rgba(13, 13, 13, 1) }
        };

        Style PreviewArea = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center },
            .size   = { 100_pct, Grow() },
            .margin = { 8_px, 8_px, 8_px, 8_px },
            .background = { .color = rgba(10, 10, 10, 1) },
            .border = { .color = rgba(42, 42, 42, 1), .radius = 4_px, .width = 1_px }
        };

        Style StatusPanel = {
            .layout = { Axis::Vertical, Align::Start, Align::Start },
            .size   = { 100_pct },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            .background = { .color = rgba(20, 20, 20, 1) }
        };

        Style ProgressTrack = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start },
            .size   = { Grow(), 6_px },
            .background = { .color = rgba(28, 28, 28, 1) },
            .border = { .radius = 3_px }
        };

        Style ProgressFill = {
            .size   = { 0_pct, 100_pct },
            .background = { .color = rgba(0, 87, 255, 1) },
            .border = { .radius = 3_px }
        };

        Style LogBox = {
            .overflow = Overflow::Hide,
            .size     = { 100_pct, 100_px },
            .margin   = { .bottom = 4_px },
            .padding  = { 4_px, 4_px, 6_px, 6_px },
            .background = { .color = rgba(28, 28, 28, 1) },
            .border   = { .color = rgba(42, 42, 42, 1), .radius = 3_px, .width = 1_px }
        };

        Style LogText = {
            .size = { 100_pct },
            .text = { .color = rgba(232, 232, 232, 1), .size = 10_px, .wrap = Wrap::BreakWord }
        };

        Style GcodeText = {
            .size = { 100_pct },
            .text = { .color = rgba(0, 255, 136, 1), .size = 10_px, .wrap = Wrap::BreakWord }
        };

        Style DimLabel = { .text = { .color = rgba(232, 232, 232, 0.4f), .size = 9_px } };
        Style NormText = { .text = { .color = rgba(232, 232, 232, 1),    .size = 11_px } };

        Style JobItem = {
            .size     = { 100_pct },
            .padding  = { 4_px, 4_px, 8_px, 8_px },
            .background = { .color = rgba(28, 28, 28, 0), .transition = 80_ms },
            .cursor   = Cursor::Hand
        };

        Style JobItemHover = {
            .applies    = { .hover = true },
            .background = { .color = rgba(28, 28, 28, 1) }
        };

        Style JobItemSelected = {
            .background = { .color = rgba(0, 87, 255, 1) }
        };

    } // namespace Theme

} // namespace LithoControl
