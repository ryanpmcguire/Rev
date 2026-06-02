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
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { Grow() },
            .padding = { 0_px, 22_px, 18_px, 22_px }
        };

        Style FullWidthSection = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct }
        };

        Style DimensionsRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct, Grow(), .min = { .height = 200_px } },
            .margin = { 0_px, 0_px, 14_px, 0_px }
        };

        Style DimensionsHost = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { Grow() },
            .margin = { 0_px, 16_px, 0_px, 0_px }
        };

        Style PreviewHost = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 220_px, .height = 100_pct }
        };

        Style PreviewPanel = {
            .size = { 100_pct, Grow(), .min = { .height = 160_px } }
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

        NumberInput* cuttingDiameterInput = nullptr;
        NumberInput* cuttingLengthInput = nullptr;
        NumberInput* totalLengthInput = nullptr;
        NumberInput* cuttingTaperInput = nullptr;
        NumberInput* shoulderDiameterInput = nullptr;
        NumberInput* shoulderLengthInput = nullptr;
        NumberInput* shoulderTaperInput = nullptr;

        Cam::App::Tool::GeometryDriver lastGeometryDriver =
            Cam::App::Tool::GeometryDriver::TotalLength;

        bool suppressGeometryReconcile = false;

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
                .size = { .width = 720, .height = 620 },
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
            const char* placeholder,
            bool allowZero = true
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

            if (allowZero) {
                p.min = 0.0;
            }

            return p;
        }

        void buildUi() {

            Cam::App::Tool* tool = app ? app->toolLibrary()->find(toolName) : nullptr;
            const std::string displayName = tool ? tool->name : toolName;
            const Cam::App::Tool::Type selectedType = tool ? tool->type : Cam::App::Tool::Type::EndMill;

            auto row = [](Element* parent, const char* name) {
                return new Box(parent, Theme::layer({ &ToolSettingsLayout::Row }, {}), name);
            };

            auto sectionLabel = [](Element* parent, const char* text) {
                new Text(parent, text, Theme::layer({}, { &Theme::Styles::SettingsSectionLabel }));
            };

            NumberInput::Params cuttingDiameterParams = numberParams("Cutting diameter (mm)", "1.0", false);
            cuttingDiameterParams.min = 1e-6;

            NumberInput::Params totalLengthParams = numberParams("Total length (mm)", "100", false);
            totalLengthParams.min = 1e-6;

            const auto footerSecondaryStyles = Theme::layer({
                &ToolSettingsLayout::FooterButton,
                &Theme::Styles::ButtonHover,
                &Theme::Styles::ButtonPress
            }, {
                &Theme::Styles::Button,
                &Theme::Styles::ButtonLabel
            });

            Box* root = new Box(this, Theme::withSettingsDialog({ &ToolSettingsLayout::Root }), "SettingsRoot");

                Box* header = new Box(root, Theme::layer({ &ToolSettingsLayout::Header }, { &Theme::Styles::SettingsHeader }), "Header");
                    headerEyebrow = new Text(header, Cam::App::Tool::typeEyebrow(selectedType), Theme::layer({}, { &Theme::Styles::SettingsHeaderEyebrow }));
                    headerTitle = new Text(header, displayName, Theme::layer({}, { &Theme::Styles::SettingsHeaderTitle }));

                Box* body = new Box(root, Theme::layer({ &ToolSettingsLayout::Body, &Theme::Styles::SettingsBody }, { &Theme::Styles::Text }), "Body");
                    nameInput = new TextInput(body, { .label = "Name", .placeholder = "Tool name", .maxLength = 64, .selectAllOnFocus = true });
                    typeDropdown = new Dropdown(body, {
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
                        updateSaveButtonAppearance(e);
                        syncPreview(e);
                        refresh(e);
                    };

                    Box* dimensionsRow = new Box(body, { &ToolSettingsLayout::DimensionsRow }, "DimensionsRow");

                        Box* dimensionsHost = new Box(dimensionsRow, { &ToolSettingsLayout::DimensionsHost }, "DimensionsHost");
                            sectionLabel(dimensionsHost, "TOOL DIMENSIONS");
                            Box* dimRow1 = row(dimensionsHost, "DimRow1");
                                cuttingDiameterInput = new NumberInput(dimRow1, cuttingDiameterParams, { &ToolSettingsLayout::RowField });
                                cuttingLengthInput = new NumberInput(dimRow1, numberParams("Cutting length (mm)", "20"), { &ToolSettingsLayout::RowField });
                            Box* dimRow2 = row(dimensionsHost, "DimRow2");
                                totalLengthInput = new NumberInput(dimRow2, totalLengthParams, { &ToolSettingsLayout::RowField });
                                cuttingTaperInput = new NumberInput(dimRow2, numberParams("Cutting taper (deg)", "0"), { &ToolSettingsLayout::RowField });
                            Box* dimRow3 = row(dimensionsHost, "DimRow3");
                                shoulderDiameterInput = new NumberInput(dimRow3, numberParams("Shoulder diameter (mm)", "0"), { &ToolSettingsLayout::RowField });
                                shoulderLengthInput = new NumberInput(dimRow3, numberParams("Shoulder length (mm)", "0"), { &ToolSettingsLayout::RowField });
                            Box* dimRow4 = row(dimensionsHost, "DimRow4");
                                shoulderTaperInput = new NumberInput(dimRow4, numberParams("Shoulder taper (deg)", "45"), { &ToolSettingsLayout::RowField });

                        Box* previewHost = new Box(dimensionsRow, { &ToolSettingsLayout::PreviewHost }, "PreviewHost");
                            sectionLabel(previewHost, "PREVIEW");
                            preview = new ToolPreview(previewHost, Theme::layer({ &ToolSettingsLayout::PreviewPanel }, { &Theme::Styles::SettingsPreview }));

                    Box* toolpathSection = new Box(body, { &ToolSettingsLayout::FullWidthSection }, "ToolpathDefaults");
                        sectionLabel(toolpathSection, "TOOLPATH DEFAULTS");
                        Box* defRow1 = row(toolpathSection, "DefRow1");
                            feedRateInput = new NumberInput(defRow1, numberParams("Feed rate (mm/min)", "250", false), { &ToolSettingsLayout::RowField });
                            stepdownInput = new NumberInput(defRow1, numberParams("Stepdown (mm)", "0.5"), { &ToolSettingsLayout::RowField });
                            stepoverInput = new NumberInput(defRow1, numberParams("Stepover (% dia.)", "25", false), { &ToolSettingsLayout::RowField });
                        Box* defRow2 = row(toolpathSection, "DefRow2");
                            rapidSpeedInput = new NumberInput(defRow2, numberParams("Rapid speed (mm/s)", "10", false), { &ToolSettingsLayout::RowField });
                            cutDirectionDropdown = new Dropdown(defRow2, {
                                .label = "Cut direction",
                                .options = { { "Climb", "climb" }, { "Conventional", "conventional" } },
                                .placeholder = "Select direction",
                                .value = "climb"
                            }, { &ToolSettingsLayout::RowField });
                            cutDirectionDropdown->onChange = [this](Event& e) { updateSaveButtonAppearance(e); refresh(e); };

                Box* footer = new Box(root, Theme::layer({ &ToolSettingsLayout::Footer }, { &Theme::Styles::SettingsFooter }), "Footer");
                    Button* cancelButton = new Button(footer, Button::Params::Secondary("Cancel"), footerSecondaryStyles);
                    cancelButton->onClick([this](Event& e) { requestClose(&e); e.propagate = false; });
                    saveButton = new Button(footer, Button::Params::Secondary(isUnsavedNewTool ? "Save tool" : "Save changes"), footerSecondaryStyles);
                    saveButton->onClick([this](Event& e) { save(e); e.propagate = false; });
                    Button* okButton = new Button(footer, Button::Params::Primary("OK"), { &ToolSettingsLayout::FooterButton });
                    okButton->onClick([this](Event& e) { if (save(e)) { close(&e); } e.propagate = false; });

            auto onGeometryCommitted = [this](Cam::App::Tool::GeometryDriver driver, Event& e) {
                reconcileGeometryFromDriver(driver, e);
                updateSaveButtonAppearance(e);
                syncPreview(e);
                refresh(e);
            };

            auto hookGeometryEdit = [this, onGeometryCommitted](NumberInput* input, Cam::App::Tool::GeometryDriver driver) {
                input->onTextInput([this](Event& e) { updateSaveButtonAppearance(e); syncPreview(e); refresh(e); });
                input->onValueChange = [this, driver, onGeometryCommitted](Event& e, std::optional<double>) { onGeometryCommitted(driver, e); };
            };

            hookGeometryEdit(cuttingDiameterInput, Cam::App::Tool::GeometryDriver::CuttingDiameter);
            hookGeometryEdit(cuttingLengthInput, Cam::App::Tool::GeometryDriver::CuttingLength);
            hookGeometryEdit(totalLengthInput, Cam::App::Tool::GeometryDriver::TotalLength);
            hookGeometryEdit(cuttingTaperInput, Cam::App::Tool::GeometryDriver::CuttingTaper);
            hookGeometryEdit(shoulderDiameterInput, Cam::App::Tool::GeometryDriver::ShoulderDiameter);
            hookGeometryEdit(shoulderLengthInput, Cam::App::Tool::GeometryDriver::ShoulderLength);
            hookGeometryEdit(shoulderTaperInput, Cam::App::Tool::GeometryDriver::ShoulderTaper);

            auto onFieldEdited = [this](Event& e, std::optional<double>) { updateSaveButtonAppearance(e); syncPreview(e); refresh(e); };
            auto hookLiveNumberEdit = [this, onFieldEdited](NumberInput* input) {
                input->onTextInput([this](Event& e) { updateSaveButtonAppearance(e); syncPreview(e); refresh(e); });
                input->onValueChange = onFieldEdited;
            };

            hookLiveNumberEdit(feedRateInput);
            hookLiveNumberEdit(stepdownInput);
            hookLiveNumberEdit(stepoverInput);
            hookLiveNumberEdit(rapidSpeedInput);
            nameInput->onTextInput([this](Event& e) { updateSaveButtonAppearance(e); refresh(e); });

            populateFrom(tool);
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

        void readGeometryFromForm(
            double& totalLength,
            double& cuttingLength,
            double& shoulderLength,
            double& shoulderDiameter,
            double& cuttingDiameter,
            double& cuttingTaperAngle,
            double& shoulderTaperAngle
        ) const {
            cuttingDiameter = cuttingDiameterInput->valueOr(1.0);
            cuttingLength = cuttingLengthInput->valueOr(20.0);
            totalLength = totalLengthInput->valueOr(100.0);
            cuttingTaperAngle = cuttingTaperInput->valueOr(0.0);
            shoulderDiameter = shoulderDiameterInput->valueOr(0.0);
            shoulderLength = shoulderLengthInput->valueOr(0.0);
            shoulderTaperAngle = shoulderTaperInput->valueOr(45.0);
        }

        void applyGeometryToForm(
            double totalLength,
            double cuttingLength,
            double shoulderLength,
            double shoulderDiameter,
            double cuttingDiameter,
            double cuttingTaperAngle,
            double shoulderTaperAngle
        ) {
            suppressGeometryReconcile = true;

            cuttingDiameterInput->setValue(cuttingDiameter);
            cuttingLengthInput->setValue(cuttingLength);
            totalLengthInput->setValue(totalLength);
            cuttingTaperInput->setValue(cuttingTaperAngle);
            shoulderDiameterInput->setValue(shoulderDiameter);
            shoulderLengthInput->setValue(shoulderLength);
            shoulderTaperInput->setValue(shoulderTaperAngle);

            suppressGeometryReconcile = false;
        }

        void reconcileGeometryFromDriver(
            Cam::App::Tool::GeometryDriver driver,
            Event&
        ) {
            if (suppressGeometryReconcile) { return; }

            lastGeometryDriver = driver;

            double totalLength = 0.0;
            double cuttingLength = 0.0;
            double shoulderLength = 0.0;
            double shoulderDiameter = 0.0;
            double cuttingDiameter = 0.0;
            double cuttingTaperAngle = 0.0;
            double shoulderTaperAngle = 0.0;

            readGeometryFromForm(
                totalLength,
                cuttingLength,
                shoulderLength,
                shoulderDiameter,
                cuttingDiameter,
                cuttingTaperAngle,
                shoulderTaperAngle
            );

            Cam::App::Tool::reconcileGeometry(
                totalLength,
                cuttingLength,
                shoulderLength,
                shoulderDiameter,
                cuttingDiameter,
                cuttingTaperAngle,
                shoulderTaperAngle,
                driver
            );

            applyGeometryToForm(
                totalLength,
                cuttingLength,
                shoulderLength,
                shoulderDiameter,
                cuttingDiameter,
                cuttingTaperAngle,
                shoulderTaperAngle
            );
        }

        void populateFrom(Cam::App::Tool* tool) {

            Cam::App::Tool t = tool ? *tool : Cam::App::Tool();

            if (!tool && isUnsavedNewTool) {
                t.name = toolName;
            }

            Cam::App::Tool::reconcileGeometry(
                t.length,
                t.cuttingLength,
                t.shoulderLength,
                t.shoulderDiameter,
                t.diameter,
                t.taperAngle,
                t.shoulderTaperAngle,
                Cam::App::Tool::GeometryDriver::TotalLength
            );

            nameInput->text->content = tool ? tool->name : toolName;

            applyGeometryToForm(
                t.length,
                t.cuttingLength,
                t.shoulderLength,
                t.shoulderDiameter,
                t.diameter,
                t.taperAngle,
                t.shoulderTaperAngle
            );

            feedRateInput->setValue(t.defaultFeedRate);
            stepdownInput->setValue(t.defaultStepdown);
            stepoverInput->setValue(t.defaultStepover * 100.0);
            rapidSpeedInput->setValue(t.defaultRapidSpeed);

            cutDirectionDropdown->params.value = t.defaultClimbMilling ? "climb" : "conventional";
            cutDirectionDropdown->dropdownText->content = t.defaultClimbMilling ? "Climb" : "Conventional";
        }

        ToolPreview::Geometry currentGeometry() {

            ToolPreview::Geometry g;

            double cuttingDiameter = cuttingDiameterInput->valueOr(1.0);
            double totalLength = totalLengthInput->valueOr(100.0);
            double cuttingLength = cuttingLengthInput->valueOr(20.0);
            double cuttingTaper = cuttingTaperInput->valueOr(0.0);
            double shoulderDiameter = shoulderDiameterInput->valueOr(0.0);
            double shoulderLength = shoulderLengthInput->valueOr(0.0);
            double shoulderTaper = shoulderTaperInput->valueOr(45.0);

            g.cuttingRadius = cuttingDiameter * 0.5;
            g.totalLength = totalLength;
            g.cuttingLength = cuttingLength;
            g.cuttingTaperAngle = cuttingTaper;
            g.shoulderDiameter = shoulderDiameter;
            g.shoulderLength = shoulderLength;
            g.shoulderTaperAngle = shoulderTaper;

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
                cuttingDiameterInput->commit(e);
                cuttingLengthInput->commit(e);
                totalLengthInput->commit(e);
                cuttingTaperInput->commit(e);
                shoulderDiameterInput->commit(e);
                shoulderLengthInput->commit(e);
                shoulderTaperInput->commit(e);
                feedRateInput->commit(e);
                stepdownInput->commit(e);
                stepoverInput->commit(e);
                rapidSpeedInput->commit(e);
            }

            double cuttingDiameter = 0.0;
            double totalLength = 0.0;

            if (!cuttingDiameterInput->tryGetValue(cuttingDiameter) || cuttingDiameter <= 0.0) {
                return false;
            }

            if (!totalLengthInput->tryGetValue(totalLength) || totalLength <= 0.0) {
                return false;
            }

            out = Cam::App::Tool();
            out.name = name;
            out.type = currentType();

            out.diameter = cuttingDiameter;
            out.radius = cuttingDiameter * 0.5;
            out.length = totalLength;

            out.taperAngle = cuttingTaperInput->valueOr(0.0);
            out.cuttingLength = cuttingLengthInput->valueOr(20.0);
            out.shoulderDiameter = shoulderDiameterInput->valueOr(0.0);
            out.shoulderLength = shoulderLengthInput->valueOr(0.0);
            out.shoulderTaperAngle = shoulderTaperInput->valueOr(45.0);

            Cam::App::Tool::reconcileGeometry(
                out.length,
                out.cuttingLength,
                out.shoulderLength,
                out.shoulderDiameter,
                out.diameter,
                out.taperAngle,
                out.shoulderTaperAngle,
                lastGeometryDriver
            );

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
                !nearlyEqual(form.cuttingLength, savedTool.cuttingLength) ||
                !nearlyEqual(form.shoulderDiameter, savedTool.shoulderDiameter) ||
                !nearlyEqual(form.shoulderLength, savedTool.shoulderLength) ||
                !nearlyEqual(form.shoulderTaperAngle, savedTool.shoulderTaperAngle) ||
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
                    "Enter a valid name, cutting diameter, and total length before saving."
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
