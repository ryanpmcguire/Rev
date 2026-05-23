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
import Cam.App.Slicer.Strategy.StrategyType;
import Cam.App.Tool;
import Cam.App.ToolPath;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolPathSettingsStyle {

        Shadow panelShadow = {
            .color = rgba(15, 23, 42, 0.14),
            .size = Px(-8),
            .blur = 28_px
        };

        Style Root = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct, 100_pct },
            .background = { .color = rgba(246, 247, 251, 1.0) },
            .border = { .radius = 10_px },
            .shadow = panelShadow
        };

        Style Header = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct },
            .padding = { 22_px, 22_px, 18_px, 18_px },
            .background = { .color = rgba(28, 34, 48, 1.0) }
        };

        Style HeaderEyebrow = {
            .text = { .color = rgba(148, 163, 184, 1.0), .size = 11_px }
        };

        Style HeaderTitle = {
            .margin = { 6_px, 0_px, 0_px, 0_px },
            .text = { .color = rgba(248, 250, 252, 1.0), .size = 21_px }
        };

        Style Body = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { Grow() },
            .padding = { 20_px, 22_px, 8_px, 22_px }
        };

        Style SectionLabel = {
            .margin = { 4_px, 0_px, 10_px, 0_px },
            .text = { .color = rgba(100, 116, 139, 1.0), .size = 11_px }
        };

        Style Footer = {
            .layout = { Axis::Horizontal, Align::End, Align::Center, Wrap::False },
            .size = { 100_pct },
            .padding = { 14_px, 22_px, 20_px, 22_px },
            .background = { .color = rgba(255, 255, 255, 1.0) },
            .border = {
                .top = {
                    .color = rgba(226, 232, 240, 1.0),
                    .width = 1_px
                }
            }
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
        NumberInput* stepDownInput = nullptr;
        NumberInput* stepoverInput = nullptr;
        NumberInput* feedRateInput = nullptr;

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
                .size = { .width = 400, .height = 520 },
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

            style->layout = {
                Axis::Vertical, Align::Start, Align::Start, Wrap::False
            };
            style->size = { .width = 100_pct, .height = 100_pct };
            styles.add(&ToolPathSettingsStyle::Root);

            buildUi();

            setTitle(stateTitle + " - Toolpath Settings");

            if (owner) {
                setPos(owner->details.x + 240, owner->details.y + 120);
            } else {
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

            Box* header = new Box(
                this,
                { &ToolPathSettingsStyle::Header },
                "Header"
            );

            new Text(
                header,
                "TOOLPATH",
                { &ToolPathSettingsStyle::HeaderEyebrow }
            );

            headerTitle = new Text(
                header,
                stateTitle,
                { &ToolPathSettingsStyle::HeaderTitle }
            );

            Box* body = new Box(
                this,
                { &ToolPathSettingsStyle::Body },
                "Body"
            );

            strategyDropdown = new Dropdown(
                body,
                {
                    .label = "Strategy",
                    .options = {
                        {
                            Cam::App::Slicer::Strategy::strategyTypeDisplayName(
                                Cam::App::Slicer::Strategy::StrategyType::Hatch
                            ),
                            "Hatch"
                        },
                        {
                            Cam::App::Slicer::Strategy::strategyTypeDisplayName(
                                Cam::App::Slicer::Strategy::StrategyType::Profile
                            ),
                            "Profile"
                        },
                        {
                            Cam::App::Slicer::Strategy::strategyTypeDisplayName(
                                Cam::App::Slicer::Strategy::StrategyType::Bore
                            ),
                            "Bore"
                        }
                    },
                    .placeholder = "Select strategy",
                    .value = Cam::App::Slicer::Strategy::strategyTypeToString(toolPath.strategy)
                }
            );

            toolDropdown = new Dropdown(
                body,
                {
                    .label = "Tool",
                    .options = toolOptions(app),
                    .placeholder = "Select tool",
                    .value = toolPath.toolName
                }
            );

            new Text(
                body,
                "MACHINING",
                { &ToolPathSettingsStyle::SectionLabel }
            );

            NumberInput::Params stepDownParams;
            stepDownParams.label = "Stepdown";
            stepDownParams.placeholder = "1.0";
            stepDownParams.maxLength = 32;
            stepDownParams.selectAllOnFocus = true;
            stepDownParams.allowNegative = false;
            stepDownParams.allowDecimal = true;
            stepDownParams.allowEmpty = false;
            stepDownParams.maxDecimalPlaces = 4;

            stepDownInput = new NumberInput(body, stepDownParams);

            NumberInput::Params feedRateParams;
            NumberInput::Params stepoverParams;

            stepoverParams.label = "Stepover (% diameter)";
            stepoverParams.placeholder = "25";
            stepoverParams.maxLength = 32;
            stepoverParams.selectAllOnFocus = true;
            stepoverParams.allowNegative = false;
            stepoverParams.allowDecimal = true;
            stepoverParams.allowEmpty = false;
            stepoverParams.maxDecimalPlaces = 4;

            stepoverInput = new NumberInput(body, stepoverParams);

            feedRateParams.label = "Feed rate";
            feedRateParams.placeholder = "1000";
            feedRateParams.maxLength = 32;
            feedRateParams.selectAllOnFocus = true;
            feedRateParams.allowNegative = false;
            feedRateParams.allowDecimal = true;
            feedRateParams.allowEmpty = false;
            feedRateParams.maxDecimalPlaces = 4;

            feedRateInput = new NumberInput(body, feedRateParams);

            stepDownInput->setValue(toolPath.stepDown);
            stepoverInput->setValue(toolPath.stepover * 100.0);
            feedRateInput->setValue(toolPath.feedRate);

            Box* footer = new Box(
                this,
                { &ToolPathSettingsStyle::Footer },
                "Footer"
            );

            Button* cancelButton = new Button(
                footer,
                Button::Params::Secondary("Cancel"),
                {
                    &ToolPathSettingsStyle::FooterButton,
                    &ToolPathSettingsStyle::FooterButtonSecondary
                }
            );

            cancelButton->onClick([this](Event& e) {
                close(&e);
                e.propagate = false;
            });

            Button* saveButton = new Button(
                footer,
                Button::Params::Primary("Save changes"),
                {
                    &ToolPathSettingsStyle::FooterButton,
                    &ToolPathSettingsStyle::FooterButtonPrimary
                }
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

            double stepDown = 0.0;
            double stepoverPercent = 0.0;
            double feedRate = 0.0;

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

            const auto strategy = Cam::App::Slicer::Strategy::strategyTypeFromString(
                strategyDropdown->params.value
            );

            if (!app->saveToolPathSettings(
                state,
                strategy,
                toolName,
                stepDown,
                stepoverPercent / 100.0,
                feedRate
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
