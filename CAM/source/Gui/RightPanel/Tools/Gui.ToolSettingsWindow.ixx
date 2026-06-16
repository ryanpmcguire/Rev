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
import Rev.Appearance;

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

        Style ContentColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = Grow() },
            .margin = { 0_px, 16_px, 0_px, 0_px }
        };

        Style FieldsArea = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct }
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
        NumberInput* pitchInput = nullptr;
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

        // The field columns are rebuilt per tool type (each type gets its own
        // menu rather than sharing/relabeling fields).  `working` carries the
        // in-progress values across those rebuilds so a type switch doesn't lose
        // edits, and lets fields absent from one type survive into another.
        Box* fieldsArea = nullptr;
        Box* leftColumn = nullptr;
        Box* midColumn = nullptr;
        Cam::App::Tool working;

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

        // Small builders shared by the per-type field layouts.
        Box* makeRow(Box* column, const char* name) {
            return new Box(column, { &ToolSettingsLayout::Row }, name);
        }

        void makeSection(Box* column, const char* text) {
            new Text(column, text, Theme::layer({}, { &Theme::Styles::SettingsSectionLabel }));
        }

        NumberInput* makeField(Box* parent, const char* label, const char* placeholder) {
            return new NumberInput(parent, numberParams(label, placeholder), { &ToolSettingsLayout::RowField });
        }

        void bindLive(NumberInput* input) {
            if (!input) { return; }
            input->onTextInput([this](Event& e) { syncPreview(e); });
            input->onValueChange = [this](Event& e, std::optional<double>) { syncPreview(e); };
        }

        // The toolpath-default block, shared but tailored: thread mills have no
        // stepdown (the pitch is the axial step); probes don't cut, so they get
        // only the motion speeds.
        void buildDefaults(Box* col, bool withStepdown, bool withStepover, bool withCutDir) {

            makeSection(col, "TOOLPATH DEFAULTS");

            Box* r1 = makeRow(col, "DefRow1");
            feedRateInput = makeField(r1, "Feed rate (mm/min)", "250");
            if (withStepdown) { stepdownInput = makeField(r1, "Stepdown (mm)", "0.5"); }

            Box* r2 = makeRow(col, "DefRow2");
            if (withStepover) { stepoverInput = makeField(r2, "Stepover (% dia.)", "25"); }
            rapidSpeedInput = makeField(r2, "Rapid speed (mm/s)", "10");

            if (withCutDir) {
                cutDirectionDropdown = new Dropdown(col, {
                    .label = "Cut direction",
                    .options = { { "Climb", "climb" }, { "Conventional", "conventional" } },
                    .placeholder = "Select direction",
                    .value = "climb"
                });
                cutDirectionDropdown->onChange = [this](Event& e) { refresh(e); };
            }
        }

        // Per-type field menus -- each owns exactly the fields that make sense for
        // it, with labels in its own language (no shared/relabeled controls).

        void buildEndMillFields(Box* left, Box* mid) {
            makeSection(left, "CUTTING");
            Box* cut = makeRow(left, "CutRow");
            diameterInput = makeField(cut, "Diameter (mm)", "1.0");
            cuttingLengthInput = makeField(cut, "Cutting length (mm)", "20");
            taperInput = makeField(makeRow(left, "TaperRow"), "Tip taper (deg)", "0");

            makeSection(left, "SHOULDER");
            Box* sh = makeRow(left, "ShoulderRow");
            shoulderDiameterInput = makeField(sh, "Shoulder dia. (mm)", "0");
            shoulderLengthInput = makeField(sh, "Shoulder length (mm)", "0");
            shoulderTaperInput = makeField(makeRow(left, "ShoulderTaperRow"), "Shoulder taper (deg)", "45");

            makeSection(mid, "COLLAR");
            Box* col = makeRow(mid, "CollarRow");
            collarDiameterInput = makeField(col, "Collar dia. (mm)", "0");
            collarLengthInput = makeField(col, "Collar length (mm)", "40");

            buildDefaults(mid, /*stepdown*/ true, /*stepover*/ true, /*cutDir*/ true);
        }

        void buildChamferFields(Box* left, Box* mid) {
            makeSection(left, "CUTTING");
            Box* cut = makeRow(left, "CutRow");
            diameterInput = makeField(cut, "Diameter (mm)", "6.0");
            cuttingLengthInput = makeField(cut, "Cutting length (mm)", "10");
            taperInput = makeField(makeRow(left, "AngleRow"), "Chamfer angle (deg)", "45");

            makeSection(left, "SHOULDER");
            Box* sh = makeRow(left, "ShoulderRow");
            shoulderDiameterInput = makeField(sh, "Shoulder dia. (mm)", "0");
            shoulderLengthInput = makeField(sh, "Shoulder length (mm)", "0");
            shoulderTaperInput = makeField(makeRow(left, "ShoulderTaperRow"), "Shoulder taper (deg)", "45");

            makeSection(mid, "COLLAR");
            Box* col = makeRow(mid, "CollarRow");
            collarDiameterInput = makeField(col, "Collar dia. (mm)", "0");
            collarLengthInput = makeField(col, "Collar length (mm)", "40");

            buildDefaults(mid, /*stepdown*/ true, /*stepover*/ true, /*cutDir*/ true);
        }

        void buildThreadMillFields(Box* left, Box* mid) {
            makeSection(left, "CUTTING");
            Box* cut = makeRow(left, "CutRow");
            diameterInput = makeField(cut, "Major dia. (mm)", "5.0");
            cuttingLengthInput = makeField(cut, "Threaded length (mm)", "10");
            pitchInput = makeField(makeRow(left, "PitchRow"), "Thread pitch (mm)", "0.8");

            // The reduced-diameter neck and the shank above it (stored in the
            // shoulder/collar fields, but presented as a thread mill's own terms).
            makeSection(left, "NECK");
            Box* neck = makeRow(left, "NeckRow");
            shoulderDiameterInput = makeField(neck, "Neck dia. (mm)", "0");
            shoulderLengthInput = makeField(neck, "Neck length (mm)", "0");
            shoulderTaperInput = makeField(makeRow(left, "NeckTaperRow"), "Neck taper (deg)", "45");

            makeSection(mid, "SHANK");
            Box* shank = makeRow(mid, "ShankRow");
            collarDiameterInput = makeField(shank, "Shank dia. (mm)", "0");
            collarLengthInput = makeField(shank, "Shank length (mm)", "40");

            // No stepdown: a thread mill's axial advance per revolution is the pitch.
            buildDefaults(mid, /*stepdown*/ false, /*stepover*/ true, /*cutDir*/ true);
        }

        void buildProbeFields(Box* left, Box* mid) {
            makeSection(left, "STYLUS");
            Box* st = makeRow(left, "StylusRow");
            diameterInput = makeField(st, "Ball dia. (mm)", "2.0");
            cuttingLengthInput = makeField(st, "Stylus length (mm)", "20");

            makeSection(left, "SHANK");
            Box* shank = makeRow(left, "ShankRow");
            collarDiameterInput = makeField(shank, "Shank dia. (mm)", "4");
            collarLengthInput = makeField(shank, "Shank length (mm)", "40");

            // A probe doesn't cut: only the motion speeds matter.
            buildDefaults(mid, /*stepdown*/ false, /*stepover*/ false, /*cutDir*/ false);
        }

        // Rebuild the field columns for the current tool type, then re-bind live
        // preview and re-populate from `working` (so a type switch carries values).
        void rebuildFields() {

            if (leftColumn) { delete leftColumn; leftColumn = nullptr; }
            if (midColumn)  { delete midColumn;  midColumn = nullptr; }

            diameterInput = cuttingLengthInput = taperInput = pitchInput = nullptr;
            shoulderDiameterInput = shoulderLengthInput = shoulderTaperInput = nullptr;
            collarDiameterInput = collarLengthInput = nullptr;
            feedRateInput = stepdownInput = stepoverInput = rapidSpeedInput = nullptr;
            cutDirectionDropdown = nullptr;
            lengthLabel = nullptr;

            leftColumn = new Box(fieldsArea, { &ToolSettingsLayout::LeftColumn }, "Left");
            midColumn  = new Box(fieldsArea, { &ToolSettingsLayout::MidColumn }, "Mid");

            switch (currentType()) {
                case Cam::App::Tool::Type::ThreadMill: buildThreadMillFields(leftColumn, midColumn); break;
                case Cam::App::Tool::Type::Chamfer:    buildChamferFields(leftColumn, midColumn); break;
                case Cam::App::Tool::Type::Probe:      buildProbeFields(leftColumn, midColumn); break;
                default:                               buildEndMillFields(leftColumn, midColumn); break;
            }

            lengthLabel = new Text(
                midColumn,
                "Overall length: -",
                Theme::layer({ &ToolSettingsLayout::LengthLabel }, { &Theme::Styles::MutedText })
            );

            bindLive(diameterInput);
            bindLive(cuttingLengthInput);
            bindLive(taperInput);
            bindLive(pitchInput);
            bindLive(shoulderDiameterInput);
            bindLive(shoulderLengthInput);
            bindLive(shoulderTaperInput);
            bindLive(collarDiameterInput);
            bindLive(collarLengthInput);

            populateInputs();
        }

        void buildUi() {

            Cam::App::Tool* tool = app ? app->toolLibrary()->find(toolName) : nullptr;
            const Cam::App::Tool::Type selectedType = tool ? tool->type : Cam::App::Tool::Type::EndMill;

            working = tool ? *tool : Cam::App::Tool();
            working.name = tool ? tool->name : toolName;
            working.type = selectedType;

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
                working.name,
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

            // Persistent content column (identity + the rebuilt field area) on the
            // left; the preview on the right.
            Box* content = new Box(body, { &ToolSettingsLayout::ContentColumn }, "Content");
            Box* previewColumn = new Box(body, { &ToolSettingsLayout::PreviewColumn }, "Preview");

            // Identity (persistent across type changes)
            Box* identityRow = makeRow(content, "IdentityRow");

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
            nameInput->text->content = working.name;

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
                captureWorking();                 // keep edits from the old menu
                working.type = currentType();
                updateHeaderEyebrow(working.type);
                rebuildFields();                  // swap in the new type's menu
                syncPreview(e);
                refresh(e);
            };

            // The per-type field area, rebuilt on every type change.
            fieldsArea = new Box(content, { &ToolSettingsLayout::FieldsArea }, "FieldsArea");

            // Preview (persistent)
            new Text(
                previewColumn,
                "PREVIEW",
                Theme::layer({}, { &Theme::Styles::SettingsSectionLabel })
            );
            preview = new ToolPreview(previewColumn);

            rebuildFields();

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

        // Set the currently-built inputs from `working`.  Fields absent for the
        // tool type are simply skipped.
        void populateInputs() {

            if (diameterInput)       { diameterInput->setValue(working.diameter); }
            if (cuttingLengthInput)  { cuttingLengthInput->setValue(working.cuttingLength); }
            if (taperInput)          { taperInput->setValue(working.taperAngle); }
            if (pitchInput)          { pitchInput->setValue(working.threadPitch); }
            if (shoulderDiameterInput) { shoulderDiameterInput->setValue(working.shoulderDiameter); }
            if (shoulderLengthInput) { shoulderLengthInput->setValue(working.shoulderLength); }
            if (shoulderTaperInput)  { shoulderTaperInput->setValue(working.shoulderTaperAngle); }
            if (collarDiameterInput) { collarDiameterInput->setValue(working.collarDiameter); }
            if (collarLengthInput)   { collarLengthInput->setValue(working.collarLength); }

            if (feedRateInput)   { feedRateInput->setValue(working.defaultFeedRate); }
            if (stepdownInput)   { stepdownInput->setValue(working.defaultStepdown); }
            if (stepoverInput)   { stepoverInput->setValue(working.defaultStepover * 100.0); }
            if (rapidSpeedInput) { rapidSpeedInput->setValue(working.defaultRapidSpeed); }

            if (cutDirectionDropdown) {
                cutDirectionDropdown->params.value = working.defaultClimbMilling ? "climb" : "conventional";
                cutDirectionDropdown->dropdownText->content = working.defaultClimbMilling ? "Climb" : "Conventional";
            }
        }

        // Read every currently-built input back into `working` (fields not present
        // for the active type keep their carried-over value).
        void captureWorking() {

            if (nameInput) { working.name = nameInput->text->content.get(); }
            working.type = currentType();

            if (diameterInput)       { working.diameter = diameterInput->valueOr(working.diameter); }
            working.radius = working.diameter * 0.5;
            if (cuttingLengthInput)  { working.cuttingLength = cuttingLengthInput->valueOr(working.cuttingLength); }
            if (taperInput)          { working.taperAngle = taperInput->valueOr(working.taperAngle); }
            if (pitchInput)          { working.threadPitch = pitchInput->valueOr(working.threadPitch); }
            if (shoulderDiameterInput) { working.shoulderDiameter = shoulderDiameterInput->valueOr(working.shoulderDiameter); }
            if (shoulderLengthInput) { working.shoulderLength = shoulderLengthInput->valueOr(working.shoulderLength); }
            if (shoulderTaperInput)  { working.shoulderTaperAngle = shoulderTaperInput->valueOr(working.shoulderTaperAngle); }
            if (collarDiameterInput) { working.collarDiameter = collarDiameterInput->valueOr(working.collarDiameter); }
            if (collarLengthInput)   { working.collarLength = collarLengthInput->valueOr(working.collarLength); }

            if (feedRateInput)   { working.defaultFeedRate = feedRateInput->valueOr(working.defaultFeedRate); }
            if (stepdownInput)   { working.defaultStepdown = stepdownInput->valueOr(working.defaultStepdown); }
            if (stepoverInput)   { working.defaultStepover = stepoverInput->valueOr(working.defaultStepover * 100.0) / 100.0; }
            if (rapidSpeedInput) { working.defaultRapidSpeed = rapidSpeedInput->valueOr(working.defaultRapidSpeed); }
            if (cutDirectionDropdown) { working.defaultClimbMilling = cutDirectionDropdown->params.value != "conventional"; }

            working.recomputeLength();
        }

        // Build a Tool from the current form (lenient — for preview and save).
        Cam::App::Tool currentTool() {
            captureWorking();
            return working;
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
                !nearlyEqual(t.threadPitch, savedTool.threadPitch) ||
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

            // Commit any focused field before reading (only those the active
            // tool type built).
            auto commitIf = [&](NumberInput* in) { if (in) { in->commit(e); } };
            commitIf(diameterInput);
            commitIf(cuttingLengthInput);
            commitIf(taperInput);
            commitIf(pitchInput);
            commitIf(shoulderDiameterInput);
            commitIf(shoulderLengthInput);
            commitIf(shoulderTaperInput);
            commitIf(collarDiameterInput);
            commitIf(collarLengthInput);
            commitIf(feedRateInput);
            commitIf(stepdownInput);
            commitIf(stepoverInput);
            commitIf(rapidSpeedInput);

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
