module;

#include <cmath>
#include <string>
#include <vector>
#include <optional>
#include <functional>

#include <dbg.hpp>

export module Cam.Gui.ToolSettingsWindow;

import Rev.Window;
import Rev.OS.Dialog;
import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.TextInput;
import Rev.Element.NumberInput;
import Rev.Element.Dropdown;
import Rev.Element.Button;

import Cam.App;
import Cam.App.Tool;
import Cam.Gui.ToolPreview;
import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolSettingsLayout {

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
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { Grow() }
        };

        Style LeftColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = Grow() },
            .margin = { 0_px, 18_px, 0_px, 0_px }
        };

        Style RightColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 220_px, .height = Grow() }
        };

        Style PreviewPanel = {
            .size = { .width = Grow(), .height = Grow() }
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
            .margin = { .left = 6_px, .right = 6_px }
        };
    }

    struct ToolSettingsWindow : public Rev::Window {

        Cam::App::AppState* app = nullptr;
        std::string toolName;
        Cam::App::Tool savedTool;
        bool isUnsavedNewTool = false;
        bool savePendingAppearance = false;

        std::function<void(Event&)> onSaved;
        std::function<void(Event&)> onClosed;

        Text* headerEyebrow = nullptr;
        Text* headerTitle = nullptr;

        TextInput* nameInput = nullptr;
        Dropdown* typeDropdown = nullptr;

        NumberInput* diameterInput = nullptr;
        NumberInput* lengthInput = nullptr;
        NumberInput* shoulderInput = nullptr;
        NumberInput* taperInput = nullptr;
        NumberInput* collarRadiusInput = nullptr;
        NumberInput* collarDepthInput = nullptr;

        NumberInput* feedRateInput = nullptr;
        NumberInput* stepdownInput = nullptr;
        NumberInput* stepoverInput = nullptr;
        NumberInput* rapidSpeedInput = nullptr;
        Dropdown* cutDirectionDropdown = nullptr;

        ToolPreview* preview = nullptr;
        Button* saveButton = nullptr;

        static std::string windowTitleFor(const std::string& toolName) {
            return toolName + " - Settings";
        }

        static Rev::Window* rootWindow(Element* from) {

            Element* node = from;

            while (node && node->parent && node->parent != node) {
                node = node->parent;
            }

            return static_cast<Rev::Window*>(node);
        }

        static bool nearlyEqual(double a, double b) {
            return std::fabs(a - b) < 1e-6;
        }

        Cam::App::Tool::Type currentType() const {
            return Cam::App::Tool::typeFromKindString(typeDropdown->params.value);
        }

        void updateHeaderEyebrow(Cam::App::Tool::Type type) {
            if (headerEyebrow) {
                headerEyebrow->content = Cam::App::Tool::typeEyebrow(type);
            }
        }

        void applyWindowTitle() {
            setTitle(windowTitleFor(toolName));
        }

        ToolSettingsWindow(
            Rev::Window* owner,
            const std::string& initialToolName
        ) : Rev::Window(
            owner,
            {
                .name = initialToolName + " - Settings",
                .size = { .width = 640, .height = 580 },
                .minimizeButton = false,
                .maximizeButton = false
            }
        ) {
            toolName = initialToolName;

            if (owner && owner->shared) {
                shared->state = owner->shared->state;
            }

            app = Cam::App::AppState::Get(shared->state);

            isUnsavedNewTool = app && app->isUnsavedTool(toolName);

            style->size = { .width = 100_pct, .height = 100_pct };

            buildUi();
            captureSavedFields();
            updateSaveButtonAppearance(event);
            syncPreview(event);

            setTitle(toolName + " - Settings");

            if (owner) {
                setPos(owner->details.x + 240, owner->details.y + 80);
            }
            else {
                setPos(240, 80);
            }

            show();
            refresh(event);
        }

        static NumberInput::Params numberParams(
            const char* label,
            const char* placeholder
        ) {
            NumberInput::Params p;
            p.label = label;
            p.placeholder = placeholder;
            p.maxLength = 32;
            p.selectAllOnFocus = true;
            p.allowNegative = false;
            p.allowDecimal = true;
            p.allowEmpty = false;
            p.maxDecimalPlaces = 4;
            return p;
        }

        void buildUi() {

            Cam::App::Tool* tool = app
                ? app->toolLibrary()->find(toolName)
                : nullptr;

            const std::string displayName = tool ? tool->name : toolName;
            const Cam::App::Tool::Type selectedType = tool
                ? tool->type
                : Cam::App::Tool::Type::EndMill;

            Box* root = new Box(
                this,
                Theme::withSettingsDialog({ &ToolSettingsLayout::Root }),
                "SettingsRoot"
            );

            Box* header = new Box(
                root,
                Theme::layer(
                    { &ToolSettingsLayout::Header },
                    { &Theme::Styles::SettingsHeader }
                ),
                "Header"
            );

            headerEyebrow = new Text(
                header,
                Cam::App::Tool::typeEyebrow(selectedType),
                Theme::layer(
                    {},
                    { &Theme::Styles::SettingsHeaderEyebrow }
                )
            );

            headerTitle = new Text(
                header,
                displayName,
                Theme::layer(
                    {},
                    { &Theme::Styles::SettingsHeaderTitle }
                )
            );

            Box* body = new Box(
                root,
                Theme::layer(
                    { &ToolSettingsLayout::Body, &Theme::Styles::SettingsBody },
                    { &Theme::Styles::Text }
                ),
                "Body"
            );

            Box* left = new Box(
                body,
                { &ToolSettingsLayout::LeftColumn },
                "LeftColumn"
            );

            Box* right = new Box(
                body,
                { &ToolSettingsLayout::RightColumn },
                "RightColumn"
            );

            nameInput = new TextInput(
                left,
                {
                    .label = "Name",
                    .placeholder = "Tool name",
                    .maxLength = 64,
                    .selectAllOnFocus = true
                }
            );

            typeDropdown = new Dropdown(
                left,
                {
                    .label = "Type",
                    .options = {
                        { Cam::App::Tool::typeDisplayName(Cam::App::Tool::Type::EndMill), "EndMill" },
                        { Cam::App::Tool::typeDisplayName(Cam::App::Tool::Type::ThreadMill), "ThreadMill" },
                        { Cam::App::Tool::typeDisplayName(Cam::App::Tool::Type::Chamfer), "Chamfer" }
                    },
                    .placeholder = "Select type",
                    .value = Cam::App::Tool::typeToKindString(selectedType)
                }
            );

            typeDropdown->onChange = [this](Event& e) {
                updateHeaderEyebrow(currentType());
                updateSaveButtonAppearance(e);
                syncPreview(e);
                refresh(e);
            };

            new Text(
                left,
                "TOOL DIMENSIONS",
                Theme::layer(
                    {},
                    { &Theme::Styles::SettingsSectionLabel }
                )
            );

            Box* dimRow1 = new Box(
                left,
                Theme::layer({ &ToolSettingsLayout::Row }, {}),
                "DimRow1"
            );

            diameterInput = new NumberInput(
                dimRow1,
                numberParams("Diameter (mm)", "1.0"),
                { &ToolSettingsLayout::RowField }
            );

            lengthInput = new NumberInput(
                dimRow1,
                numberParams("Total length (mm)", "100"),
                { &ToolSettingsLayout::RowField }
            );

            Box* dimRow2 = new Box(
                left,
                Theme::layer({ &ToolSettingsLayout::Row }, {}),
                "DimRow2"
            );

            shoulderInput = new NumberInput(
                dimRow2,
                numberParams("Shoulder length (mm)", "20"),
                { &ToolSettingsLayout::RowField }
            );

            taperInput = new NumberInput(
                dimRow2,
                numberParams("Taper angle (deg)", "0"),
                { &ToolSettingsLayout::RowField }
            );

            Box* dimRow3 = new Box(
                left,
                Theme::layer({ &ToolSettingsLayout::Row }, {}),
                "DimRow3"
            );

            collarRadiusInput = new NumberInput(
                dimRow3,
                numberParams("Collar radius (mm)", "0"),
                { &ToolSettingsLayout::RowField }
            );

            collarDepthInput = new NumberInput(
                dimRow3,
                numberParams("Collar depth (mm)", "0"),
                { &ToolSettingsLayout::RowField }
            );

            new Text(
                left,
                "TOOLPATH DEFAULTS",
                Theme::layer(
                    {},
                    { &Theme::Styles::SettingsSectionLabel }
                )
            );

            Box* defRow1 = new Box(
                left,
                Theme::layer({ &ToolSettingsLayout::Row }, {}),
                "DefRow1"
            );

            feedRateInput = new NumberInput(
                defRow1,
                numberParams("Feed rate (mm/min)", "250"),
                { &ToolSettingsLayout::RowField }
            );

            stepdownInput = new NumberInput(
                defRow1,
                numberParams("Stepdown (mm)", "0.5"),
                { &ToolSettingsLayout::RowField }
            );

            Box* defRow2 = new Box(
                left,
                Theme::layer({ &ToolSettingsLayout::Row }, {}),
                "DefRow2"
            );

            stepoverInput = new NumberInput(
                defRow2,
                numberParams("Stepover (% dia.)", "25"),
                { &ToolSettingsLayout::RowField }
            );

            rapidSpeedInput = new NumberInput(
                defRow2,
                numberParams("Rapid speed (mm/s)", "10"),
                { &ToolSettingsLayout::RowField }
            );

            cutDirectionDropdown = new Dropdown(
                left,
                {
                    .label = "Cut direction",
                    .options = {
                        { "Climb", "climb" },
                        { "Conventional", "conventional" }
                    },
                    .placeholder = "Select direction",
                    .value = "climb"
                }
            );

            cutDirectionDropdown->onChange = [this](Event& e) {
                updateSaveButtonAppearance(e);
                refresh(e);
            };

            new Text(
                right,
                "PREVIEW",
                Theme::layer(
                    {},
                    { &Theme::Styles::SettingsSectionLabel }
                )
            );

            preview = new ToolPreview(
                right,
                Theme::layer(
                    { &ToolSettingsLayout::PreviewPanel },
                    { &Theme::Styles::SettingsPreview }
                )
            );

            auto onFieldEdited = [this](Event& e, std::optional<double>) {
                updateSaveButtonAppearance(e);
                syncPreview(e);
                refresh(e);
            };

            auto hookLiveNumberEdit = [this, onFieldEdited](NumberInput* input) {
                input->onTextInput([this](Event& e) {
                    updateSaveButtonAppearance(e);
                    syncPreview(e);
                    refresh(e);
                });
                input->onValueChange = onFieldEdited;
            };

            hookLiveNumberEdit(diameterInput);
            hookLiveNumberEdit(lengthInput);
            hookLiveNumberEdit(shoulderInput);
            hookLiveNumberEdit(taperInput);
            hookLiveNumberEdit(collarRadiusInput);
            hookLiveNumberEdit(collarDepthInput);
            hookLiveNumberEdit(feedRateInput);
            hookLiveNumberEdit(stepdownInput);
            hookLiveNumberEdit(stepoverInput);
            hookLiveNumberEdit(rapidSpeedInput);

            nameInput->onTextInput([this](Event& e) {
                updateSaveButtonAppearance(e);
                refresh(e);
            });

            populateFrom(tool);

            Box* footer = new Box(
                root,
                Theme::layer(
                    { &ToolSettingsLayout::Footer },
                    { &Theme::Styles::SettingsFooter }
                ),
                "Footer"
            );

            const auto footerSecondaryStyles = Theme::layer({
                &ToolSettingsLayout::FooterButton,
                &Theme::Styles::ButtonHover,
                &Theme::Styles::ButtonPress
            }, {
                &Theme::Styles::Button,
                &Theme::Styles::ButtonLabel
            });

            Button* cancelButton = new Button(
                footer,
                Button::Params::Secondary("Cancel"),
                footerSecondaryStyles
            );

            cancelButton->onClick([this](Event& e) {
                requestClose(&e);
                e.propagate = false;
            });

            saveButton = new Button(
                footer,
                Button::Params::Secondary(
                    isUnsavedNewTool ? "Save tool" : "Save changes"
                ),
                footerSecondaryStyles
            );

            saveButton->onClick([this](Event& e) {
                save(e);
                e.propagate = false;
            });

            Button* okButton = new Button(
                footer,
                Button::Params::Primary("OK"),
                { &ToolSettingsLayout::FooterButton }
            );

            okButton->onClick([this](Event& e) {
                if (save(e)) {
                    close(&e);
                }
                e.propagate = false;
            });
        }

        void computeStyle(Event& e) override {
            Rev::Window::computeStyle(e);
            updateSaveButtonAppearance(e);
        }

        void updateSaveButtonAppearance(Event& e) {

            if (!saveButton) { return; }

            const bool pending = hasUnsavedChanges(e);

            if (pending == savePendingAppearance) { return; }

            savePendingAppearance = pending;

            saveButton->styles.remove(&Theme::Styles::Button);
            saveButton->styles.remove(&Theme::Styles::ButtonHover);
            saveButton->styles.remove(&Theme::Styles::ButtonPress);
            saveButton->styles.remove(&Theme::Styles::ButtonLabel);
            saveButton->styles.remove(&Theme::Styles::SettingsApplyDirty);
            saveButton->styles.remove(&Theme::Styles::SettingsApplyDirtyHover);
            saveButton->styles.remove(&Theme::Styles::SettingsApplyDirtyPress);

            if (pending) {
                saveButton->styles.add(&Theme::Styles::SettingsApplyDirty);
                saveButton->styles.add(&Theme::Styles::SettingsApplyDirtyHover);
                saveButton->styles.add(&Theme::Styles::SettingsApplyDirtyPress);
            }
            else {
                saveButton->styles.add(&Theme::Styles::Button);
                saveButton->styles.add(&Theme::Styles::ButtonHover);
                saveButton->styles.add(&Theme::Styles::ButtonPress);
            }

            saveButton->styles.add(&Theme::Styles::ButtonLabel);
            saveButton->dirty.style = true;
        }

        void populateFrom(Cam::App::Tool* tool) {

            Cam::App::Tool t = tool ? *tool : Cam::App::Tool();

            if (!tool && isUnsavedNewTool) {
                t.name = toolName;
            }

            nameInput->text->content = tool ? tool->name : toolName;

            diameterInput->setValue(t.diameter);
            lengthInput->setValue(t.length);
            shoulderInput->setValue(t.shoulderLength);
            taperInput->setValue(t.taperAngle);
            collarRadiusInput->setValue(t.collarRadius);
            collarDepthInput->setValue(t.collarDepth);

            feedRateInput->setValue(t.defaultFeedRate);
            stepdownInput->setValue(t.defaultStepdown);
            stepoverInput->setValue(t.defaultStepover * 100.0);
            rapidSpeedInput->setValue(t.defaultRapidSpeed);

            cutDirectionDropdown->params.value = t.defaultClimbMilling ? "climb" : "conventional";
            cutDirectionDropdown->dropdownText->content = t.defaultClimbMilling ? "Climb" : "Conventional";
        }

        ToolPreview::Geometry currentGeometry() {

            ToolPreview::Geometry g;

            const double diameter = diameterInput->valueOr(1.0);

            g.radius = diameter * 0.5;
            g.length = lengthInput->valueOr(100.0);
            g.shoulderLength = shoulderInput->valueOr(0.0);
            g.taperAngle = taperInput->valueOr(0.0);
            g.collarRadius = collarRadiusInput->valueOr(0.0);
            g.collarDepth = collarDepthInput->valueOr(0.0);

            return g;
        }

        void syncPreview(Event& e) {
            if (preview) {
                preview->setGeometry(currentGeometry(), e);
            }
        }

        bool readForm(Cam::App::Tool& out, Event& e, bool commitInputs) {

            const std::string name = nameInput->text->content.get();

            if (name.empty()) { return false; }

            if (commitInputs) {
                diameterInput->commit(e);
                lengthInput->commit(e);
                shoulderInput->commit(e);
                taperInput->commit(e);
                collarRadiusInput->commit(e);
                collarDepthInput->commit(e);
                feedRateInput->commit(e);
                stepdownInput->commit(e);
                stepoverInput->commit(e);
                rapidSpeedInput->commit(e);
            }

            double diameter = 0.0;
            double length = 0.0;

            if (!diameterInput->tryGetValue(diameter) || diameter <= 0.0) { return false; }
            if (!lengthInput->tryGetValue(length) || length <= 0.0) { return false; }

            out = Cam::App::Tool();
            out.name = name;
            out.type = currentType();

            out.diameter = diameter;
            out.radius = diameter * 0.5;
            out.length = length;

            out.taperAngle = taperInput->valueOr(0.0);
            out.shoulderLength = shoulderInput->valueOr(0.0);
            out.collarRadius = collarRadiusInput->valueOr(0.0);
            out.collarDepth = collarDepthInput->valueOr(0.0);

            out.defaultFeedRate = feedRateInput->valueOr(250.0);
            out.defaultStepdown = stepdownInput->valueOr(0.5);
            out.defaultStepover = stepoverInput->valueOr(25.0) / 100.0;
            out.defaultRapidSpeed = rapidSpeedInput->valueOr(10.0);
            out.defaultClimbMilling = cutDirectionDropdown->params.value != "conventional";

            return true;
        }

        void captureSavedFields() {

            Cam::App::Tool* tool = app
                ? app->toolLibrary()->find(toolName)
                : nullptr;

            if (tool) {
                savedTool = *tool;
                return;
            }

            savedTool = Cam::App::Tool();
            savedTool.name = toolName;
        }

        bool hasUnsavedChanges(Event& e) {

            if (isUnsavedNewTool) { return true; }

            Cam::App::Tool form;

            if (!readForm(form, e, true)) { return true; }

            return (
                form.name != savedTool.name ||
                form.type != savedTool.type ||
                !nearlyEqual(form.diameter, savedTool.diameter) ||
                !nearlyEqual(form.length, savedTool.length) ||
                !nearlyEqual(form.taperAngle, savedTool.taperAngle) ||
                !nearlyEqual(form.shoulderLength, savedTool.shoulderLength) ||
                !nearlyEqual(form.collarRadius, savedTool.collarRadius) ||
                !nearlyEqual(form.collarDepth, savedTool.collarDepth) ||
                !nearlyEqual(form.defaultFeedRate, savedTool.defaultFeedRate) ||
                !nearlyEqual(form.defaultStepdown, savedTool.defaultStepdown) ||
                !nearlyEqual(form.defaultStepover, savedTool.defaultStepover) ||
                !nearlyEqual(form.defaultRapidSpeed, savedTool.defaultRapidSpeed) ||
                form.defaultClimbMilling != savedTool.defaultClimbMilling
            );
        }

        bool save(Event& e) {

            if (!app) {
                dbg("[ToolSettings] Missing app state");
                return false;
            }

            Cam::App::Tool form;

            if (!readForm(form, e, true)) {
                dbg("[ToolSettings] Invalid tool settings");
                Rev::OS::Dialog::Warning(
                    "Tool Settings",
                    "Enter a valid name, diameter, and total length before saving."
                );
                return false;
            }

            if (!app->saveTool(toolName, form)) {
                dbg("[ToolSettings] Failed to save tool \"%s\"", toolName.c_str());
                Rev::OS::Dialog::Error(
                    "Tool Settings",
                    "Could not save the tool. Check the name and try again."
                );
                return false;
            }

            toolName = form.name;
            isUnsavedNewTool = false;
            captureSavedFields();
            updateSaveButtonAppearance(e);

            if (headerTitle) {
                headerTitle->content = toolName;
            }

            updateHeaderEyebrow(form.type);
            applyWindowTitle();

            if (onSaved) {
                onSaved(e);
            }

            refresh(e);
            return true;
        }

        void discardIfUnsaved() {

            if (!app || !isUnsavedNewTool) {
                return;
            }

            app->removeTool(toolName);
            isUnsavedNewTool = false;
        }

        void close(Event* event = nullptr) {

            shouldClose = true;

            if (event && onClosed) {
                onClosed(*event);
            }
        }

        void requestClose(Event* event = nullptr) {

            Event& e = event ? *event : this->event;
            void* owner = window
                ? static_cast<void*>(window->handle)
                : nullptr;

            if (!hasUnsavedChanges(e)) {
                close(event);
                return;
            }

            Rev::OS::UnsavedChangesResult result =
                Rev::OS::Dialog::UnsavedChanges(windowTitleFor(toolName), owner);

            if (result == Rev::OS::UnsavedChangesResult::Cancel) {
                return;
            }

            if (result == Rev::OS::UnsavedChangesResult::Save) {
                if (!save(e)) {
                    return;
                }

                close(event);
                return;
            }

            discardIfUnsaved();
            close(event);
        }

        void onClose(bool& rejectClose) override {

            Event& e = this->event;
            void* owner = window
                ? static_cast<void*>(window->handle)
                : nullptr;

            if (!hasUnsavedChanges(e)) {
                if (onClosed) { onClosed(e); }
                rejectClose = false;
                return;
            }

            rejectClose = true;

            Rev::OS::UnsavedChangesResult result =
                Rev::OS::Dialog::UnsavedChanges(windowTitleFor(toolName), owner);

            if (result == Rev::OS::UnsavedChangesResult::Cancel) {
                return;
            }

            if (result == Rev::OS::UnsavedChangesResult::Save) {
                if (!save(e)) {
                    return;
                }

                if (onClosed) { onClosed(e); }
                rejectClose = false;
                return;
            }

            discardIfUnsaved();
            if (onClosed) { onClosed(e); }
            rejectClose = false;
        }
    };
}
