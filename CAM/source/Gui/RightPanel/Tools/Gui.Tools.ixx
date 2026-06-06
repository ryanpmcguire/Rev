module;

#include <string>
#include <vector>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.Tools;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

import Cam.App;
import Cam.App.Tool;

import Cam.Gui.Tool;
import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolsStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .height = Grow() },
            .margin = { 2_px, 2_px, 2_px, 2_px },
            .padding = { .left = 6_px, .right = 6_px, .top = 4_px, .bottom = 4_px },
            //.zIndex = +1
        };

        Style Title = {
            .size = { 100_pct },
            .margin = { .bottom = 4_px },
            .text = { .size = 12_px }
        };

        Style NewToolButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .bottom = 4_px },
            .padding = { .left = 10_px, .right = 10_px, .top = 6_px, .bottom = 6_px },
            .border = {
                .width = 1_px,
                .radius = 5_px
            },
            .cursor = Cursor::Hand
        };

        Style NewToolButtonLabel = {
            .text = { .size = 12_px }
        };

        Style NewToolButtonIcon = {
            .size = { 13_px, 13_px },
            .margin = { 0_px, 5_px, 0_px, 0_px }
        };

        Style List = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct },
            .overflow = Overflow::Hide
        };
    };

    using namespace ToolsStyle;

    struct Tools : public Box {

        Cam::App::AppState* app = nullptr;

        Text* title = nullptr;
        Box* newToolButton = nullptr;
        Svg* newToolButtonIcon = nullptr;
        Text* newToolButtonLabel = nullptr;
        Box* list = nullptr;

        std::vector<ToolRow*> rows;

        // Name of a just-created tool whose settings window should be opened once
        // its row exists (rows own their own windows).
        std::string pendingSettingsToolName;

        std::function<void(Event&)> onSelectTool;
        std::function<void(Event&)> onToolEdited;

        Tools(Element* parent, StyleList styles = {}) : Box(parent, styles, "Tools") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&ToolsStyle::Self);

            title = new Text(
                this,
                "Tools",
                Theme::withMutedText({ &ToolsStyle::Title })
            );

            newToolButton = new Box(
                this,
                Theme::withButton({
                    &ToolsStyle::NewToolButton,
                    &Theme::Styles::ButtonHover,
                    &Theme::Styles::ButtonPress
                }),
                "NewToolButton"
            );

            newToolButtonLabel = new Text(
                newToolButton,
                "New Tool",
                Theme::layer(
                    { &ToolsStyle::NewToolButtonLabel },
                    { &Theme::Styles::ButtonLabel }
                )
            );

            newToolButtonIcon = new Svg(
                newToolButton,
                File("./New-Tool.svg"),
                Theme::layer({
                    &ToolsStyle::NewToolButtonIcon,
                    &Theme::Styles::ChromeIconHover
                }, {
                    &Theme::Styles::ChromeIcon
                }),
                "NewToolButtonIcon"
            );

            newToolButton->moveChild(newToolButtonIcon, newToolButtonLabel, true);

            newToolButton->onClick([this](Event& e) {
                openNewToolSettings(e);
                e.propagate = false;
            });

            list = new Box(this, { &ToolsStyle::List }, "ToolsList");
        }

        void selectTool(size_t index, Event& e) {

            if (!app) { return; }

            if (app->selectTool(index)) {

                if (onSelectTool) {
                    onSelectTool(e);
                }

                refresh(e);
            }
        }

        void openNewToolSettings(Event& e) {

            if (!app) { return; }

            std::string toolName;

            if (!app->createNewTool(toolName)) {
                return;
            }

            if (onSelectTool) {
                onSelectTool(e);
            }

            // The new tool's row is created on the next layout pass; ask it to
            // open its own settings window once it exists (see computeChildren).
            pendingSettingsToolName = toolName;

            refresh(e);
        }

        void computeChildren(Event& e) override {

            if (!app) {
                Box::computeChildren(e);
                return;
            }

            size_t oldSize = rows.size();
            size_t newSize = app->toolCount();

            for (size_t i = newSize; i < oldSize; i++) {
                delete rows[i];
            }

            rows.resize(newSize);

            for (size_t i = oldSize; i < newSize; i++) {

                rows[i] = new ToolRow(list);

                rows[i]->onSelect = [this](Event& ev, size_t index) {
                    this->selectTool(index, ev);
                };

                // Saving in / closing a tool's settings window refreshes tools.
                rows[i]->onSettingsChanged = [this](Event& ev) {
                    if (onToolEdited) { onToolEdited(ev); }
                };
            }

            for (size_t i = 0; i < newSize; i++) {

                Cam::App::Tool* tool = app->toolAt(i);

                rows[i]->setToolIndex(i);

                if (tool) {
                    rows[i]->setLabel(
                        "#" + std::to_string(i + 1) + "  " + tool->name
                    );
                }
                else {
                    rows[i]->setLabel("Invalid Tool");
                }
            }

            // A freshly-created tool asked to open its settings — now that its
            // row exists, delegate to the row (which owns the window).
            if (!pendingSettingsToolName.empty()) {

                for (size_t i = 0; i < newSize; i++) {

                    Cam::App::Tool* tool = app->toolAt(i);

                    if (tool && tool->name == pendingSettingsToolName) {
                        rows[i]->openSettings(e);
                        break;
                    }
                }

                pendingSettingsToolName.clear();
            }

            Box::computeChildren(e);
        }
    };
}
