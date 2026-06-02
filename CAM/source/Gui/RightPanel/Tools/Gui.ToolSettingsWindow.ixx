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

import Cam.App;
import Cam.App.Tool;
import Cam.Gui.ToolPreview;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolSettingsStyle {

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
            .padding = { 20_px, 22_px, 16_px, 18_px },
            .background = { .color = rgba(28, 34, 48, 1.0) }
        };

        Style HeaderEyebrow = {
            .text = { .color = rgba(148, 163, 184, 1.0), .size = 11_px }
        };

        Style HeaderTitle = {
            .margin = { 6_px, 0_px, 0_px, 0_px },
            .text = { .color = rgba(248, 250, 252, 1.0), .size = 20_px }
        };

        Style Body = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { Grow() },
            .padding = { 16_px, 22_px, 8_px, 22_px }
        };

        Style LeftColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = Grow() },
            .margin = { 0_px, 18_px, 0_px, 0_px }
        };

        Style RightColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 240_px, .height = Grow() }
        };

        Style SectionLabel = {
            .margin = { 12_px, 0_px, 8_px, 0_px },
            .text = { .color = rgba(100, 116, 139, 1.0), .size = 11_px }
        };

        Style PreviewLabel = {
            .margin = { 4_px, 0_px, 8_px, 0_px },
            .text = { .color = rgba(100, 116, 139, 1.0), .size = 11_px }
        };

        Style LengthLabel = {
            .margin = { 14_px, 0_px, 0_px, 2_px },
            .text = { .color = rgba(71, 85, 105, 1.0), .size = 12_px }
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
                .size = { .width = 700, .height = 760 },
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
            styles.add(&ToolSettingsStyle::Root);

            buildUi();
            captureSavedFields();
            syncPreview(event);

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

            // Header
            Box* header = new Box(this, { &ToolSettingsStyle::Header }, "Header");

            headerEyebrow = new Text(
                header,
                Cam::App::Tool::typeEyebrow(selectedType),
                { &ToolSettingsStyle::HeaderEyebrow }
            );

            headerTitle = new Text(header, displayName, { &ToolSettingsStyle::HeaderTitle });

            // Body
            Box* body = new Box(this, { &ToolSettingsStyle::Body }, "Body");
            Box* left = new Box(body, { &ToolSettingsStyle::LeftColumn }, "Left");
            Box* right = new Box(body, { &ToolSettingsStyle::RightColumn }, "Right");

            auto row = [&](const char* name) {
                return new Box(left, { &ToolSettingsStyle::Row }, name);
            };

            auto section = [&](const char* text) {
                new Text(left, text, { &ToolSettingsStyle::SectionLabel });
            };

            auto field = [&](Box* parent, const char* label, const char* placeholder) {
                return new NumberInput(parent, numberParams(label, placeholder), { &ToolSettingsStyle::RowField });
            };

            // Identity
            nameInput = new TextInput(left, {
                .label = "Name",
                .placeholder = "Tool name",
                .maxLength = 64,
                .selectAllOnFocus = true
            });

            typeDropdown = new Dropdown(left, {
                .label = "Type",
                .options = {
                    { Cam::App::Tool::typeDisplayName(Cam::App::Tool::Type::EndMill), "EndMill" },
                    { Cam::App::Tool::typeDisplayName(Cam::App::Tool::Type::ThreadMill), "ThreadMill" },
                    { Cam::App::Tool::typeDisplayName(Cam::App::Tool::Type::Chamfer), "Chamfer" }
                },
                .placeholder = "Select type",
                .value = Cam::App::Tool::typeToKindString(selectedType)
            });

            typeDropdown->onChange = [this](Event& e) {
                updateHeaderEyebrow(currentType());
                refresh(e);
            };

            // Cutting
            section("CUTTING");
            Box* cutRow = row("CutRow");
            diameterInput = field(cutRow, "Diameter (mm)", "1.0");
            cuttingLengthInput = field(cutRow, "Cutting length (mm)", "20");
            taperInput = field(row("TaperRow"), "Tip taper (deg)", "0");

            // Shoulder
            section("SHOULDER");
            Box* shoulderRow = row("ShoulderRow");
            shoulderDiameterInput = field(shoulderRow, "Shoulder dia. (mm)", "0");
            shoulderLengthInput = field(shoulderRow, "Shoulder length (mm)", "0");
            shoulderTaperInput = field(row("ShoulderTaperRow"), "Shoulder taper (deg)", "45");

            // Collar
            section("COLLAR");
            Box* collarRow = row("CollarRow");
            collarDiameterInput = field(collarRow, "Collar dia. (mm)", "0");
            collarLengthInput = field(collarRow, "Collar length (mm)", "40");

            // Toolpath defaults
            section("TOOLPATH DEFAULTS");
            Box* defRow1 = row("DefRow1");
            feedRateInput = field(defRow1, "Feed rate (mm/min)", "250");
            stepdownInput = field(defRow1, "Stepdown (mm)", "0.5");
            Box* defRow2 = row("DefRow2");
            stepoverInput = field(defRow2, "Stepover (% dia.)", "25");
            rapidSpeedInput = field(defRow2, "Rapid speed (mm/s)", "10");

            cutDirectionDropdown = new Dropdown(left, {
                .label = "Cut direction",
                .options = { { "Climb", "climb" }, { "Conventional", "conventional" } },
                .placeholder = "Select direction",
                .value = "climb"
            });

            cutDirectionDropdown->onChange = [this](Event& e) { refresh(e); };

            lengthLabel = new Text(left, "Overall length: -", { &ToolSettingsStyle::LengthLabel });

            // Preview
            new Text(right, "PREVIEW", { &ToolSettingsStyle::PreviewLabel });
            preview = new ToolPreview(right);

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
            Box* footer = new Box(this, { &ToolSettingsStyle::Footer }, "Footer");

            Button* cancelButton = new Button(
                footer,
                Button::Params::Secondary("Cancel"),
                { &ToolSettingsStyle::FooterButton, &ToolSettingsStyle::FooterButtonSecondary }
            );

            cancelButton->onClick([this](Event& e) {
                requestClose(&e);
                e.propagate = false;
            });

            Button* saveButton = new Button(
                footer,
                Button::Params::Primary(isUnsavedNewTool ? "Save tool" : "Save changes"),
                { &ToolSettingsStyle::FooterButton, &ToolSettingsStyle::FooterButtonPrimary }
            );

            saveButton->onClick([this](Event& e) {
                save(e);
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
