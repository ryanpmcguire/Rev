module;

#include <string>
#include <vector>
#include <functional>

#include <dbg.hpp>

export module Cam.Gui.ToolPathSettingsWindow;

import Rev.Window;
import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.NumberInput;
import Rev.Element.Dropdown;
import Rev.Element.Button;

import Cam.App;
import Cam.App.MaterialState;
import Cam.App.Slicer.Strategy.Strategies.Bore;
import Cam.App.Slicer.Strategy.Strategies.Profile;
import Cam.App.Slicer.Strategy.Strategies.Hatch;
import Cam.App.Tool;
import Cam.App.ToolPath;

import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolPathSettingsLayout {

        Style Root = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct, 100_pct }
        };

        Style Header = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct },
            .padding = { 22_px, 22_px, 18_px, 18_px }
        };

        Style Body = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { Grow() }
        };

        Style Row = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct }
        };

        Style RowField = {
            .size = { Grow() },
            .margin = { 0_px, 4_px, 0_px, 4_px }
        };

        Style Footer = {
            .layout = { Axis::Horizontal, Align::End, Align::Center, Wrap::False },
            .size = { 100_pct },
            .padding = { 14_px, 22_px, 20_px, 22_px }
        };

        Style FooterButton = {
            .margin = { 0_px, 0_px, 0_px, 10_px }
        };

        Style FooterButtonPrimary = {
            .size = { .width = 128_px, .height = 40_px }
        };

        Style FooterButtonSecondary = {
            .size = { .width = 96_px, .height = 40_px }
        };
    }

    struct ToolPathSettingsWindow : public Rev::Window {

        Cam::App::AppState* app = nullptr;
        Cam::App::MaterialState* state = nullptr;
        std::string stateTitle;

        Text* headerTitle = nullptr;
        Dropdown* strategyDropdown = nullptr;
        Dropdown* toolDropdown = nullptr;
        Dropdown* cutDirectionDropdown = nullptr;
        NumberInput* stepDownInput = nullptr;
        NumberInput* stepoverInput = nullptr;
        NumberInput* feedRateInput = nullptr;
        NumberInput* rapidSpeedInput = nullptr;
        NumberInput* linkRetractInput = nullptr;

        std::function<void(Event&)> onSaved;
        std::function<void(Event&)> onClosed;

        static Rev::Window* rootWindow(Element* from) {

            Element* node = from;

            while (node && node->parent && node->parent != node) {
                node = node->parent;
            }

            return static_cast<Rev::Window*>(node);
        }

        static std::vector<Dropdown::Item> toolOptions(Cam::App::AppState* app) {

            std::vector<Dropdown::Item> items;

            if (!app) {
                return items;
            }

            for (size_t i = 0; i < app->toolCount(); i++) {

                Cam::App::Tool* tool = app->toolAt(i);

                if (!tool) { continue; }

                items.push_back({ tool->name, tool->name });
            }

            if (items.empty()) {
                items.push_back({ "No tools", "" });
            }

            return items;
        }

        ToolPathSettingsWindow(
            Rev::Window* owner,
            Cam::App::MaterialState* materialState,
            const std::string& title
        ) : Rev::Window(
            owner,
            {
                .name = title + " - Toolpath Settings",
                .size = { .width = 480, .height = 500 },
                .minimizeButton = false,
                .maximizeButton = false
            }
        ) {
            state = materialState;
            stateTitle = title;

            if (owner && owner->shared) {
                shared->state = owner->shared->state;
            }

            app = Cam::App::AppState::Get(shared->state);

            style->size = { .width = 100_pct, .height = 100_pct };

            buildUi();

            setTitle(stateTitle + " - Toolpath Settings");

            if (owner) {
                setPos(owner->details.x + 240, owner->details.y + 120);
            }
            else {
                setPos(240, 120);
            }

            show();
            refresh(event);
        }

        void buildUi() {

            static const Cam::App::ToolPath emptyToolPath;

            const Cam::App::ToolPath& toolPath = state
                ? state->toolPath
                : emptyToolPath;

            Box* root = new Box(
                this,
                Theme::withSettingsDialog({ &ToolPathSettingsLayout::Root }),
                "SettingsRoot"
            );

            Box* header = new Box(
                root,
                Theme::layer(
                    { &ToolPathSettingsLayout::Header },
                    { &Theme::Styles::SettingsHeader }
                ),
                "Header"
            );

            new Text(
                header,
                "TOOLPATH",
                Theme::layer(
                    {},
                    { &Theme::Styles::SettingsHeaderEyebrow }
                )
            );

            headerTitle = new Text(
                header,
                stateTitle,
                Theme::layer(
                    {},
                    { &Theme::Styles::SettingsHeaderTitle }
                )
            );

            Box* body = new Box(
                root,
                Theme::layer(
                    { &ToolPathSettingsLayout::Body, &Theme::Styles::SettingsBody },
                    { &Theme::Styles::Text }
                ),
                "Body"
            );

            auto settingsRow = [&](const char* name) {
                return new Box(
                    body,
                    Theme::layer({ &ToolPathSettingsLayout::Row }, {}),
                    name
                );
            };

            Box* setupRow = settingsRow("SetupRow");

            strategyDropdown = new Dropdown(
                setupRow,
                {
                    .label = "Strategy",
                    .options = {
                        { "Hatch", Cam::App::Slicer::Strategy::Strategies::Hatch::name() },
                        { "Profile", Cam::App::Slicer::Strategy::Strategies::Profile::name() },
                        { "Bore", Cam::App::Slicer::Strategy::Strategies::Bore::name() }
                    },
                    .placeholder = "Select strategy",
                    .value = toolPath.strategy
                },
                { &ToolPathSettingsLayout::RowField }
            );

            toolDropdown = new Dropdown(
                setupRow,
                {
                    .label = "Tool",
                    .options = toolOptions(app),
                    .placeholder = "Select tool",
                    .value = toolPath.toolName
                },
                { &ToolPathSettingsLayout::RowField }
            );

            new Text(
                body,
                "MACHINING",
                Theme::layer(
                    {},
                    { &Theme::Styles::SettingsSectionLabel }
                )
            );

            NumberInput::Params stepDownParams;
            stepDownParams.label = "Stepdown (mm)";
            stepDownParams.placeholder = "1.0";
            stepDownParams.maxLength = 32;
            stepDownParams.selectAllOnFocus = true;
            stepDownParams.allowNegative = false;
            stepDownParams.allowDecimal = true;
            stepDownParams.allowEmpty = false;
            stepDownParams.maxDecimalPlaces = 4;

            NumberInput::Params stepoverParams;
            stepoverParams.label = "Stepover (% dia.)";
            stepoverParams.placeholder = "25";
            stepoverParams.maxLength = 32;
            stepoverParams.selectAllOnFocus = true;
            stepoverParams.allowNegative = false;
            stepoverParams.allowDecimal = true;
            stepoverParams.allowEmpty = false;
            stepoverParams.maxDecimalPlaces = 4;

            Box* machiningRow = settingsRow("MachiningRow");

            stepDownInput = new NumberInput(
                machiningRow,
                stepDownParams,
                { &ToolPathSettingsLayout::RowField }
            );

            stepoverInput = new NumberInput(
                machiningRow,
                stepoverParams,
                { &ToolPathSettingsLayout::RowField }
            );

            NumberInput::Params feedRateParams;
            feedRateParams.label = "Feed rate (mm/min)";
            feedRateParams.placeholder = "1000";
            feedRateParams.maxLength = 32;
            feedRateParams.selectAllOnFocus = true;
            feedRateParams.allowNegative = false;
            feedRateParams.allowDecimal = true;
            feedRateParams.allowEmpty = false;
            feedRateParams.maxDecimalPlaces = 4;

            Box* feedRow = settingsRow("FeedRow");

            feedRateInput = new NumberInput(
                feedRow,
                feedRateParams,
                { &ToolPathSettingsLayout::RowField }
            );

            cutDirectionDropdown = new Dropdown(
                feedRow,
                {
                    .label = "Cut direction",
                    .options = {
                        { "Climb", "climb" },
                        { "Conventional", "conventional" }
                    },
                    .placeholder = "Select direction",
                    .value = toolPath.climbMilling ? "climb" : "conventional"
                },
                { &ToolPathSettingsLayout::RowField }
            );

            new Text(
                body,
                "RAPID MOTION",
                Theme::layer(
                    {},
                    { &Theme::Styles::SettingsSectionLabel }
                )
            );

            NumberInput::Params rapidSpeedParams;
            rapidSpeedParams.label = "Rapid speed (mm/s)";
            rapidSpeedParams.placeholder = "10";
            rapidSpeedParams.maxLength = 32;
            rapidSpeedParams.selectAllOnFocus = true;
            rapidSpeedParams.allowNegative = false;
            rapidSpeedParams.allowDecimal = true;
            rapidSpeedParams.allowEmpty = false;
            rapidSpeedParams.maxDecimalPlaces = 4;

            NumberInput::Params linkRetractParams;
            linkRetractParams.label = "Link retract (mm)";
            linkRetractParams.placeholder = "10";
            linkRetractParams.maxLength = 32;
            linkRetractParams.selectAllOnFocus = true;
            linkRetractParams.allowNegative = false;
            linkRetractParams.allowDecimal = true;
            linkRetractParams.allowEmpty = false;
            linkRetractParams.maxDecimalPlaces = 4;

            Box* rapidRow = settingsRow("RapidRow");

            rapidSpeedInput = new NumberInput(
                rapidRow,
                rapidSpeedParams,
                { &ToolPathSettingsLayout::RowField }
            );

            linkRetractInput = new NumberInput(
                rapidRow,
                linkRetractParams,
                { &ToolPathSettingsLayout::RowField }
            );

            stepDownInput->setValue(toolPath.stepDown);
            stepoverInput->setValue(toolPath.stepover * 100.0);
            feedRateInput->setValue(toolPath.feedRate);
            rapidSpeedInput->setValue(toolPath.rapidSpeedMmPerSec);
            linkRetractInput->setValue(double(toolPath.linkRetractDistance));

            Box* footer = new Box(
                root,
                Theme::layer(
                    { &ToolPathSettingsLayout::Footer },
                    { &Theme::Styles::SettingsFooter }
                ),
                "Footer"
            );

            Button* cancelButton = new Button(
                footer,
                Button::Params::Secondary("Cancel"),
                Theme::layer({
                    &ToolPathSettingsLayout::FooterButton,
                    &ToolPathSettingsLayout::FooterButtonSecondary,
                    &Theme::Styles::ButtonHover,
                    &Theme::Styles::ButtonPress
                }, {
                    &Theme::Styles::Button,
                    &Theme::Styles::ButtonLabel
                })
            );

            cancelButton->onClick([this](Event& e) {
                close(&e);
                e.propagate = false;
            });

            Button* saveButton = new Button(
                footer,
                Button::Params::Primary("Save changes"),
                Theme::layer({
                    &ToolPathSettingsLayout::FooterButton,
                    &ToolPathSettingsLayout::FooterButtonPrimary,
                    &Theme::Styles::AccentButtonHover
                }, {
                    &Theme::Styles::AccentButton,
                    &Theme::Styles::AccentButtonLabel
                })
            );

            saveButton->onClick([this](Event& e) {
                save(e);
                e.propagate = false;
            });
        }

        void save(Event& e) {

            if (!app || !state) {
                dbg("[ToolPathSettings] Missing app state or material state");
                return;
            }

            const std::string toolName = toolDropdown->params.value;

            if (toolName.empty()) {
                dbg("[ToolPathSettings] Tool is required");
                return;
            }

            stepDownInput->commit(e);
            stepoverInput->commit(e);
            feedRateInput->commit(e);
            rapidSpeedInput->commit(e);
            linkRetractInput->commit(e);

            double stepDown = 0.0;
            double stepoverPercent = 0.0;
            double feedRate = 0.0;
            double rapidSpeed = 0.0;
            double linkRetract = 0.0;

            if (!stepDownInput->tryGetValue(stepDown) || stepDown <= 0.0) {
                dbg("[ToolPathSettings] Invalid stepdown");
                return;
            }

            if (!stepoverInput->tryGetValue(stepoverPercent) || stepoverPercent <= 0.0) {
                dbg("[ToolPathSettings] Invalid stepover");
                return;
            }

            if (!feedRateInput->tryGetValue(feedRate) || feedRate <= 0.0) {
                dbg("[ToolPathSettings] Invalid feed rate");
                return;
            }

            if (!rapidSpeedInput->tryGetValue(rapidSpeed) || rapidSpeed <= 0.0) {
                dbg("[ToolPathSettings] Invalid rapid speed");
                return;
            }

            if (!linkRetractInput->tryGetValue(linkRetract) || linkRetract <= 0.0) {
                dbg("[ToolPathSettings] Invalid link retract height");
                return;
            }

            const std::string strategy = strategyDropdown->params.value;
            const bool climbMilling = cutDirectionDropdown->params.value != "conventional";

            if (!app->saveToolPathSettings(
                state,
                strategy,
                toolName,
                stepDown,
                stepoverPercent / 100.0,
                feedRate,
                rapidSpeed,
                climbMilling,
                static_cast<float>(linkRetract)
            )) {
                dbg("[ToolPathSettings] Failed to save toolpath settings");
                return;
            }

            dbg("[ToolPathSettings] Saved toolpath settings for \"%s\"", stateTitle.c_str());

            if (onSaved) {
                onSaved(e);
            }

            refresh(e);
        }

        void notifyClosed(Event& e) {

            if (onClosed) {
                onClosed(e);
            }
        }

        void close(Event* event = nullptr) {
            shouldClose = true;

            if (event) {
                notifyClosed(*event);
            }
        }
    };
}
