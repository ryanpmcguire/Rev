module;

#include <string>
#include <functional>

export module Cam.Gui.Tool;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;

import Cam.App;
import Cam.App.Tool;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolRowStyle::Styles {

        Style Self = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .bottom = 2_px },
            .padding = { 4_px, 8_px, 4_px, 8_px },
            .background = { .color = rgba(203, 213, 223, 0.0), .transition = 100_ms },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };

        Style Hover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(203, 213, 223, 1.0) }
        };

        Style Selected = {
            .background = { .color = rgba(109, 119, 255, 0.22) },
            .border = { .color = rgba(109, 119, 255, 1.0), .width = 1_px }
        };

        Style Label = {
            .size = { .width = Grow() },
            .text = { .color = rgba(0, 0, 0, 0.85), .size = 13_px }
        };

        Style SettingsButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { 0_px, 0_px, 0_px, 2_px },
            .padding = { 3_px, 5_px, 3_px, 5_px },
            .background = { .color = rgba(255, 255, 255, 0.2), .transition = 100_ms },
            .border = { .color = rgba(0, 0, 0, 0.15), .width = 1_px, .radius = 4_px },
            .cursor = Cursor::Hand
        };

        Style SettingsButtonHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(255, 255, 255, 0.55) }
        };

        Style SettingsButtonLabel = {
            .text = { .color = rgba(0, 0, 0, 0.7), .size = 11_px }
        };
    };

    using namespace ToolRowStyle;

    struct ToolRow : public Box {

        Cam::App::AppState* app = nullptr;
        size_t toolIndex = 0;

        Text* label = nullptr;
        Box* settingsButton = nullptr;

        std::function<void(Event&, size_t)> onSelect;
        std::function<void(Event&, size_t)> onOpenSettings;

        ToolRow(Element* parent, StyleList styles = {}) : Box(parent, styles, "ToolRow") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&Styles::Self);
            this->styles.add(&Styles::Hover);

            label = new Text(this, "", { &Styles::Label });

            label->onMouseDown([this](Event& e) {
                if (onSelect) { onSelect(e, toolIndex); }
            });

            settingsButton = new Box(
                this,
                {
                    &Styles::SettingsButton,
                    &Styles::SettingsButtonHover
                },
                "ToolSettingsButton"
            );

            new Text(
                settingsButton,
                "3",
                { &Styles::SettingsButtonLabel }
            );

            settingsButton->onClick([this](Event& e) {
                if (onOpenSettings) {
                    onOpenSettings(e, toolIndex);
                }
                e.propagate = false;
            });
        }

        void setToolIndex(size_t index) {
            toolIndex = index;
        }

        void setLabel(const std::string& text) {

            if (label) {
                label->content = text;
            }
        }

        bool isSelected() const {

            if (!app || !app->activeProject) {
                return false;
            }

            Cam::App::Tool* tool = app->toolAt(toolIndex);

            if (!tool) {
                return false;
            }

            return app->activeProject->selectedToolName == tool->name;
        }

        void computeChildren(Event& e) override {

            if (isSelected()) {
                styles.add(&Styles::Selected);
            }

            else {
                styles.remove(&Styles::Selected);
            }

            Box::computeChildren(e);
        }
    };
}
