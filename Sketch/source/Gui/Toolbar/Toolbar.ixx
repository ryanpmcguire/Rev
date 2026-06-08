module;

#include <string>
#include <vector>
#include <functional>

#include <managed.hpp>

export module Sketch.Gui.Toolbar;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Svg;

import Sketch.App;
import Sketch.Gui.Theme;

export namespace Sketch::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolbarStyle {

        Style Self = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct, .height = 46_px },
            .padding = { 8_px, 8_px, 4_px, 4_px }
        };

        // Medium-size, square-ish tool buttons.
        Style Button = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False, CrossAlign::True },
            .size = { 34_px, 34_px },
            .margin = { .right = 4_px },
            .background = { .color = rgba(0, 0, 0, 0.0), .transition = 100_ms },
            .border = { .color = rgba(0, 0, 0, 0.0), .width = 1_px, .radius = 8_px },
            .cursor = Cursor::Hand
        };

        // Engaged (selected) tool: a lifted, outlined surface.
        Style ButtonActive = {
            .border = { .width = 1_px }
        };

        Style Icon = {
            .size = { 20_px, 20_px }
        };
    }

    struct ToolButton : public Box {

        Sketch::App::AppState* app = nullptr;
        Sketch::App::SketchTool tool = Sketch::App::SketchTool::None;

        Svg* icon = nullptr;

        std::function<void(Event&)> onSelect;

        ToolButton(
            Element* parent,
            Sketch::App::AppState* app,
            Sketch::App::SketchTool tool,
            Rev::Core::Resource iconResource
        ) : Box(
            parent,
            Theme::layer({
                &ToolbarStyle::Button,
                &Theme::Styles::ChromeHover
            }, {}),
            "ToolButton"
        ) {
            this->app = app;
            this->tool = tool;

            icon = new Svg(
                this,
                iconResource,
                Theme::layer({
                    &ToolbarStyle::Icon,
                    &Theme::Styles::IconHover
                }, {
                    &Theme::Styles::Icon
                }),
                "ToolIcon"
            );

            onClick([this](Event& e) {

                if (!this->app) { return; }

                this->app->selectTool(this->tool);

                if (this->onSelect) { this->onSelect(e); }

                refresh(e);
                e.propagate = false;
            });
        }

        bool isActive() const {
            return app && app->activeTool == tool;
        }

        // Appearance only: reflect the selected tool.
        void computeStyle(Event& e) override {

            if (isActive()) {
                styles.add(&Theme::Styles::Button);
                styles.add(&ToolbarStyle::ButtonActive);
                if (icon) { icon->styles.add(&Theme::Styles::AccentText); }
            }
            else {
                styles.remove(&Theme::Styles::Button);
                styles.remove(&ToolbarStyle::ButtonActive);
                if (icon) { icon->styles.remove(&Theme::Styles::AccentText); }
            }

            Box::computeStyle(e);
        }
    };

    struct Toolbar : public Box {

        Sketch::App::AppState* app = nullptr;

        std::vector<ToolButton*> buttons;

        std::function<void(Event&)> onSelectTool;

        Toolbar(Element* parent, StyleList styles = {}) : Box(parent, styles, "Toolbar") {

            app = Sketch::App::AppState::Get(shared->state);

            this->styles.add(&ToolbarStyle::Self);
            this->styles.add(&Theme::Styles::TabBar);

            using Sketch::App::SketchTool;

            addTool(SketchTool::Point,      File("./Point.svg"));
            addTool(SketchTool::Line,       File("./Line.svg"));
            addTool(SketchTool::Arc,        File("./Arc.svg"));
            addTool(SketchTool::Circle,     File("./Circle.svg"));
            addTool(SketchTool::Box,        File("./Box.svg"));
            addTool(SketchTool::Ellipse,    File("./Ellipse.svg"));
            addTool(SketchTool::EllipseArc, File("./EllipseArc.svg"));
        }

        void addTool(Sketch::App::SketchTool tool, Rev::Core::Resource iconResource) {

            ToolButton* button = new ToolButton(this, app, tool, iconResource);

            button->onSelect = [this](Event& e) {
                if (onSelectTool) { onSelectTool(e); }
            };

            buttons.push_back(button);
        }
    };
}
