module;

#include <cstddef>
#include <string>

#include <dbg.hpp>

export module Interface;

import Rev.Element;
import Rev.Element.Style;
import Rev.Element.Event;

import Rev.Element.Box;

export namespace LayoutDiagnose {

    using namespace Rev;
    using namespace Rev::Element;

    // A red 1px outline + rounded corners so every element's true rect is
    // visible. Background colours are distinct per role so we can see exactly
    // which box overshoots.
    inline Border outline(float radius = 4.0f) {
        return { .color = rgba(255, 0, 0, 1.0), .radius = Px(radius), .width = 1_px };
    }

    // A reduced, geometry-only mirror of the real app's layout tree:
    //
    //   Interface (vertical, 100% x 100%)
    //     tabView   : 100% x 80px                    (placeholder for TabView)
    //     content   : 100% x Grow(), horizontal      (the real "body")
    //       leftPanel : 380px x 100% (max 100%), margin/padding, overflow:Hide,
    //                   scroll:Vertical  — same flex as the real LeftPanel
    //         list    : 100% wide vertical stack of dummy "stage" cards
    //       worldView : Grow() x Grow()  (same flex as the real WorldView; empty)
    //
    // Everything is a plain Box, so the only thing under test is the layout engine.
    struct Interface : public Box {

        Interface(Element* parent) : Box(parent, {}, "Interface") {

            this->style->layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
            this->style->size = { .width = 100_pct, .height = 100_pct, .max = { .height = 50_pct } };
            this->style->padding = { .left = 12_px, .right = 12_px, .top = 12_px, .bottom = 12_px };
            this->style->background.color = rgba(24, 24, 28, 1.0);

            // tabView placeholder: full width, fixed 80px tall.
            Box* tabView = new Box(this, {}, "TabView");
            tabView->style = {
                .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
                .size = { .width = 100_pct, .height = 80_px },
                .background = { .color = rgba(50, 70, 120, 1.0) },
                .border = outline(6.0f)
            };

            // content: full width, grows to fill the rest. The real "body".
            Box* content = new Box(this, {}, "Content");
            content->style = {
                .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
                .size = { .width = 100_pct, .height = Grow() },
                .padding = { .left = 12_px, .right = 12_px, .top = 12_px, .bottom = 12_px },
                .background = { .color = rgba(255, 0, 0, 0.1) },
                .border = outline(6.0f)
            };

            // leftPanel: EXACT flex settings of the real Cam::Gui::LeftPanel.
            Box* leftPanel = new Box(content, {}, "LeftPanel");
            leftPanel->style = {
                .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
                .size = { .width = 380_px, .height = 100_pct, .max = { .height = 100_pct } },
                //.margin = { .left = 12_px, .right = 12_px, .top = 12_px, .bottom = 12_px },
                .padding = { .left = 8_px, .right = 8_px, .top = 8_px, .bottom = 8_px },
                .background = { .color = rgba(40, 40, 50, 1.0) },
                .border = outline(6.0f),
                .overflow = Overflow::Hide,
                .scroll = Scroll::Vertical,
                .zIndex = +1
            };

            // The material-state list: full-width vertical stack (mirrors Stages).
            Box* list = new Box(leftPanel, {}, "StageList");
            list->style = {
                .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
                .size = { .width = 100_pct },
                .margin = { .top = 2_px, .bottom = 2_px },
                .padding = { .left = 2_px, .right = 2_px, .top = 4_px, .bottom = 4_px },
                .background = { .color = rgba(0, 0, 0, 0.0) }
            };

            // A handful of dummy "stage" cards. Tall enough in aggregate to
            // overflow the panel, so we can confirm the panel clips/scrolls
            // rather than growing (and doesn't inflate its ancestors).
            for (size_t i = 0; i < 6; i++) {
                Box* card = new Box(list, {}, "StageCard");
                card->style = {
                    .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
                    .size = { .width = 100_pct, .height = 110_px },
                    .margin = { .bottom = 4_px },
                    .background = { .color = rgba(70, 70, 95, 1.0) },
                    .border = outline(4.0f)
                };
            }

            // worldView: EXACT flex settings of the real Cam::Gui::WorldView.
            // No children, per the brief.
            Box* worldView = new Box(content, {}, "WorldView");
            worldView->style = {
                .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
                .size = { .width = Grow(), .height = Grow() },
                .background = { .color = rgba(40, 90, 60, 1.0) },
                .border = outline(6.0f)
            };
        }

        ~Interface() {}
    };
};
