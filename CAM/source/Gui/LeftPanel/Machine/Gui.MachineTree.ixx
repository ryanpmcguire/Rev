module;

#include <string>
#include <vector>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.MachineTree;

import Rev.Core.Resource;
import Rev.OS.File;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;
import Rev.Element.Dropdown;
import Rev.Element.Collapsible;
import Rev.Window;

import Cam.App;
import Cam.App.MachineProfile;
import Cam.Gui.Theme;
import Cam.Gui.MachineSettingsWindow;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace MachineTreeStyle::Styles {

        Style Field = {
            .size = { .width = Grow() },
            .margin = { .top = 0_px, .bottom = 0_px }
        };

        Style HiddenLabel = {
            .visibility = { Visibility::Hidden }
        };

        Style Spacer = {
            .size = { .width = Grow() }
        };

        // A leaf row in the machine tree (DOF item, component item).
        Style Row = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .padding = { .left = 4_px, .right = 2_px, .top = 3_px, .bottom = 3_px }
        };

        Style Label = {
            .text = { .size = 12_px, .wrap = Wrap::False }
        };

        // The X/Y/Z degrees of freedom are laid out horizontally in a group.
        Style DofGroup = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .padding = { .left = 4_px, .top = 2_px, .bottom = 3_px }
        };

        // A single X/Y/Z item: checkbox + label, compact, sitting side by side.
        Style DofItem = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .margin = { .right = 16_px }
        };

        // Compact checkbox for a degree of freedom.
        Style Checkbox = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { 15_px, 15_px },
            .margin = { .right = 5_px },
            .border = { .color = rgba(140, 150, 165, 1.0), .radius = 3_px, .width = 1_px },
            .cursor = Cursor::Hand
        };

        Style CheckMark = {
            .size = { 12_px, 12_px }
        };

        // Small icon button (settings, file-select).
        Style IconButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { .right = 4_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 2_px, .bottom = 2_px },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };

        Style SmallIcon = {
            .size = { 14_px, 14_px }
        };

        Style Eye = {
            .size = { 15_px, 15_px }
        };

        Style EyeButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { .right = 2_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 2_px, .bottom = 2_px },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };
    };

    using namespace MachineTreeStyle;

    // The "Machine" tree node shown above the stage list. Its header carries the
    // machine-select dropdown and a settings button (opening the full machine
    // settings window). Its body exposes the machine's degrees of freedom
    // (translation / rotation checkboxes) and renderable components (each with a
    // visibility eye and a compact STEP-file selector) — a quick way to inspect
    // and tweak the machine without opening the advanced window.
    struct MachineTree : public Collapsible {

        Cam::App::AppState* app = nullptr;

        Dropdown* machineDropdown = nullptr;
        Box* settingsButton = nullptr;

        MachineSettingsWindow* settingsWindow = nullptr;

        // Degrees of freedom: 0..2 translation X/Y/Z, 3..5 rotation X/Y/Z.
        static constexpr int DofCount = 6;
        Box* dofCheckbox[DofCount] = {};
        Svg* dofMark[DofCount] = {};

        // Components: 0 spindle, 1 base, 2 workpiece, 3 rotary, 4 tool.
        static constexpr int CompCount = 5;
        Box* compEyeButton[CompCount] = {};
        Svg* compEye[CompCount] = {};
        Box* compFileButton[CompCount] = {};

        Rev::Core::Resource eyeOnResource;
        Rev::Core::Resource eyeOffResource;

        std::function<void(Event&)> onChanged;

        MachineTree(Element* parent, StyleList styles = {})
            : Collapsible(parent, "", styles, /*startOpen*/ true) {

            app = Cam::App::AppState::Get(shared->state);

            eyeOnResource  = File("CAM/source/Gui/LeftPanel/Stages/Eye.svg");
            eyeOffResource = File("CAM/source/Gui/LeftPanel/Stages/Eye-Off.svg");

            // Header: machine dropdown + settings button.
            machineDropdown = new Dropdown(
                header,
                {
                    .label = "Machine",
                    .options = machineOptions(),
                    .placeholder = "Machine",
                    .value = app ? app->selectedMachineName : ""
                },
                { &Styles::Field }
            );

            machineDropdown->label->styles.add(&Styles::HiddenLabel);

            machineDropdown->onChange = [this](Event& e) {
                if (app) { app->selectMachineByName(machineDropdown->params.value); }
                if (onChanged) { onChanged(e); }
                refresh(e);
            };

            settingsButton = new Box(header, { &Styles::IconButton }, "MachineSettingsButton");
            new Svg(
                settingsButton,
                File("CAM/source/Gui/LeftPanel/Stages/ToolPath/Settings.svg"),
                Theme::layer({ &Styles::SmallIcon }, { &Theme::Styles::Icon, &Theme::Styles::IconHover }),
                "MachineSettingsIcon"
            );
            settingsButton->onClick([this](Event& e) {
                e.propagate = false;
                openSettings(e);
            });

            themeCollapsible(this);

            buildDegreesOfFreedom();
            buildComponents();
        }

        ~MachineTree() {
            retireSettingsWindow();
        }

        // Build
        //--------------------------------------------------

        // Make a collapsible's chevron and title follow the active colour theme
        // (the framework default chevron colour is theme-agnostic).
        static void themeCollapsible(Collapsible* c) {
            if (!c) { return; }
            if (c->arrow)     { c->arrow->styles.add(&Theme::Styles::Icon); }
            if (c->titleText) { c->titleText->styles.add(&Theme::Styles::Text); }
        }

        void buildDegreesOfFreedom() {

            Collapsible* dof = new Collapsible(
                container, "Degrees of freedom", { &CollapsibleStyle::Self }, false
            );
            themeCollapsible(dof);

            Collapsible* translation = new Collapsible(
                dof->container, "Translation", { &CollapsibleStyle::Self }, false
            );
            themeCollapsible(translation);
            Box* transGroup = new Box(translation->container, { &Styles::DofGroup }, "MachineTransGroup");
            buildDofRow(transGroup, 0, "X");
            buildDofRow(transGroup, 1, "Y");
            buildDofRow(transGroup, 2, "Z");

            Collapsible* rotation = new Collapsible(
                dof->container, "Rotation", { &CollapsibleStyle::Self }, false
            );
            themeCollapsible(rotation);
            Box* rotGroup = new Box(rotation->container, { &Styles::DofGroup }, "MachineRotGroup");
            buildDofRow(rotGroup, 3, "X");
            buildDofRow(rotGroup, 4, "Y");
            buildDofRow(rotGroup, 5, "Z");
        }

        void buildDofRow(Element* parent, int i, const std::string& label) {

            Box* row = new Box(parent, { &Styles::DofItem }, "MachineDofItem");

            dofCheckbox[i] = new Box(row, { &Styles::Checkbox }, "MachineDofCheckbox");

            dofMark[i] = new Svg(
                dofCheckbox[i],
                File("Rev/src/Elements/Controls/Checkbox/check.svg"),
                Theme::layer({ &Styles::CheckMark }, { &Theme::Styles::AccentText }),
                "MachineDofCheck"
            );

            new Text(row, label, Theme::layer({ &Styles::Label }, { &Theme::Styles::Text }));

            dofCheckbox[i]->onClick([this, i](Event& e) {
                e.propagate = false;
                toggleDof(i, e);
            });
        }

        void buildComponents() {

            Collapsible* comps = new Collapsible(
                container, "Components", { &CollapsibleStyle::Self }, false
            );
            themeCollapsible(comps);

            buildComponentRow(comps->container, 0, "Spindle",   true);
            buildComponentRow(comps->container, 1, "Base",      true);
            buildComponentRow(comps->container, 2, "Workpiece", true);
            buildComponentRow(comps->container, 3, "Rotary",    true);
            // The Tool is the one currently loaded from the tool library — it has
            // no STEP asset of its own, so no file selector.
            buildComponentRow(comps->container, 4, "Tool",      false);
        }

        void buildComponentRow(Element* parent, int i, const std::string& label, bool hasFile) {

            Box* row = new Box(parent, { &Styles::Row }, "MachineComponentRow");

            new Text(row, label, Theme::layer({ &Styles::Label }, { &Theme::Styles::Text }));

            new Box(row, { &Styles::Spacer }, "MachineComponentSpacer");

            // Compact STEP-file selector (components backed by a STEP asset).
            if (hasFile) {

                compFileButton[i] = new Box(row, { &Styles::IconButton }, "MachineComponentFileButton");
                new Svg(
                    compFileButton[i],
                    File("CAM/source/Gui/RightPanel/Choose-Folder.svg"),
                    Theme::layer({ &Styles::SmallIcon }, { &Theme::Styles::Icon, &Theme::Styles::IconHover }),
                    "MachineComponentFileIcon"
                );
                compFileButton[i]->onClick([this, i](Event& e) {
                    e.propagate = false;
                    selectComponentFile(i, e);
                });
            }

            // Visibility eye.
            compEyeButton[i] = new Box(row, { &Styles::EyeButton }, "MachineComponentEyeButton");
            compEye[i] = new Svg(
                compEyeButton[i],
                eyeOnResource,
                Theme::layer({ &Styles::Eye }, { &Theme::Styles::Text }),
                "MachineComponentEye"
            );
            compEye[i]->opacity = 0.5f;

            compEyeButton[i]->onClick([this, i](Event& e) {
                e.propagate = false;
                if (bool* f = compVisibility(i)) { *f = !*f; }
                if (onChanged) { onChanged(e); }
                refresh(e);
            });
        }

        // Data access
        //--------------------------------------------------

        static bool* dofField(int i, Cam::App::MachineProfile* m) {
            if (!m) { return nullptr; }
            switch (i) {
                case 0: return &m->axisX;
                case 1: return &m->axisY;
                case 2: return &m->axisZ;
                case 3: return &m->rotaryX;
                case 4: return &m->rotaryY;
                case 5: return &m->rotaryZ;
                default: return nullptr;
            }
        }

        bool* compVisibility(int i) {
            if (!app) { return nullptr; }
            switch (i) {
                case 0: return &app->machineVisible.spindle;
                case 1: return &app->machineVisible.base;
                case 2: return &app->machineVisible.workpiece;
                case 3: return &app->machineVisible.rotary;
                case 4: return &app->machineVisible.tool;
                default: return nullptr;
            }
        }

        std::vector<Dropdown::Item> machineOptions() const {

            std::vector<Dropdown::Item> items;

            if (!app) { return items; }

            for (size_t i = 0; i < app->machineCount(); i++) {
                if (Cam::App::MachineProfile* mp = app->machineAt(i)) {
                    items.push_back({ mp->name, mp->name });
                }
            }

            if (items.empty()) {
                items.push_back({ "No machines", "" });
            }

            return items;
        }

        // Actions
        //--------------------------------------------------

        void toggleDof(int i, Event& e) {

            if (!app) { return; }

            Cam::App::MachineProfile* m = app->selectedMachine();
            if (!m) { return; }

            Cam::App::MachineProfile src = *m;

            if (bool* f = dofField(i, &src)) { *f = !*f; }

            app->saveMachine(m->name, src, {});

            if (onChanged) { onChanged(e); }

            refresh(e);
        }

        void selectComponentFile(int i, Event& e) {

            if (!app) { return; }

            Cam::App::MachineProfile* m = app->selectedMachine();
            if (!m) { return; }

            Rev::OS::File file;

            if (!file.open("Select STEP File", "STEP Files\0*.step;*.stp\0All Files\0*.*\0")) {
                return;
            }

            Cam::App::MachineProfile src = *m;
            Cam::App::MachineStepSources sources;

            switch (i) {
                case 0: sources.spindle   = file.pathname; break;
                case 1: sources.bed       = file.pathname; break;
                case 2: sources.workpiece = file.pathname; break;
                case 3: sources.rotary    = file.pathname; break;
                default: return;
            }

            app->saveMachine(m->name, src, sources);

            if (onChanged) { onChanged(e); }

            refresh(e);
        }

        // Settings window (owned by this element)
        //--------------------------------------------------

        void retireSettingsWindow() {

            if (!settingsWindow) { return; }

            settingsWindow->onSaved = nullptr;
            settingsWindow->onClosed = nullptr;
            settingsWindow->shouldClose = true;
            settingsWindow = nullptr;
        }

        void openSettings(Event& e) {

            if (!app) { return; }

            if (settingsWindow && !settingsWindow->shouldClose) {
                settingsWindow->show();
                return;
            }

            Rev::Window* owner = MachineSettingsWindow::rootWindow(this);
            if (!owner || !owner->shared) { return; }

            std::string name;

            if (Cam::App::MachineProfile* selected = app->selectedMachine()) {
                name = selected->name;
            }
            else if (!app->createNewMachine(name)) {
                return;
            }

            settingsWindow = new MachineSettingsWindow(owner, name);

            settingsWindow->onSaved = [this](Event& ev) {
                if (onChanged) { onChanged(ev); }
            };

            settingsWindow->onClosed = [this](Event& ev) {
                settingsWindow = nullptr;
                if (onChanged) { onChanged(ev); }
            };
        }

        // Compute
        //--------------------------------------------------

        // Structure + content only.
        void computeChildren(Event& e) override {

            if (settingsWindow && settingsWindow->shouldClose) {
                settingsWindow = nullptr;
            }

            if (machineDropdown) {
                machineDropdown->params.options = machineOptions();
                machineDropdown->params.value = app ? app->selectedMachineName : "";
            }
        }

        // Per-frame style/visual updates.
        void computeStyle(Event& e) override {

            Cam::App::MachineProfile* m = app ? app->selectedMachine() : nullptr;

            // Degrees of freedom: tick reflects the profile flag.
            for (int i = 0; i < DofCount; i++) {

                if (!dofMark[i]) { continue; }

                bool on = false;
                if (bool* f = dofField(i, m)) { on = *f; }

                dofMark[i]->style->visibility = on
                    ? Visibility::Visible
                    : Visibility::Hidden;
            }

            // Components: eye reflects the visibility request.
            for (int i = 0; i < CompCount; i++) {

                if (!compEye[i]) { continue; }

                bool on = true;
                if (bool* f = compVisibility(i)) { on = *f; }

                compEye[i]->resource = on ? eyeOnResource : eyeOffResource;

                const bool hover = compEyeButton[i] && compEyeButton[i]->targetFlags.hover;
                compEye[i]->opacity = hover ? 0.95f : (on ? 0.5f : 0.26f);
            }

            Element::computeStyle(e);
        }
    };
}
