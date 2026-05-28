module;

#include <string>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.Tool;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

import Cam.App;
import Cam.App.Tool;
import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolRowStyle::Styles {

        Style Self = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .bottom = 2_px },
            .padding = { 4_px, 8_px, 4_px, 8_px },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };

        Style Label = {
            .size = { .width = Grow() },
            .text = { .size = 13_px }
        };

        Style SettingsButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { 0_px, 0_px, 0_px, 2_px },
            .padding = { 3_px, 4_px, 3_px, 4_px },
            .cursor = Cursor::Hand
        };

        Style SettingsIcon = {
            .size = { 15_px, 15_px }
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
            this->styles.add(&Theme::Styles::Row);
            this->styles.add(&Theme::Styles::RowHover);

            label = new Text(
                this,
                "",
                Theme::layer(
                    { &Styles::Label },
                    { &Theme::Styles::Text }
                )
            );

            label->onMouseDown([this](Event& e) {
                if (onSelect) { onSelect(e, toolIndex); }
            });

            settingsButton = new Box(
                this,
                { &Styles::SettingsButton },
                "ToolSettingsButton"
            );

            new Svg(
                settingsButton,
                File("./Settings.svg"),
                Theme::layer({
                    &Styles::SettingsIcon,
                    &Theme::Styles::IconHover
                }, {
                    &Theme::Styles::Icon
                }),
                "ToolSettingsIcon"
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
                styles.add(&Theme::Styles::RowSelected);
            }

            else {
                styles.remove(&Theme::Styles::RowSelected);
            }

            Box::computeChildren(e);
        }
    };
}
