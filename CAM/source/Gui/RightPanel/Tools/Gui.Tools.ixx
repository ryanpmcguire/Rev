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
import Cam.Gui.Theme;

import Rev.Window;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolsStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .height = Grow() },
            .margin = { 2_px, 2_px, 2_px, 2_px },
            .padding = { 4_px, 6_px, 4_px, 6_px },
            .zIndex = +1
        };

        Style Title = {
            .size = { 100_pct },
            .margin = { .bottom = 4_px },
            .text = { .size = 12_px }
        };

        Style NewToolButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { 0_px, 0_px, 0_px, 4_px },
            .padding = { 6_px, 10_px, 6_px, 10_px },
            .border = {
                .radius = 5_px,
                .width = 1_px
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
            .overflow = Overflow::Hide,
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct }
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

            closeSettingsWindow();

            settingsWindow = new ToolSettingsWindow(
                owner,
                name
            );

            settingsWindow->onSaved = [this](Event& e) {
                if (onToolEdited) { onToolEdited(e); }
            };

            settingsWindow->onClosed = [this](Event& e) {
                if (onToolEdited) { onToolEdited(e); }
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
