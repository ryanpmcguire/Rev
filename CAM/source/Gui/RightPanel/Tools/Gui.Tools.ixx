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
import Cam.Gui.ToolSettingsWindow;

import Rev.Window;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolsStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .height = Grow() },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            .zIndex = +1
        };

        Style Title = {
            .size = { 100_pct },
            .margin = { .bottom = 8_px },
            .text = { .color = rgba(0, 0, 0, 0.65), .size = 13_px }
        };

        Style NewToolButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct, .height = 34_px },
            .margin = { 0_px, 0_px, 0_px, 8_px },
            .padding = { 10_px, 10_px, 3_px, 2_px },
            .background = { .color = rgba(255, 255, 255, 0.24), .transition = 100_ms },
            .border = {
                .color = rgba(0, 0, 0, 0.22),
                .width = 1_px,
                .radius = 5_px
            },
            .cursor = Cursor::Hand
        };

        Style NewToolButtonHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(255, 255, 255, 0.42) },
            .border = {
                .color = rgba(0, 0, 0, 0.32)
            }
        };

        Style NewToolButtonPress = {
            .applies = { .press = true },
            .background = { .color = rgba(255, 255, 255, 0.56) },
            .border = {
                .color = rgba(0, 0, 0, 0.42)
            }
        };

        Style NewToolButtonLabel = {
            .text = {
                .color = rgba(0, 0, 0, 0.74),
                .size = 13_px
            }
        };

        Style NewToolButtonIcon = {
            .size = { 14_px, 14_px },
            .margin = { 0_px, 6_px, 0_px, 0_px },
            .text = { .color = rgba(0, 0, 0, 0.55), .transition = 100_ms }
        };

        Style NewToolButtonIconHover = {
            .applies = { .hover = true, .focus = true },
            .text = { .color = rgba(0, 0, 0, 0.90) }
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

        ToolSettingsWindow* settingsWindow = nullptr;

        std::function<void(Event&)> onSelectTool;
        std::function<void(Event&)> onToolEdited;

        Tools(Element* parent, StyleList styles = {}) : Box(parent, styles, "Tools") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&ToolsStyle::Self);

            title = new Text(this, "Tools", { &ToolsStyle::Title });

            newToolButton = new Box(
                this,
                {
                    &ToolsStyle::NewToolButton,
                    &ToolsStyle::NewToolButtonHover,
                    &ToolsStyle::NewToolButtonPress
                },
                "NewToolButton"
            );

            newToolButtonLabel = new Text(
                newToolButton,
                "New Tool",
                { &ToolsStyle::NewToolButtonLabel }
            );

            newToolButtonIcon = new Svg(
                newToolButton,
                File("./New-Tool.svg"),
                {
                    &ToolsStyle::NewToolButtonIcon,
                    &ToolsStyle::NewToolButtonIconHover
                },
                "NewToolButtonIcon"
            );

            newToolButton->moveChild(newToolButtonIcon, newToolButtonLabel, true);

            newToolButton->onClick([this](Event& e) {
                openNewToolSettings(e);
                e.propagate = false;
            });

            list = new Box(this, { &ToolsStyle::List }, "ToolsList");
        }

        ~Tools() {
            closeSettingsWindow();
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

        void closeSettingsWindow() {

            if (!settingsWindow) {
                return;
            }

            settingsWindow->shouldClose = true;
            settingsWindow = nullptr;
        }

        void openSettingsWindow(const std::string& name, Event& e) {

            Rev::Window* owner = ToolSettingsWindow::rootWindow(this);

            if (!owner || !owner->shared) { return; }

            std::vector<void*>* windowGroup = owner->shared->windowGroup;

            if (!windowGroup) { return; }

            closeSettingsWindow();

            settingsWindow = new ToolSettingsWindow(
                *windowGroup,
                owner,
                name
            );

            settingsWindow->onSaved = [this](Event& savedEvent) {

                if (onToolEdited) {
                    onToolEdited(savedEvent);
                }

                refresh(savedEvent);
            };

            settingsWindow->onClosed = [this](Event& closedEvent) {

                if (onToolEdited) {
                    onToolEdited(closedEvent);
                }

                refresh(closedEvent);
            };
        }

        void openSettings(size_t index, Event& e) {

            if (!app) { return; }

            Cam::App::Tool* tool = app->toolAt(index);

            if (!tool) { return; }

            openSettingsWindow(tool->name, e);
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

            refresh(e);

            openSettingsWindow(toolName, e);
        }

        void computeChildren(Event& e) override {

            if (settingsWindow && settingsWindow->shouldClose) {
                settingsWindow = nullptr;
            }

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

                rows[i]->onOpenSettings = [this](Event& ev, size_t index) {
                    this->openSettings(index, ev);
                };
            }

            for (size_t i = 0; i < newSize; i++) {

                Cam::App::Tool* tool = app->toolAt(i);

                rows[i]->setToolIndex(i);

                if (tool) {
                    rows[i]->setLabel(tool->name);
                }

                else {
                    rows[i]->setLabel("Invalid Tool");
                }
            }

            Box::computeChildren(e);
        }
    };
}
