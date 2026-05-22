module;

#include <string>
#include <vector>
#include <cstdio>
#include <functional>
#include <stdexcept>

#include <dbg.hpp>

export module Cam.Gui.ToolSettingsWindow;

import Rev.Window;
import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.TextInput;

import Cam.App;
import Cam.App.Tool;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolSettingsStyle {

        Shadow panelShadow = {
            .color = rgba(0, 0, 0, 0.4),
            .size = Px(-6),
            .blur = 14_px
        };

        Style Root = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct, 100_pct },
            .padding = { 12_px, 12_px, 12_px, 12_px },
            .background = { .color = rgba(240, 242, 248, 1.0) },
            .border = { .radius = 8_px },
            .shadow = panelShadow
        };

        Style Title = {
            .size = { 100_pct },
            .margin = { .bottom = 10_px },
            .text = { .color = rgba(0, 0, 0, 0.85), .size = 16_px }
        };

        Style FieldLabel = {
            .margin = { .bottom = 4_px },
            .text = { .color = rgba(0, 0, 0, 0.6), .size = 12_px }
        };

        Style Actions = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { 100_pct },
            .margin = { 12_px, 0_px, 0_px, 0_px }
        };

        Style Button = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = Grow(), .height = 34_px },
            .margin = { 0_px, 0_px, 0_px, 6_px },
            .padding = { 10_px, 10_px, 3_px, 2_px },
            .background = { .color = rgba(255, 255, 255, 0.5), .transition = 100_ms },
            .border = { .color = rgba(0, 0, 0, 0.18), .width = 1_px, .radius = 5_px },
            .cursor = Cursor::Hand
        };

        Style ButtonHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(255, 255, 255, 0.85) }
        };

        Style ButtonPrimary = {
            .background = { .color = rgba(109, 119, 255, 0.35) },
            .border = { .color = rgba(109, 119, 255, 0.9) }
        };

        Style ButtonLabel = {
            .text = { .color = rgba(0, 0, 0, 0.8), .size = 13_px }
        };
    }

    struct ToolSettingsWindow : public Rev::Window {

        Cam::App::AppState* app = nullptr;
        std::string toolName;

        Text* titleText = nullptr;
        TextInput* diameterInput = nullptr;
        TextInput* lengthInput = nullptr;

        std::function<void(Event&)> onSaved;

        static Rev::Window* rootWindow(Element* from) {

            Element* node = from;

            while (node && node->parent && node->parent != node) {
                node = node->parent;
            }

            return static_cast<Rev::Window*>(node);
        }

        static std::string formatNumber(double value) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.4f", value);

            std::string result = buffer;

            while (!result.empty() && result.back() == '0') {
                result.pop_back();
            }

            if (!result.empty() && result.back() == '.') {
                result.pop_back();
            }

            if (result.empty()) {
                return "0";
            }

            return result;
        }

        static bool parseNumber(
            const std::string& text,
            double& out
        ) {
            if (text.empty()) {
                return false;
            }

            try {
                out = std::stod(text);
                return true;
            }

            catch (...) {
                return false;
            }
        }

        ToolSettingsWindow(
            std::vector<void*>& windowGroup,
            Rev::Window* owner,
            const std::string& toolName
        ) : Rev::Window(
            windowGroup,
            {
                .name = "Tool Settings",
                .size = { .width = 340, .height = 300 },
                //.borderless = true
            }
        ) {
            this->toolName = toolName;

            if (owner && owner->shared) {
                shared->state = owner->shared->state;
            }

            app = Cam::App::AppState::Get(shared->state);

            this->style->layout = {
                Axis::Vertical, Align::Start, Align::Start, Wrap::False
            };
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->styles.add(&ToolSettingsStyle::Root);

            buildUi();

            if (owner) {
                setPos(
                    owner->details.x + 280,
                    owner->details.y + 96
                );
            }

            else {
                setPos(280, 96);
            }

            show();
            refresh(event);
        }

        void buildUi() {

            Cam::App::Tool* tool = nullptr;

            if (app) {
                tool = app->toolLibrary()->find(toolName);
            }

            std::string title = "Tool Settings";

            if (tool) {
                title = tool->name;
            }

            titleText = new Text(this, title, { &ToolSettingsStyle::Title });

            diameterInput = new TextInput(
                this,
                {
                    .label = "Diameter (mm)",
                    .placeholder = "1.0",
                    .maxLength = 32,
                    .selectAllOnFocus = true
                }
            );

            lengthInput = new TextInput(
                this,
                {
                    .label = "Length (mm)",
                    .placeholder = "100",
                    .maxLength = 32,
                    .selectAllOnFocus = true
                }
            );

            if (tool) {
                diameterInput->text->content =
                    formatNumber(tool->diameter);
                lengthInput->text->content =
                    formatNumber(tool->length);
            }

            Box* actions = new Box(
                this,
                { &ToolSettingsStyle::Actions },
                "ToolSettingsActions"
            );

            Box* saveButton = new Box(
                actions,
                {
                    &ToolSettingsStyle::Button,
                    &ToolSettingsStyle::ButtonHover,
                    &ToolSettingsStyle::ButtonPrimary
                },
                "ToolSettingsSave"
            );

            new Text(
                saveButton,
                "Save",
                { &ToolSettingsStyle::ButtonLabel }
            );

            saveButton->onClick([this](Event& e) {
                this->save(e);
                e.propagate = false;
            });

            Box* closeButton = new Box(
                actions,
                {
                    &ToolSettingsStyle::Button,
                    &ToolSettingsStyle::ButtonHover
                },
                "ToolSettingsClose"
            );

            new Text(
                closeButton,
                "Close",
                { &ToolSettingsStyle::ButtonLabel }
            );

            closeButton->onClick([this](Event& e) {
                this->close();
                e.propagate = false;
            });
        }

        void save(Event& e) {

            if (!app) { return; }

            double diameter = 0.0;
            double length = 0.0;

            if (!parseNumber(diameterInput->text->content.get(), diameter)) {
                dbg("[ToolSettings] Invalid diameter");
                return;
            }

            if (!parseNumber(lengthInput->text->content.get(), length)) {
                dbg("[ToolSettings] Invalid length");
                return;
            }

            if (diameter <= 0.0 || length <= 0.0) {
                dbg("[ToolSettings] Diameter and length must be positive");
                return;
            }

            if (!app->saveTool(toolName, diameter, length)) {
                dbg("[ToolSettings] Failed to save tool");
                return;
            }

            dbg("[ToolSettings] Saved \"%s\"", toolName.c_str());

            if (onSaved) {
                onSaved(e);
            }

            refresh(e);
        }

        void close() {
            shouldClose = true;
        }

        void onClose(bool& rejectClose) override {
            rejectClose = false;
            shouldClose = true;
        }
    };
}
