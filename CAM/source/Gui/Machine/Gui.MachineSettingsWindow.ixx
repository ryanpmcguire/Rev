module;

#include <cmath>
#include <cstdio>
#include <string>
#include <optional>
#include <functional>
#include <filesystem>

#include <dbg.hpp>

export module Cam.Gui.MachineSettingsWindow;

import Rev.OS.File;

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
import Rev.Element.Checkbox;
import Rev.Element.ControlTheme;

import Cam.App;
import Cam.App.MachineProfile;
import Cam.App.MachineLibrary;
import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace MachineSettingsLayout {

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

        Style RightColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = Grow() }
        };

        Style Row = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct }
        };

        Style RowField = {
            .size = { Grow() },
            .margin = { .left = 4_px, .right = 4_px }
        };

        Style CheckboxRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { 100_pct },
            .margin = { .bottom = 4_px }
        };

        Style CheckboxField = {
            .margin = { 0_px, 12_px, 0_px, 0_px }
        };

        Style FileRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { 100_pct },
            .margin = { .bottom = 6_px }
        };

        Style FilePathField = {
            .size = { Grow() },
            .margin = { 0_px, 6_px, 0_px, 0_px }
        };

        Style SmallButton = {
            .margin = { .left = 4_px }
        };

        Style FolderHint = {
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

    struct MachineSettingsWindow : public Rev::Window {

        Cam::App::AppState* app = nullptr;
        std::string machineName;
        Cam::App::MachineProfile savedMachine;
        Cam::App::MachineStepSources savedStepSources;
        Cam::App::MachineStepSources pendingStepSources;
        bool isUnsavedNewMachine = false;
        bool switchingMachine = false;

        std::function<void(Event&)> onSaved;
        std::function<void(Event&)> onClosed;

        Text* headerEyebrow = nullptr;
        Text* headerTitle = nullptr;
        Text* folderHint = nullptr;

        TextInput* nameInput = nullptr;
        Dropdown* machineDropdown = nullptr;

        NumberInput* spindleMinInput = nullptr;
        NumberInput* spindleMaxInput = nullptr;

        Checkbox* axisXCheckbox = nullptr;
        Checkbox* axisYCheckbox = nullptr;
        Checkbox* axisZCheckbox = nullptr;
        Checkbox* rotaryXCheckbox = nullptr;
        Checkbox* rotaryYCheckbox = nullptr;
        Checkbox* rotaryZCheckbox = nullptr;

        TextInput* spindleStepInput = nullptr;
        TextInput* bedStepInput = nullptr;
        TextInput* workpieceStepInput = nullptr;
        TextInput* rotaryStepInput = nullptr;

        Button* applyButton = nullptr;
        bool applyPendingAppearance = false;

        static std::string windowTitleFor(const std::string& name) {
            return name + " - Machine Settings";
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

        static std::string basename(const std::string& path) {
            if (path.empty()) { return ""; }

            std::filesystem::path p(path);
            return p.filename().string();
        }

        static std::string truncatePath(const std::string& value, size_t maxChars = 36) {
            if (value.size() <= maxChars) { return value; }
            if (maxChars <= 3) { return value.substr(0, maxChars); }
            return "..." + value.substr(value.size() - (maxChars - 3));
        }

        std::string currentStorageFolder() const {
            if (!app) { return ""; }
            return app->machineStorageFolder(machineName);
        }

        MachineSettingsWindow(
            Rev::Window* owner,
            const std::string& initialMachineName
        ) : Rev::Window(
            owner,
            {
                .name = initialMachineName + " - Machine Settings",
                .size = { .width = 820, .height = 560 },
                .minimizeButton = false,
                .maximizeButton = false
            }
        ) {
            machineName = initialMachineName;

            if (owner && owner->shared) {
                shared->state = owner->shared->state;
            }

            app = Cam::App::AppState::Get(shared->state);
            isUnsavedNewMachine = app && app->isUnsavedMachine(machineName);

            style->layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
            style->size = { .width = 100_pct, .height = 100_pct };

            buildUi();
            captureSavedFields();
            updateApplyButtonAppearance(event);

            setTitle(machineName + " - Machine Settings");

            if (owner) {
                setPos(owner->details.x + 180, owner->details.y + 60);
            }
            else {
                setPos(180, 60);
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
            p.maxDecimalPlaces = 2;
            return p;
        }

        void buildUi() {
            Cam::App::MachineProfile* machine = app ? app->machineLibrary.find(machineName) : nullptr;
            const std::string displayName = machine ? machine->name : machineName;

            Box* root = new Box(
                this,
                Theme::withSettingsDialog({ &MachineSettingsLayout::Root }),
                "MachineSettingsRoot"
            );

            Box* header = new Box(
                root,
                Theme::layer(
                    { &MachineSettingsLayout::Header },
                    { &Theme::Styles::SettingsHeader }
                ),
                "Header"
            );

            headerEyebrow = new Text(
                header,
                "MACHINE DEFINITION",
                Theme::layer({}, { &Theme::Styles::SettingsHeaderEyebrow })
            );

            headerTitle = new Text(
                header,
                displayName,
                Theme::layer({}, { &Theme::Styles::SettingsHeaderTitle })
            );

            Box* body = new Box(
                root,
                Theme::layer(
                    { &MachineSettingsLayout::Body, &Theme::Styles::SettingsBody },
                    { &Theme::Styles::Text }
                ),
                "Body"
            );

            Box* left = new Box(body, { &MachineSettingsLayout::LeftColumn }, "Left");
            Box* mid = new Box(body, { &MachineSettingsLayout::MidColumn }, "Mid");
            Box* right = new Box(body, { &MachineSettingsLayout::RightColumn }, "Right");

            auto row = [&](Box* column, const char* name) {
                return new Box(column, { &MachineSettingsLayout::Row }, name);
            };

            auto section = [&](Box* column, const char* text) {
                new Text(
                    column,
                    text,
                    Theme::layer({}, { &Theme::Styles::SettingsSectionLabel })
                );
            };

            auto field = [&](Box* parent, const char* label, const char* placeholder) {
                return new NumberInput(
                    parent,
                    numberParams(label, placeholder),
                    { &MachineSettingsLayout::RowField }
                );
            };

            section(left, "IDENTITY");

            Box* identityRow = row(left, "IdentityRow");
            nameInput = new TextInput(
                identityRow,
                {
                    .label = "Name",
                    .placeholder = "Machine name",
                    .maxLength = 64,
                    .selectAllOnFocus = true
                },
                { &MachineSettingsLayout::RowField }
            );

            machineDropdown = new Dropdown(
                identityRow,
                {
                    .label = "Machine",
                    .options = buildMachineOptions(),
                    .placeholder = "Select machine",
                    .value = machineName
                },
                { &MachineSettingsLayout::RowField }
            );

            machineDropdown->onChange = [this](Event& e) {
                if (switchingMachine) { return; }

                const std::string nextName = machineDropdown->params.value;
                if (nextName.empty() || nextName == machineName) { return; }

                if (!trySwitchMachine(nextName, e)) {
                    switchingMachine = true;
                    machineDropdown->params.value = machineName;
                    if (machineDropdown->dropdownText) {
                        machineDropdown->dropdownText->content = machineName;
                    }
                    switchingMachine = false;
                }

                refresh(e);
            };

            section(left, "STORAGE");

            folderHint = new Text(
                left,
                "",
                Theme::layer(
                    { &MachineSettingsLayout::FolderHint },
                    { &Theme::Styles::MutedText }
                )
            );

            Button* chooseFolderButton = new Button(
                left,
                Button::Params::Secondary("Choose library folder..."),
                { &MachineSettingsLayout::SmallButton }
            );

            chooseFolderButton->onClick([this](Event& e) {
                if (!app) { return; }

                if (!app->selectMachinesRoot()) { return; }

                reloadMachineContext(machineName, e);
                refresh(e);
                e.propagate = false;
            });

            section(left, "SPINDLE");
            Box* spindleRow = row(left, "SpindleRow");
            spindleMinInput = field(spindleRow, "Min RPM", "0");
            spindleMaxInput = field(spindleRow, "Max RPM", "24000");

            section(mid, "LINEAR AXES");
            Box* linearRow = new Box(mid, { &MachineSettingsLayout::CheckboxRow }, "LinearAxesRow");
            axisXCheckbox = new Checkbox(linearRow, { .label = "X", .def = true }, { &MachineSettingsLayout::CheckboxField });
            axisYCheckbox = new Checkbox(linearRow, { .label = "Y", .def = true }, { &MachineSettingsLayout::CheckboxField });
            axisZCheckbox = new Checkbox(linearRow, { .label = "Z", .def = true }, { &MachineSettingsLayout::CheckboxField });

            section(mid, "ROTARY SUPPORT");
            Box* rotaryRow = new Box(mid, { &MachineSettingsLayout::CheckboxRow }, "RotaryAxesRow");
            rotaryXCheckbox = new Checkbox(rotaryRow, { .label = "A (X)", .def = false }, { &MachineSettingsLayout::CheckboxField });
            rotaryYCheckbox = new Checkbox(rotaryRow, { .label = "B (Y)", .def = false }, { &MachineSettingsLayout::CheckboxField });
            rotaryZCheckbox = new Checkbox(rotaryRow, { .label = "C (Z)", .def = false }, { &MachineSettingsLayout::CheckboxField });

            section(right, "STEP MODELS");
            new Text(
                right,
                "Files are copied beside the machine JSON on save.",
                Theme::layer(
                    { &MachineSettingsLayout::FolderHint },
                    { &Theme::Styles::MutedText }
                )
            );

            spindleStepInput = addStepFileRow(right, "Spindle");
            bedStepInput = addStepFileRow(right, "Bed");
            workpieceStepInput = addStepFileRow(right, "Workpiece");
            rotaryStepInput = addStepFileRow(right, "Rotary axis");

            populateFrom(machine);
            syncFolderHint();

            Box* footer = new Box(
                root,
                Theme::layer(
                    { &MachineSettingsLayout::Footer },
                    { &Theme::Styles::SettingsFooter }
                ),
                "Footer"
            );

            Button* newMachineButton = new Button(
                footer,
                Button::Params::Secondary("New Machine"),
                { &MachineSettingsLayout::FooterButton }
            );

            newMachineButton->onClick([this](Event& e) {
                if (!app) { return; }

                if (hasUnsavedChanges(e)) {
                    Rev::OS::UnsavedChangesResult result =
                        Rev::OS::Dialog::UnsavedChanges(windowTitleFor(machineName));

                    if (result == Rev::OS::UnsavedChangesResult::Cancel) { return; }

                    if (result == Rev::OS::UnsavedChangesResult::Save) {
                        if (!save(e)) { return; }
                    }
                    else if (isUnsavedNewMachine) {
                        discardIfUnsaved();
                    }
                }

                std::string newName;
                if (!app->createNewMachine(newName)) { return; }

                reloadMachineContext(newName, e);
                refresh(e);
                e.propagate = false;
            });

            Button* cancelButton = new Button(
                footer,
                Button::Params::Secondary("Cancel"),
                { &MachineSettingsLayout::FooterButton }
            );

            cancelButton->onClick([this](Event& e) {
                requestClose(&e);
                e.propagate = false;
            });

            applyButton = new Button(
                footer,
                Button::Params::Secondary("Apply"),
                { &MachineSettingsLayout::FooterButton }
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
                { &MachineSettingsLayout::FooterButton }
            );

            saveButton->onClick([this](Event& e) {
                if (save(e)) {
                    updateApplyButtonAppearance(e);
                    close(&e);
                }
                e.propagate = false;
            });
        }

        TextInput* addStepFileRow(Box* column, const char* label) {
            Box* fileRow = new Box(column, { &MachineSettingsLayout::FileRow }, "StepFileRow");

            TextInput* pathInput = new TextInput(
                fileRow,
                {
                    .label = label,
                    .placeholder = "No file selected",
                    .maxLength = 512,
                    .selectAllOnFocus = true
                },
                { &MachineSettingsLayout::FilePathField }
            );

            Button* browseButton = new Button(
                fileRow,
                Button::Params::Secondary("Browse..."),
                { &MachineSettingsLayout::SmallButton }
            );

            Button* clearButton = new Button(
                fileRow,
                Button::Params::Secondary("Clear"),
                { &MachineSettingsLayout::SmallButton }
            );

            browseButton->onClick([this, pathInput, label](Event& e) {
                Rev::OS::File selected;

                std::string initialDir = currentStorageFolder();
                if (initialDir.empty() && app) {
                    initialDir = app->machinesRootPath;
                }

                if (!selected.open(
                    std::string("Select STEP file - ") + label,
                    "STEP Files\0*.step;*.stp\0All Files\0*.*\0",
                    initialDir
                )) {
                    return;
                }

                setStepInputPath(pathInput, selected.pathname);
                setPendingSourceForInput(pathInput, selected.pathname);
                refresh(e);
                e.propagate = false;
            });

            clearButton->onClick([this, pathInput](Event& e) {
                pathInput->text->content = "";
                setPendingSourceForInput(pathInput, "");
                refresh(e);
                e.propagate = false;
            });

            return pathInput;
        }

        void setStepInputPath(TextInput* input, const std::string& path) {
            if (!input || !input->text) { return; }
            input->text->content = path.empty() ? "" : truncatePath(path);
        }

        void setPendingSourceForInput(TextInput* input, const std::string& path) {
            if (input == spindleStepInput) { pendingStepSources.spindle = path; }
            else if (input == bedStepInput) { pendingStepSources.bed = path; }
            else if (input == workpieceStepInput) { pendingStepSources.workpiece = path; }
            else if (input == rotaryStepInput) { pendingStepSources.rotary = path; }
        }

        std::string pendingSourceForInput(TextInput* input) const {
            if (input == spindleStepInput) { return pendingStepSources.spindle; }
            if (input == bedStepInput) { return pendingStepSources.bed; }
            if (input == workpieceStepInput) { return pendingStepSources.workpiece; }
            if (input == rotaryStepInput) { return pendingStepSources.rotary; }
            return "";
        }

        std::vector<Dropdown::Item> buildMachineOptions() {
            std::vector<Dropdown::Item> items;

            if (!app) { return items; }

            for (const std::string& name : app->machineLibrary.order) {
                items.push_back({ name, name });
            }

            if (items.empty()) {
                items.push_back({ machineName, machineName });
            }

            return items;
        }

        void syncFolderHint() {
            if (!folderHint) { return; }

            const std::string storageFolder = currentStorageFolder();

            if (storageFolder.empty()) {
                folderHint->content = "Folder: (created on save)";
                return;
            }

            folderHint->content = "Folder: " + truncatePath(storageFolder, 52);
        }

        void reloadMachineContext(const std::string& nextName, Event& e) {
            std::string resolved = nextName;

            if (app && !app->machineLibrary.find(resolved)) {
                if (!app->machineLibrary.empty()) {
                    resolved = app->machineLibrary.order.front();
                }
            }

            machineName = resolved;
            isUnsavedNewMachine = app && app->isUnsavedMachine(machineName);

            if (app) {
                app->selectMachineByName(machineName);
            }

            Cam::App::MachineProfile* machine = app ? app->machineLibrary.find(machineName) : nullptr;

            if (headerTitle) {
                headerTitle->content = machine ? machine->name : machineName;
            }

            setTitle(windowTitleFor(machineName));

            switchingMachine = true;

            if (machineDropdown) {
                machineDropdown->params.options = buildMachineOptions();
                machineDropdown->params.value = machineName;
                if (machineDropdown->dropdownText) {
                    machineDropdown->dropdownText->content = machineName;
                }
            }

            switchingMachine = false;

            pendingStepSources = {};
            populateFrom(machine);
            captureSavedFields();
            syncFolderHint();
            updateApplyButtonAppearance(e);
        }

        bool trySwitchMachine(const std::string& nextName, Event& e) {
            if (hasUnsavedChanges(e)) {
                Rev::OS::UnsavedChangesResult result =
                    Rev::OS::Dialog::UnsavedChanges(windowTitleFor(machineName));

                if (result == Rev::OS::UnsavedChangesResult::Cancel) { return false; }

                if (result == Rev::OS::UnsavedChangesResult::Save) {
                    if (!save(e)) { return false; }
                }
                else if (isUnsavedNewMachine) {
                    discardIfUnsaved();
                }
            }

            reloadMachineContext(nextName, e);
            return true;
        }

        void populateFrom(Cam::App::MachineProfile* machine) {
            Cam::App::MachineProfile m = machine ? *machine : Cam::App::MachineProfile();

            nameInput->text->content = machine ? machine->name : machineName;

            spindleMinInput->setValue(m.spindleMinRpm);
            spindleMaxInput->setValue(m.spindleMaxRpm);

            axisXCheckbox->value = m.axisX;
            axisYCheckbox->value = m.axisY;
            axisZCheckbox->value = m.axisZ;
            rotaryXCheckbox->value = m.rotaryX;
            rotaryYCheckbox->value = m.rotaryY;
            rotaryZCheckbox->value = m.rotaryZ;

            const std::string folder = currentStorageFolder();

            auto displaySavedStep = [&](TextInput* input, const std::string& relativeName) {
                if (!input) { return; }

                if (relativeName.empty()) {
                    setStepInputPath(input, "");
                    return;
                }

                setStepInputPath(
                    input,
                    Cam::App::MachineLibrary::resolveStepPath(folder, relativeName)
                );
            };

            displaySavedStep(spindleStepInput, m.spindleStep);
            displaySavedStep(bedStepInput, m.bedStep);
            displaySavedStep(workpieceStepInput, m.workpieceStep);
            displaySavedStep(rotaryStepInput, m.rotaryStep);

            pendingStepSources = {};
        }

        Cam::App::MachineProfile currentMachine() {
            Cam::App::MachineProfile m;

            m.name = nameInput->text->content.get();

            m.spindleMinRpm = spindleMinInput->valueOr(0.0);
            m.spindleMaxRpm = spindleMaxInput->valueOr(24000.0);

            m.axisX = axisXCheckbox->value;
            m.axisY = axisYCheckbox->value;
            m.axisZ = axisZCheckbox->value;
            m.rotaryX = rotaryXCheckbox->value;
            m.rotaryY = rotaryYCheckbox->value;
            m.rotaryZ = rotaryZCheckbox->value;

            Cam::App::MachineProfile* existing = app ? app->machineLibrary.find(machineName) : nullptr;

            if (existing) {
                m.spindleStep = existing->spindleStep;
                m.bedStep = existing->bedStep;
                m.workpieceStep = existing->workpieceStep;
                m.rotaryStep = existing->rotaryStep;
                m.filePath = existing->filePath;
            }

            if (pendingStepSources.spindle.empty() && spindleStepInput->text->content.get().empty()) {
                m.spindleStep.clear();
            }
            if (pendingStepSources.bed.empty() && bedStepInput->text->content.get().empty()) {
                m.bedStep.clear();
            }
            if (pendingStepSources.workpiece.empty() && workpieceStepInput->text->content.get().empty()) {
                m.workpieceStep.clear();
            }
            if (pendingStepSources.rotary.empty() && rotaryStepInput->text->content.get().empty()) {
                m.rotaryStep.clear();
            }

            return m;
        }

        Cam::App::MachineStepSources currentStepSources() const {
            return pendingStepSources;
        }

        void captureSavedFields() {
            Cam::App::MachineProfile* machine = app ? app->machineLibrary.find(machineName) : nullptr;

            if (machine) {
                savedMachine = *machine;
            }
            else {
                savedMachine = Cam::App::MachineProfile();
                savedMachine.name = machineName;
            }

            savedStepSources = {};
            pendingStepSources = {};
        }

        bool hasUnsavedChanges(Event&) {
            if (isUnsavedNewMachine) { return true; }

            const Cam::App::MachineProfile m = currentMachine();
            const Cam::App::MachineStepSources steps = currentStepSources();

            return (
                m.name != savedMachine.name ||
                !nearlyEqual(m.spindleMinRpm, savedMachine.spindleMinRpm) ||
                !nearlyEqual(m.spindleMaxRpm, savedMachine.spindleMaxRpm) ||
                m.axisX != savedMachine.axisX ||
                m.axisY != savedMachine.axisY ||
                m.axisZ != savedMachine.axisZ ||
                m.rotaryX != savedMachine.rotaryX ||
                m.rotaryY != savedMachine.rotaryY ||
                m.rotaryZ != savedMachine.rotaryZ ||
                !steps.spindle.empty() ||
                !steps.bed.empty() ||
                !steps.workpiece.empty() ||
                !steps.rotary.empty() ||
                (spindleStepInput && spindleStepInput->text->content.get().empty() && !savedMachine.spindleStep.empty()) ||
                (bedStepInput && bedStepInput->text->content.get().empty() && !savedMachine.bedStep.empty()) ||
                (workpieceStepInput && workpieceStepInput->text->content.get().empty() && !savedMachine.workpieceStep.empty()) ||
                (rotaryStepInput && rotaryStepInput->text->content.get().empty() && !savedMachine.rotaryStep.empty())
            );
        }

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
                dbg("[MachineSettings] Missing app state");
                return false;
            }

            spindleMinInput->commit(e);
            spindleMaxInput->commit(e);

            Cam::App::MachineProfile m = currentMachine();
            Cam::App::MachineStepSources steps = currentStepSources();

            if (m.name.empty()) {
                Rev::OS::Dialog::Warning(
                    "Machine Settings",
                    "Enter a machine name before saving."
                );
                return false;
            }

            if (m.spindleMaxRpm < m.spindleMinRpm) {
                Rev::OS::Dialog::Warning(
                    "Machine Settings",
                    "Maximum spindle speed must be greater than or equal to the minimum."
                );
                return false;
            }

            if (!m.axisX && !m.axisY && !m.axisZ && !m.rotaryX && !m.rotaryY && !m.rotaryZ) {
                Rev::OS::Dialog::Warning(
                    "Machine Settings",
                    "Enable at least one linear or rotary axis."
                );
                return false;
            }

            if (!app->saveMachine(machineName, m, steps)) {
                dbg("[MachineSettings] Failed to save machine \"%s\"", machineName.c_str());
                Rev::OS::Dialog::Error(
                    "Machine Settings",
                    "Could not save the machine definition. Check the name and folder, then try again."
                );
                return false;
            }

            machineName = m.name;
            isUnsavedNewMachine = false;
            captureSavedFields();

            if (headerTitle) {
                headerTitle->content = machineName;
            }

            setTitle(windowTitleFor(machineName));

            switchingMachine = true;
            if (machineDropdown) {
                machineDropdown->params.options = buildMachineOptions();
                machineDropdown->params.value = machineName;
                if (machineDropdown->dropdownText) {
                    machineDropdown->dropdownText->content = machineName;
                }
            }
            switchingMachine = false;

            Cam::App::MachineProfile* saved = app->machineLibrary.find(machineName);
            populateFrom(saved);
            syncFolderHint();

            if (onSaved) {
                onSaved(e);
            }

            refresh(e);
            return true;
        }

        void discardIfUnsaved() {
            if (!app || !isUnsavedNewMachine) { return; }

            app->removeMachine(machineName);
            isUnsavedNewMachine = false;
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
                Rev::OS::Dialog::UnsavedChanges(windowTitleFor(machineName), owner);

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
                Rev::OS::Dialog::UnsavedChanges(windowTitleFor(machineName), owner);

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
