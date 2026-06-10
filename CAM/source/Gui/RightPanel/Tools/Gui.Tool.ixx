module;

#include <string>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.Tool;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;
import Rev.Window;

import Cam.App;
import Cam.App.Tool;
import Cam.Gui.Theme;
import Cam.Gui.ToolSettingsWindow;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolRowStyle::Styles {

        Style Self = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .bottom = 2_px },
            .padding = { .left = 8_px, .right = 8_px, .top = 4_px, .bottom = 4_px },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };

        Style Label = {
            .size = { .width = Grow() },
            .text = { .size = 13_px }
        };

        Style SettingsButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { .left = 2_px },
            .padding = { .left = 4_px, .right = 4_px, .top = 3_px, .bottom = 3_px },
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

        // This row owns its own settings window (if any), so multiple tools can
        // have settings windows open at once. The Application owns the lifetime;
        // we keep only a non-owning pointer.
        ToolSettingsWindow* settingsWindow = nullptr;

        std::function<void(Event&, size_t)> onSelect;
        std::function<void(Event&)> onSettingsChanged;

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
                openSettings(e);
                e.propagate = false;
            });
        }

        ~ToolRow() {
            retireSettingsWindow();
        }

        void setToolIndex(size_t index) {

            // Reassigning this row to a different tool retires its window.
            if (settingsWindow && index != toolIndex) {
                retireSettingsWindow();
            }

            toolIndex = index;
        }

        // Settings window
        //--------------------------------------------------

        void retireSettingsWindow() {

            if (!settingsWindow) { return; }

            settingsWindow->onSaved = nullptr;
            settingsWindow->onClosed = nullptr;
            settingsWindow->shouldClose = true;
            settingsWindow = nullptr;
        }

        void openSettings(Event& e) {

            if (!app) { return; }

            Cam::App::Tool* tool = app->toolAt(toolIndex);

            if (!tool) { return; }

            // Already open — just bring it forward.
            if (settingsWindow && !settingsWindow->shouldClose) {
                settingsWindow->show();
                return;
            }

            Rev::Window* owner = ToolSettingsWindow::rootWindow(this);

            if (!owner || !owner->shared) { return; }

            settingsWindow = new ToolSettingsWindow(owner, tool->name);

            settingsWindow->onSaved = [this](Event& ev) {
                if (onSettingsChanged) { onSettingsChanged(ev); }
            };

            settingsWindow->onClosed = [this](Event& ev) {
                settingsWindow = nullptr;
                if (onSettingsChanged) { onSettingsChanged(ev); }
            };
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

            // Drop our pointer if the window closed itself (OS close button).
            if (settingsWindow && settingsWindow->shouldClose) {
                settingsWindow = nullptr;
            }

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
