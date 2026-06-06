module;

#include <cmath>
#include <cstdio>
#include <string>
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
import Rev.Element.ControlTheme;

import Cam.App;
import Cam.App.Tool;
import Cam.Gui.ToolPreview;
import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    // Structure only — all colours/surfaces come from the shared Theme so this
    // window matches the material-state (toolpath) settings panel.
    namespace ToolSettingsLayout {

        Style Root = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct, 100_pct }
        };

        Style Header = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct },
            .padding = { .left = 20_px, .right = 20_px, .top = 16_px, .bottom = 12_px }
        };

        Style Body = {
            // Padding comes from Theme::Styles::SettingsBody (layered on top),
            // matching the material-state settings panel.
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { Grow() }
        };

        Style LeftColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = Grow() },
            .margin = { 0_px, 16_px, 0_px, 0_px }
        };

        Style MidColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = Grow() },
            .margin = { 0_px, 16_px, 0_px, 0_px }
        };

        Style PreviewColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 196_px, .height = Grow() }
        };

        Style Row = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct }
        };

        Style RowField = {
            .size = { Grow() },
            .margin = { .left = 4_px, .right = 4_px }
        };

        Style LengthLabel = {
            // left, right, top, bottom — a top gap separates it from the fields above.
            .margin = { 4_px, 0_px, 10_px, 2_px },
            .text = { .size = 11_px }
        };

        Style Footer = {
            .layout = { Axis::Horizontal, Align::End, Align::Center, Wrap::False },
            .size = { 100_pct },
            .padding = { .left = 20_px, .right = 20_px, .top = 12_px, .bottom = 16_px }
        };

        Style FooterButton = {
            .margin = { .left = 8_px }
        };
    }

    struct ToolSettingsWindow : public Rev::Window {

        Cam::App::AppState* app = nullptr;
        std::string toolName;
        Cam::App::Tool savedTool;
        bool isUnsavedNewTool = false;

        std::function<void(Event&)> onSaved;
        std::function<void(Event&)> onClosed;

        Text* headerEyebrow = nullptr;
        Text* headerTitle = nullptr;
        Text* lengthLabel = nullptr;

        TextInput* nameInput = nullptr;
        Dropdown* typeDropdown = nullptr;

        NumberInput* diameterInput = nullptr;
        NumberInput* cuttingLengthInput = nullptr;
        NumberInput* taperInput = nullptr;
        NumberInput* shoulderDiameterInput = nullptr;
        NumberInput* shoulderLengthInput = nullptr;
        NumberInput* shoulderTaperInput = nullptr;
        NumberInput* collarDiameterInput = nullptr;
        NumberInput* collarLengthInput = nullptr;

        NumberInput* feedRateInput = nullptr;
        NumberInput* stepdownInput = nullptr;
        NumberInput* stepoverInput = nullptr;
        NumberInput* rapidSpeedInput = nullptr;
        Dropdown* cutDirectionDropdown = nullptr;

        Button* applyButton = nullptr;
        bool applyPendingAppearance = false;

        ToolPreview* preview = nullptr;

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
                .size = { .width = 760, .height = 520 },
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

            style->layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
            style->size = { .width = 100_pct, .height = 100_pct };

            buildUi();
            captureSavedFields();
            syncPreview(event);
            updateApplyButtonAppearance(event);

            setTitle(toolName + " - Settings");

            if (owner) {
                setPos(owner->details.x + 220, owner->details.y + 70);
            }
            else {
                setPos(220, 70);
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

            Cam::App::Tool* tool = app ? app->toolLibrary()->find(toolName) : nullptr;
            const std::string displayName = tool ? tool->name : toolName;
            const Cam::App::Tool::Type selectedType = tool ? tool->type : Cam::App::Tool::Type::EndMill;

            Box* root = new Box(
                this,
                Theme::withSettingsDialog({ &ToolSettingsLayout::Root }),
                "SettingsRoot"
            );

            // Header
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
                Theme::layer({}, { &Theme::Styles::SettingsHeaderEyebrow })
            );

            headerTitle = new Text(
                header,
                displayName,
                Theme::layer({}, { &Theme::Styles::SettingsHeaderTitle })
            );

            // Body
            Box* body = new Box(
                root,
                Theme::layer(
                    { &ToolSettingsLayout::Body, &Theme::Styles::SettingsBody },
                    { &Theme::Styles::Text }
                ),
                "Body"
            );

            Box* left = new Box(body, { &ToolSettingsLayout::LeftColumn }, "Left");
            Box* mid = new Box(body, { &ToolSettingsLayout::MidColumn }, "Mid");
            Box* previewColumn = new Box(body, { &ToolSettingsLayout::PreviewColumn }, "Preview");

            auto row = [&](Box* column, const char* name) {
                return new Box(column, { &ToolSettingsLayout::Row }, name);
            };

            auto section = [&](Box* column, const char* text) {
                new Text(
                    column,
                    text,
                    Theme::layer({}, { &Theme::Styles::SettingsSectionLabel })
                );
            };

            auto field = [&](Box* parent, const char* label, const char* placeholder) {
                return new NumberInput(parent, numberParams(label, placeholder), { &ToolSettingsLayout::RowField });
            };

            // Identity
            Box* identityRow = row(left, "IdentityRow");

            nameInput = new TextInput(
                identityRow,
                {
                    .label = "Name",
                    .placeholder = "Tool name",
                    .maxLength = 64,
                    .selectAllOnFocus = true
                },
                { &ToolSettingsLayout::RowField }
            );

            typeDropdown = new Dropdown(
                identityRow,
                {
                    .label = "Type",
                    .options = {
                        { Cam::App::Tool::typeDisplayName(Cam::App::Tool::Type::EndMill),    "EndMill"    },
                        { Cam::App::Tool::typeDisplayName(Cam::App::Tool::Type::ThreadMill), "ThreadMill" },
                        { Cam::App::Tool::typeDisplayName(Cam::App::Tool::Type::Chamfer),    "Chamfer"    },
                        { Cam::App::Tool::typeDisplayName(Cam::App::Tool::Type::Probe),      "Probe"      }
                    },
                    .placeholder = "Select type",
                    .value = Cam::App::Tool::typeToKindString(selectedType)
                },
                { &ToolSettingsLayout::RowField }
            );

            typeDropdown->onChange = [this](Event& e) {
                updateHeaderEyebrow(currentType());
                refresh(e);
            };

            // Cutting
            section(left, "CUTTING");
            Box* cutRow = row(left, "CutRow");
            diameterInput = field(cutRow, "Diameter (mm)", "1.0");
            cuttingLengthInput = field(cutRow, "Cutting length (mm)", "20");
            taperInput = field(row(left, "TaperRow"), "Tip taper (deg)", "0");

            // Shoulder
            section(left, "SHOULDER");
            Box* shoulderRow = row(left, "ShoulderRow");
            shoulderDiameterInput = field(shoulderRow, "Shoulder dia. (mm)", "0");
            shoulderLengthInput = field(shoulderRow, "Shoulder length (mm)", "0");
            shoulderTaperInput = field(row(left, "ShoulderTaperRow"), "Shoulder taper (deg)", "45");

            // Collar
            section(mid, "COLLAR");
            Box* collarRow = row(mid, "CollarRow");
            collarDiameterInput = field(collarRow, "Collar dia. (mm)", "0");
            collarLengthInput = field(collarRow, "Collar length (mm)", "40");

            // Toolpath defaults
            section(mid, "TOOLPATH DEFAULTS");
            Box* defRow1 = row(mid, "DefRow1");
            feedRateInput = field(defRow1, "Feed rate (mm/min)", "250");
            stepdownInput = field(defRow1, "Stepdown (mm)", "0.5");
            Box* defRow2 = row(mid, "DefRow2");
            stepoverInput = field(defRow2, "Stepover (% dia.)", "25");
            rapidSpeedInput = field(defRow2, "Rapid speed (mm/s)", "10");

            cutDirectionDropdown = new Dropdown(mid, {
                .label = "Cut direction",
                .options = { { "Climb", "climb" }, { "Conventional", "conventional" } },
                .placeholder = "Select direction",
                .value = "climb"
            });

            cutDirectionDropdown->onChange = [this](Event& e) { refresh(e); };

            lengthLabel = new Text(
                mid,
                "Overall length: -",
                Theme::layer(
                    { &ToolSettingsLayout::LengthLabel },
                    { &Theme::Styles::MutedText }
                )
            );

            // Preview
            new Text(
                previewColumn,
                "PREVIEW",
                Theme::layer({}, { &Theme::Styles::SettingsSectionLabel })
            );
            preview = new ToolPreview(previewColumn);

            // Live preview on any geometry edit.
            auto live = [this](NumberInput* input) {
                input->onTextInput([this](Event& e) { syncPreview(e); });
                input->onValueChange = [this](Event& e, std::optional<double>) { syncPreview(e); };
            };

            live(diameterInput);
            live(cuttingLengthInput);
            live(taperInput);
            live(shoulderDiameterInput);
            live(shoulderLengthInput);
            live(shoulderTaperInput);
            live(collarDiameterInput);
            live(collarLengthInput);

            populateFrom(tool);

            // Footer
            Box* footer = new Box(
                root,
                Theme::layer(
                    { &ToolSettingsLayout::Footer },
                    { &Theme::Styles::SettingsFooter }
                ),
                "Footer"
            );

            Button* cancelButton = new Button(
                footer,
                Button::Params::Secondary("Cancel"),
                { &ToolSettingsLayout::FooterButton }
            );

            cancelButton->onClick([this](Event& e) {
                requestClose(&e);
                e.propagate = false;
            });

            applyButton = new Button(
                footer,
                Button::Params::Secondary("Apply"),
                { &ToolSettingsLayout::FooterButton }
            );

            applyButton->onClick([this](Event& e) {
                if (save(e)) {
                    updateApplyButtonAppearance(e);
                }
                e.propagate = false;
            });

            Button* saveButton = new Button(
                footer,
                Button::Params::Primary("Save"),
                { &ToolSettingsLayout::FooterButton }
            );

            saveButton->onClick([this](Event& e) {
                if (save(e)) {
                    updateApplyButtonAppearance(e);
                    close(&e);
                }
                e.propagate = false;
            });
        }

        void populateFrom(Cam::App::Tool* tool) {

            Cam::App::Tool t = tool ? *tool : Cam::App::Tool();

            nameInput->text->content = tool ? tool->name : toolName;

            diameterInput->setValue(t.diameter);
            cuttingLengthInput->setValue(t.cuttingLength);
            taperInput->setValue(t.taperAngle);
            shoulderDiameterInput->setValue(t.shoulderDiameter);
            shoulderLengthInput->setValue(t.shoulderLength);
            shoulderTaperInput->setValue(t.shoulderTaperAngle);
            collarDiameterInput->setValue(t.collarDiameter);
            collarLengthInput->setValue(t.collarLength);

            feedRateInput->setValue(t.defaultFeedRate);
            stepdownInput->setValue(t.defaultStepdown);
            stepoverInput->setValue(t.defaultStepover * 100.0);
            rapidSpeedInput->setValue(t.defaultRapidSpeed);

            cutDirectionDropdown->params.value = t.defaultClimbMilling ? "climb" : "conventional";
            cutDirectionDropdown->dropdownText->content = t.defaultClimbMilling ? "Climb" : "Conventional";
        }

        // Build a Tool from the current form (lenient — for preview and save).
        Cam::App::Tool currentTool() {

            Cam::App::Tool t;

            t.name = nameInput->text->content.get();
            t.type = currentType();

            t.diameter = diameterInput->valueOr(1.0);
            t.radius = t.diameter * 0.5;
            t.cuttingLength = cuttingLengthInput->valueOr(0.0);
            t.taperAngle = taperInput->valueOr(0.0);
            t.shoulderDiameter = shoulderDiameterInput->valueOr(0.0);
            t.shoulderLength = shoulderLengthInput->valueOr(0.0);
            t.shoulderTaperAngle = shoulderTaperInput->valueOr(45.0);
            t.collarDiameter = collarDiameterInput->valueOr(0.0);
            t.collarLength = collarLengthInput->valueOr(0.0);

            t.defaultFeedRate = feedRateInput->valueOr(250.0);
            t.defaultStepdown = stepdownInput->valueOr(0.5);
            t.defaultStepover = stepoverInput->valueOr(25.0) / 100.0;
            t.defaultRapidSpeed = rapidSpeedInput->valueOr(10.0);
            t.defaultClimbMilling = cutDirectionDropdown->params.value != "conventional";

            t.recomputeLength();

            return t;
        }

        void syncPreview(Event& e) {

            Cam::App::Tool t = currentTool();

            if (preview) {
                preview->setTool(t, e);
            }

            if (lengthLabel) {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "Overall length: %.2f mm", t.length);
                lengthLabel->content = buffer;
            }
        }

        void captureSavedFields() {

            Cam::App::Tool* tool = app ? app->toolLibrary()->find(toolName) : nullptr;

            if (tool) {
                savedTool = *tool;
                return;
            }

            savedTool = Cam::App::Tool();
            savedTool.name = toolName;
        }

        bool hasUnsavedChanges(Event&) {

            if (isUnsavedNewTool) { return true; }

            const Cam::App::Tool t = currentTool();

            return (
                t.name != savedTool.name ||
                t.type != savedTool.type ||
                !nearlyEqual(t.diameter, savedTool.diameter) ||
                !nearlyEqual(t.cuttingLength, savedTool.cuttingLength) ||
                !nearlyEqual(t.taperAngle, savedTool.taperAngle) ||
                !nearlyEqual(t.shoulderDiameter, savedTool.shoulderDiameter) ||
                !nearlyEqual(t.shoulderLength, savedTool.shoulderLength) ||
                !nearlyEqual(t.shoulderTaperAngle, savedTool.shoulderTaperAngle) ||
                !nearlyEqual(t.collarDiameter, savedTool.collarDiameter) ||
                !nearlyEqual(t.collarLength, savedTool.collarLength) ||
                !nearlyEqual(t.defaultFeedRate, savedTool.defaultFeedRate) ||
                !nearlyEqual(t.defaultStepdown, savedTool.defaultStepdown) ||
                !nearlyEqual(t.defaultStepover, savedTool.defaultStepover) ||
                !nearlyEqual(t.defaultRapidSpeed, savedTool.defaultRapidSpeed) ||
                t.defaultClimbMilling != savedTool.defaultClimbMilling
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

        void computeStyle(Event& e) override {
            Rev::Window::computeStyle(e);
            updateApplyButtonAppearance(e);
        }

        bool save(Event& e) {

            if (!app) {
                dbg("[ToolSettings] Missing app state");
                return false;
            }

            // Commit any focused field before reading.
            diameterInput->commit(e);
            cuttingLengthInput->commit(e);
            taperInput->commit(e);
            shoulderDiameterInput->commit(e);
            shoulderLengthInput->commit(e);
            shoulderTaperInput->commit(e);
            collarDiameterInput->commit(e);
            collarLengthInput->commit(e);
            feedRateInput->commit(e);
            stepdownInput->commit(e);
            stepoverInput->commit(e);
            rapidSpeedInput->commit(e);

            Cam::App::Tool t = currentTool();

            if (t.name.empty() || t.diameter <= 0.0) {
                dbg("[ToolSettings] Invalid tool settings");
                Rev::OS::Dialog::Warning(
                    "Tool Settings",
                    "Enter a valid name and a diameter greater than zero before saving."
                );
                return false;
            }

            if (!app->saveTool(toolName, t)) {
                dbg("[ToolSettings] Failed to save tool \"%s\"", toolName.c_str());
                Rev::OS::Dialog::Error(
                    "Tool Settings",
                    "Could not save the tool. Check the name and try again."
                );
                return false;
            }

            toolName = t.name;
            isUnsavedNewTool = false;
            captureSavedFields();

            if (headerTitle) {
                headerTitle->content = toolName;
            }

            updateHeaderEyebrow(t.type);
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
            void* owner = window ? static_cast<void*>(window->handle) : nullptr;

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
                if (!save(e)) { return; }
                close(event);
                return;
            }

            discardIfUnsaved();
            close(event);
        }

        void onClose(bool& rejectClose) override {

            Event& e = this->event;
            void* owner = window ? static_cast<void*>(window->handle) : nullptr;

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
                if (!save(e)) { return; }
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
