module;

#include <cmath>
#include <optional>
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
import Rev.Element.ControlTheme;

import Cam.App;
import Cam.App.Stage;
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
            .margin = { .left = 4_px, .right = 4_px }
        };

        Style Footer = {
            .layout = { Axis::Horizontal, Align::End, Align::Center, Wrap::False },
            .size = { 100_pct },
            .padding = { .left = 22_px, .right = 22_px, .top = 14_px, .bottom = 20_px }
        };

        Style FooterButton = {
            .margin = { .left = 6_px, .right = 6_px }
        };
    }

    struct ToolPathSettingsWindow : public Rev::Window {

        struct SavedFields {
            std::string strategy;
            std::string toolName;
            double stepDown = 0.0;
            double stepoverPercent = 0.0;
            double feedRate = 0.0;
            bool climbMilling = true;
            double rapidSpeedMmPerSec = 0.0;
            double linkRetract = 0.0;
        };

        Cam::App::AppState* app = nullptr;
        Cam::App::Stage* state = nullptr;
        std::string stateTitle;

        SavedFields savedFields;
        bool applyPendingAppearance = false;

        Text* headerTitle = nullptr;
        Dropdown* strategyDropdown = nullptr;
        Dropdown* toolDropdown = nullptr;
        Dropdown* cutDirectionDropdown = nullptr;
        NumberInput* stepDownInput = nullptr;
        NumberInput* stepoverInput = nullptr;
        NumberInput* feedRateInput = nullptr;
        NumberInput* rapidSpeedInput = nullptr;
        NumberInput* linkRetractInput = nullptr;
        Button* applyButton = nullptr;

        std::function<void(Event&)> onSaved;
        std::function<void(Event&)> onClosed;

        static bool nearlyEqual(double a, double b) {
            return std::fabs(a - b) < 1e-6;
        }

        static Rev::Window* rootWindow(Element* from) {

            Element* node = from;

            while (node && node->parent && node->parent != node) {
                node = node->parent;
            }

            return static_cast<Rev::Window*>(node);
        }

        static std::vector<Dropdown::Item> toolOptions(Cam::App::AppState* app) {

            std::vector<Dropdown::Item> items;

            if (!app) { return items; }

            for (size_t i = 0; i < app->toolCount(); i++) {

                Cam::App::Tool* tool = app->toolAt(i);

                if (!tool) { continue; }

                // Display label includes the 1-based slot number so operators
                // can quickly match tool selections to machine slots.
                // The stored value remains the plain tool name so it serialises
                // cleanly alongside the material-state toolpath data.
                const std::string label =
                    "#" + std::to_string(i + 1) + "  " + tool->name;

                items.push_back({ label, tool->name });
            }

            if (items.empty()) {
                items.push_back({ "No tools", "" });
            }

            return items;
        }

        ToolPathSettingsWindow(
            Rev::Window* owner,
            Cam::App::Stage* materialState,
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
            captureSavedFieldsFromForm(event);
            updateApplyButtonAppearance(event);

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

            strategyDropdown->onChange = [this](Event& e) {
                updateApplyButtonAppearance(e);
                refresh(e);
            };

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

            toolDropdown->onChange = [this](Event& e) {
                updateApplyButtonAppearance(e);
                refresh(e);
            };

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
            stepDownParams.placeholder = "0.5";
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

            auto onFieldEdited = [this](Event& e, std::optional<double>) {
                updateApplyButtonAppearance(e);
                refresh(e);
            };

            auto hookLiveNumberEdit = [this, onFieldEdited](NumberInput* input) {
                input->onTextInput([this](Event& e) {
                    updateApplyButtonAppearance(e);
                    refresh(e);
                });
                input->onValueChange = onFieldEdited;
            };

            hookLiveNumberEdit(stepDownInput);
            hookLiveNumberEdit(stepoverInput);

            NumberInput::Params feedRateParams;
            feedRateParams.label = "Feed rate (mm/min)";
            feedRateParams.placeholder = "250";
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

            cutDirectionDropdown->onChange = [this](Event& e) {
                updateApplyButtonAppearance(e);
                refresh(e);
            };

            hookLiveNumberEdit(feedRateInput);

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

            hookLiveNumberEdit(rapidSpeedInput);
            hookLiveNumberEdit(linkRetractInput);

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
                { &ToolPathSettingsLayout::FooterButton }
            );

            cancelButton->onClick([this](Event& e) {
                close(&e);
                e.propagate = false;
            });

            applyButton = new Button(
                footer,
                Button::Params::Secondary("Apply"),
                { &ToolPathSettingsLayout::FooterButton }
            );

            applyButton->onClick([this](Event& e) {
                if (trySave(e)) {
                    updateApplyButtonAppearance(e);
                }
                e.propagate = false;
            });

            Button* saveButton = new Button(
                footer,
                Button::Params::Primary("Save"),
                { &ToolPathSettingsLayout::FooterButton }
            );

            saveButton->onClick([this](Event& e) {
                if (trySave(e)) {
                    updateApplyButtonAppearance(e);
                    close(&e);
                }
                e.propagate = false;
            });
        }

        void computeStyle(Event& e) override {
            Rev::Window::computeStyle(e);
            updateApplyButtonAppearance(e);
        }

        void captureSavedFieldsFromForm(Event& e) {

            SavedFields snapshot;

            if (readCurrentFields(e, true, snapshot)) {
                savedFields = snapshot;
                return;
            }

            if (!state) {
                savedFields = {};
                return;
            }

            const Cam::App::ToolPath& toolPath = state->toolPath;

            savedFields = {
                .strategy = toolPath.strategy,
                .toolName = toolPath.toolName,
                .stepDown = toolPath.stepDown,
                .stepoverPercent = toolPath.stepover * 100.0,
                .feedRate = toolPath.feedRate,
                .climbMilling = toolPath.climbMilling,
                .rapidSpeedMmPerSec = toolPath.rapidSpeedMmPerSec,
                .linkRetract = double(toolPath.linkRetractDistance)
            };
        }

        bool readCurrentFields(Event& e, bool commitInputs, SavedFields& out) const {

            if (commitInputs) {
                stepDownInput->commit(e);
                stepoverInput->commit(e);
                feedRateInput->commit(e);
                rapidSpeedInput->commit(e);
                linkRetractInput->commit(e);
            }

            out.strategy = strategyDropdown->params.value;
            out.toolName = toolDropdown->params.value;
            out.climbMilling = cutDirectionDropdown->params.value != "conventional";

            if (!stepDownInput->tryGetValue(out.stepDown)) { return false; }
            if (!stepoverInput->tryGetValue(out.stepoverPercent)) { return false; }
            if (!feedRateInput->tryGetValue(out.feedRate)) { return false; }
            if (!rapidSpeedInput->tryGetValue(out.rapidSpeedMmPerSec)) { return false; }
            if (!linkRetractInput->tryGetValue(out.linkRetract)) { return false; }

            return true;
        }

        bool hasUnsavedChanges(Event& e) const {

            SavedFields current;

            if (!readCurrentFields(e, false, current)) {
                return true;
            }

            return (
                current.strategy != savedFields.strategy ||
                current.toolName != savedFields.toolName ||
                !nearlyEqual(current.stepDown, savedFields.stepDown) ||
                !nearlyEqual(current.stepoverPercent, savedFields.stepoverPercent) ||
                !nearlyEqual(current.feedRate, savedFields.feedRate) ||
                current.climbMilling != savedFields.climbMilling ||
                !nearlyEqual(current.rapidSpeedMmPerSec, savedFields.rapidSpeedMmPerSec) ||
                !nearlyEqual(current.linkRetract, savedFields.linkRetract)
            );
        }

        // Apply reads as a primary (blue) action while edits are pending and
        // falls back to the same grey secondary look as Cancel once saved.
        void updateApplyButtonAppearance(Event& e) {

            if (!applyButton) { return; }

            const bool pending = hasUnsavedChanges(e);

            if (pending == applyPendingAppearance) { return; }

            applyPendingAppearance = pending;

            Text* label = applyButton->labelText;

            if (pending) {
                applyButton->styles.remove(&ControlTheme::ButtonSecondary);
                applyButton->styles.remove(&ControlTheme::ButtonSecondaryHover);
                applyButton->styles.add(&ControlTheme::ButtonPrimary);
                applyButton->styles.add(&ControlTheme::ButtonPrimaryHover);

                if (label) {
                    label->styles.remove(&ControlTheme::ButtonSecondaryLabel);
                    label->styles.add(&ControlTheme::ButtonPrimaryLabel);
                }
            }
            else {
                applyButton->styles.remove(&ControlTheme::ButtonPrimary);
                applyButton->styles.remove(&ControlTheme::ButtonPrimaryHover);
                applyButton->styles.add(&ControlTheme::ButtonSecondary);
                applyButton->styles.add(&ControlTheme::ButtonSecondaryHover);

                if (label) {
                    label->styles.remove(&ControlTheme::ButtonPrimaryLabel);
                    label->styles.add(&ControlTheme::ButtonSecondaryLabel);
                }
            }

            applyButton->dirty.style = true;

            if (label) { label->dirty.style = true; }
        }

        bool trySave(Event& e) {

            if (!app || !state) {
                dbg("[ToolPathSettings] Missing app state or material state");
                return false;
            }

            const std::string toolName = toolDropdown->params.value;

            if (toolName.empty()) {
                dbg("[ToolPathSettings] Tool is required");
                return false;
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
                return false;
            }

            if (!stepoverInput->tryGetValue(stepoverPercent) || stepoverPercent <= 0.0) {
                dbg("[ToolPathSettings] Invalid stepover");
                return false;
            }

            if (!feedRateInput->tryGetValue(feedRate) || feedRate <= 0.0) {
                dbg("[ToolPathSettings] Invalid feed rate");
                return false;
            }

            if (!rapidSpeedInput->tryGetValue(rapidSpeed) || rapidSpeed <= 0.0) {
                dbg("[ToolPathSettings] Invalid rapid speed");
                return false;
            }

            if (!linkRetractInput->tryGetValue(linkRetract) || linkRetract <= 0.0) {
                dbg("[ToolPathSettings] Invalid link retract height");
                return false;
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
                return false;
            }

            dbg("[ToolPathSettings] Saved toolpath settings for \"%s\"", stateTitle.c_str());

            captureSavedFieldsFromForm(e);

            if (onSaved) {
                onSaved(e);
            }

            refresh(e);
            return true;
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
