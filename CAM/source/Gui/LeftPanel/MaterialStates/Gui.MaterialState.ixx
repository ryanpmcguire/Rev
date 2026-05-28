module;

#include <string>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.MaterialState;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;
import Rev.Element.Dropdown;

import Cam.App;
import Cam.App.Project;
import Cam.App.MaterialState;
import Cam.Gui.Theme;
import Cam.Gui.ToolPathSettingsWindow;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace MaterialStateStyle::Styles {

        Style Self = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .bottom = 2_px },
            .padding = { 4_px, 8_px, 4_px, 8_px },
            .border = { .radius = 6_px },
            .cursor = Cursor::Hand
        };

        Style IndexLabel = {
            .margin = { 0_px, 8_px, 0_px, 0_px },
            .text = { .size = 11_px }
        };

        Style IndexLabelSelected = {
            .text = { .size = 11_px }
        };

        Style Content = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = Grow() }
        };

        Style Label = {
            .size = { .width = Grow() },
            .text = { .size = 13_px }
        };

        Style Subtitle = {
            .margin = { 1_px, 0_px, 0_px, 0_px },
            .text = { .size = 10_px }
        };

        Style SubtitleWorking = {
            .text = { .size = 10_px }
        };

        Style SubtitleChanged = {
            .text = { .size = 10_px }
        };

        Style IconButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { 0_px, 0_px, 0_px, 2_px },
            .padding = { 3_px, 4_px, 3_px, 4_px },
            .cursor = Cursor::Hand
        };

        Style SettingsIcon = {
            .size = { 15_px, 15_px }
        };

        Style DeleteIcon = {
            .size = { 15_px, 15_px }
        };

        Style ToolDropdownHost = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { 0_px, 0_px, 0_px, 4_px },
            .size = { .width = 152_px }
        };

        Style ToolDropdown = {
            .size = { .width = 100_pct, .height = Grow() }
        };

        Style ToolDropdownLabelHidden = {
            .visibility = Visibility::Hidden
        };

        Style ToolDropdownField = {
            .padding = { 6_px, 8_px, 6_px, 8_px }
        };

        Style ToolDropdownFieldText = {
            .text = { .wrap = Wrap::False }
        };
    };

    using namespace MaterialStateStyle;

    struct MaterialState : public Box {

        Cam::App::AppState* app = nullptr;
        Cam::App::MaterialState* state = nullptr;

        size_t index = 0;

        Text* indexLabel = nullptr;
        Box* content = nullptr;
        Text* label = nullptr;
        Text* subtitle = nullptr;
        Box* toolDropdownHost = nullptr;
        Dropdown* toolDropdown = nullptr;
        Box* settingsButton = nullptr;
        Svg* settingsIcon = nullptr;
        Box* deleteButton = nullptr;
        Svg* deleteIcon = nullptr;

        std::function<void(Event&, Cam::App::MaterialState*)> onSelect;
        std::function<void(Event&, Cam::App::MaterialState*, const std::string&)> onToolPathToolChanged;
        std::function<void(Event&, Cam::App::MaterialState*)> onOpenToolPathSettings;
        std::function<void(Event&, Cam::App::MaterialState*)> onDelete;

        MaterialState(Element* parent, StyleList styles = {}) : Box(parent, styles, "MaterialState") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&Styles::Self);
            this->styles.add(&Theme::Styles::Row);
            this->styles.add(&Theme::Styles::RowHover);

            indexLabel = new Text(
                this,
                "0",
                Theme::layer(
                    { &Styles::IndexLabel },
                    { &Theme::Styles::MutedText }
                )
            );

            content = new Box(this, { &Styles::Content }, "MaterialStateContent");

            label = new Text(
                content,
                "",
                Theme::layer(
                    { &Styles::Label },
                    { &Theme::Styles::Text }
                )
            );
            subtitle = new Text(
                content,
                "",
                Theme::layer(
                    { &Styles::Subtitle },
                    { &Theme::Styles::MutedText }
                )
            );

            toolDropdownHost = new Box(
                this,
                { &Styles::ToolDropdownHost },
                "ToolPathToolHost"
            );

            toolDropdownHost->onClick([](Event& e) {
                e.propagate = false;
            });

            toolDropdown = new Dropdown(
                toolDropdownHost,
                {
                    .label = "Tool",
                    .options = ToolPathSettingsWindow::toolOptions(app),
                    .placeholder = "Tool",
                    .value = ""
                },
                { &Styles::ToolDropdown }
            );

            toolDropdown->label->styles.add(&Styles::ToolDropdownLabelHidden);
            toolDropdown->dropdown->styles.add(&Styles::ToolDropdownField);
            toolDropdown->dropdownText->styles.add(&Styles::ToolDropdownFieldText);

            toolDropdown->onChange = [this](Event& e) {

                e.propagate = false;

                if (!canEditToolPath() || !state || !toolDropdown) { return; }

                const std::string toolName = toolDropdown->params.value;

                if (toolName.empty() || toolName == state->toolPath.toolName) { return; }

                if (onToolPathToolChanged) {
                    onToolPathToolChanged(e, state, toolName);
                }
            };

            settingsButton = new Box(
                this,
                { &Styles::IconButton },
                "ToolPathSettingsButton"
            );

            settingsIcon = new Svg(
                settingsButton,
                File("./ToolPath/Settings.svg"),
                Theme::layer({
                    &Styles::SettingsIcon,
                    &Theme::Styles::IconHover,
                    &Theme::Styles::IconDisabled
                }, {
                    &Theme::Styles::Icon
                }),
                "ToolPathSettingsIcon"
            );

            deleteButton = new Box(
                this,
                { &Styles::IconButton },
                "DeleteMaterialStateButton"
            );

            deleteIcon = new Svg(
                deleteButton,
                File("./Close.svg"),
                Theme::layer({
                    &Styles::DeleteIcon,
                    &Theme::Styles::DeleteIconHover,
                    &Theme::Styles::IconDisabled
                }, {
                    &Theme::Styles::Icon
                }),
                "DeleteMaterialStateIcon"
            );

            settingsButton->onClick([this](Event& e) {
                if (!canEditToolPath()) { return; }
                if (onOpenToolPathSettings && state) {
                    onOpenToolPathSettings(e, state);
                }
                e.propagate = false;
            });

            deleteButton->onClick([this](Event& e) {

                if (!canDelete()) { return; }

                Cam::App::Project* project = activeProject();

                if (onDelete && project && index < project->states.size()) {
                    onDelete(e, project->states[index]);
                }

                e.propagate = false;
            });

            this->onClick([this](Event& e) {
                if (onSelect && state) { onSelect(e, state); }
            });
        }

        Cam::App::Project* activeProject() {

            if (!app) { return nullptr; }

            return app->activeProject;
        }

        Cam::App::MaterialState* displayedState() {

            Cam::App::Project* project = activeProject();

            if (!project) { return nullptr; }

            return project->displayedState;
        }

        void setState(Cam::App::MaterialState* state, size_t index) {

            this->state = state;
            this->index = index;
        }

        std::string stateName() {

            if (!state) { return "Invalid State"; }

            if (!state->name.empty()) {
                return state->name;
            }

            if (state->working) {
                return "Working State";
            }

            if (index == 0) { return "Final State"; }

            return "Material State " + std::to_string(index);
        }

        std::string stateSubtitle() {

            if (!state) { return ""; }

            if (state->working && state->model.changed) {
                return "Unsaved changes";
            }

            if (state->working) {
                return "Editing";
            }

            if (state->hasToolPath) {
                return "Toolpath ready";
            }

            if (index == 0) {
                return "Final geometry";
            }

            return "Committed";
        }

        bool canDelete() {

            if (index == 0) { return false; }

            Cam::App::Project* project = activeProject();

            if (!project) { return false; }

            return index < project->states.size();
        }

        bool canEditToolPath() const {

            if (!state) { return false; }

            return state->parent != nullptr;
        }

        void computeChildren(Event& e) override {

            bool selected = state && activeProject() && activeProject()->isViewSelected(state);
            bool working = state && state->working;
            bool changed = state && state->working && state->model.changed;

            if (indexLabel) {
                indexLabel->content = std::to_string(index + 1);
            }

            if (label) {
                label->content = stateName();
            }

            if (subtitle) {
                subtitle->content = stateSubtitle();
            }

            if (deleteIcon) {
                deleteIcon->resolved.disabled = !canDelete();
            }

            if (settingsIcon) {
                settingsIcon->resolved.disabled = !canEditToolPath();
            }

            const bool showToolDropdown = canEditToolPath();

            if (toolDropdownHost) {
                toolDropdownHost->style->visibility = showToolDropdown
                    ? Visibility::Visible
                    : Visibility::Hidden;
            }

            if (toolDropdown && showToolDropdown) {

                toolDropdown->params.options = ToolPathSettingsWindow::toolOptions(app);

                const std::string toolName = state->toolPath.toolName;

                if (!toolName.empty()) {

                    const Dropdown::Item item = toolDropdown->getItemWithVal(toolName);

                    if (item.value == toolName) {
                        toolDropdown->params.value = toolName;
                        toolDropdown->dropdownText->content = item.name;
                    }
                }

                else {
                    toolDropdown->params.value = "";
                    toolDropdown->dropdownText->content = toolDropdown->params.placeholder;
                }

                toolDropdown->resolved.disabled = false;
            }

            else if (toolDropdown) {
                toolDropdown->resolved.disabled = true;
            }

            if (selected) { styles.add(&Theme::Styles::RowSelected); }
            else { styles.remove(&Theme::Styles::RowSelected); }

            if (indexLabel) {
                if (selected) { indexLabel->styles.add(&Theme::Styles::AccentText); }
                else { indexLabel->styles.remove(&Theme::Styles::AccentText); }
            }

            if (subtitle) {
                subtitle->styles.remove(&Theme::Styles::WarningText);

                if (changed) {
                    subtitle->styles.add(&Theme::Styles::WarningText);
                }
            }

            Box::computeChildren(e);
        }
    };
}
