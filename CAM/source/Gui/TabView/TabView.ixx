module;

#include <string>
#include <vector>

export module Cam.Gui.TabView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;

import Cam.App;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace TabViewStyle {

        Style Self = {
            .layout = { Axis::Horizontal, Align::Start, Align::End, Wrap::False },
            .size = { .width = 100_pct, .height = 42_px },
            .margin = { 4_px, 4_px, 0_px, 4_px },
            .padding = { 8_px, 8_px, 0_px, 0_px },
            .border = {
                .bottom = {
                    .color = rgba(255, 0, 0, 1.0),
                    .width = 100_px
                }
            },
            .background = { .color = rgba(0, 0, 0, 0.025) }
        };

        Style Tab = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .height = 34_px },
            .margin = { .right = 5_px },
            .padding = { 15_px, 15_px, 4_px, 2_px },
            .background = { .color = rgba(255, 255, 255, 0.22), .transition = 100_ms },
            .border = {
                .color = rgba(0, 0, 0, 0.16),
                .width = 1_px,
                .tl = { .radius = 7_px },
                .tr = { .radius = 7_px },
                .bottom = { .width = 0_px }
            },
            .shadow = {
                .color = rgba(0, 0, 0, 0.10),
                .size = Px(-4),
                .blur = 8_px,
                .y = 1_px
            },
            .cursor = Cursor::Hand
        };

        Style TabHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(255, 255, 255, 0.40) },
            .border = {
                .color = rgba(0, 0, 0, 0.23)
            }
        };

        Style TabActive = {
            .background = { .color = rgba(255, 255, 255, 0.86) },
            .border = {
                .color = rgba(0, 0, 0, 0.30),
                .width = 1_px,
                .tl = { .radius = 7_px },
                .tr = { .radius = 7_px },
                .bottom = {
                    .color = rgba(255, 255, 255, 0.86),
                    .width = 1_px
                }
            },
            .shadow = {
                .color = rgba(0, 0, 0, 0.15),
                .size = Px(-5),
                .blur = 10_px,
                .y = 1_px
            }
        };

        Style LabelBox = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False }
        };

        Style Label = {
            .text = { .color = rgba(0, 0, 0, 0.68), .size = 13_px }
        };

        Style LabelActive = {
            .text = { .color = rgba(0, 0, 0, 0.94), .size = 13_px }
        };
    }

    struct TabView : public Box {

        Cam::App::AppState* app = nullptr;

        std::vector<std::string> names = {
            "Nut Mount",
            "Bearing Block",
            "Test Project"
        };

        std::vector<Box*> tabs;
        std::vector<Box*> labelBoxes;
        std::vector<Text*> labels;

        size_t active = 0;

        TabView(Element* parent, StyleList styles = {}) : Box(parent, styles, "TabView") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&TabViewStyle::Self);

            buildDummyTabs();
        }

        void buildDummyTabs() {

            for (size_t i = 0; i < names.size(); i++) {

                Box* tab = new Box(
                    this,
                    { &TabViewStyle::Tab, &TabViewStyle::TabHover },
                    "ProjectTab"
                );

                Box* labelBox = new Box(
                    tab,
                    { &TabViewStyle::LabelBox },
                    "ProjectTabLabelBox"
                );

                Text* label = new Text(
                    labelBox,
                    names[i],
                    { &TabViewStyle::Label }
                );

                tab->onMouseDown([this, i](Event& e) {
                    active = i;
                    refresh(e);
                    e.propagate = false;
                });

                tabs.push_back(tab);
                labelBoxes.push_back(labelBox);
                labels.push_back(label);
            }
        }

        void computeChildren(Event& e) override {

            for (size_t i = 0; i < tabs.size(); i++) {

                bool isActive = (i == active);

                if (isActive) { tabs[i]->styles.add(&TabViewStyle::TabActive); }
                else { tabs[i]->styles.remove(&TabViewStyle::TabActive); }

                if (isActive) { labels[i]->styles.add(&TabViewStyle::LabelActive); }
                else { labels[i]->styles.remove(&TabViewStyle::LabelActive); }
            }

            Box::computeChildren(e);
        }
    };
}