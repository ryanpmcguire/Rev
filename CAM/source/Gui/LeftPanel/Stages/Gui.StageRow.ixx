module;

#include <string>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.StageRow;

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
import Cam.App.Stage;
import Cam.Gui.Theme;
import Cam.Gui.ToolPathSettingsWindow;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace StageRowStyle::Styles {

        // The whole row is now a vertical container: a header line plus the
        // collapsible list of component visibility toggles beneath it.
        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .bottom = 2_px },
            .padding = { .left = 4_px, .right = 4_px, .top = 4_px, .bottom = 4_px },
            .border = { .radius = 6_px },
            .cursor = Cursor::Hand
        };

        Style Header = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct }
        };

        Style IndexLabel = {
            .margin = { .right = 4_px },
            .text = { .size = 11_px }
        };

        Style IndexLabelSelected = {
            .text = { .size = 11_px }
        };

        Style Content = {
            .layout = { Axis::Vertical, Align::Center, Align::Start, Wrap::False },
            .overflow = Overflow::Hide,
            //.size = { Grow() }
        };

        Style Label = {
            .overflow = Overflow::Hide,
            .text = { .size = 13_px, .wrap = Wrap::False }
        };

        Style Subtitle = {
            .margin = { .top = 1_px },
            .text = { .size = 10_px }
        };

        Style SubtitleWorking = {
            .text = { .size = 10_px }
        };

        Style SubtitleChanged = {
            .text = { .size = 10_px }
        };

        // Component toggle list
        //--------------------------------------------------

        Style Components = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .top = 4_px },
            .padding = { .left = 18_px }
        };

        Style ComponentRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .padding = { .top = 2_px, .bottom = 2_px },
            .cursor = Cursor::Hand
        };

        Style ComponentLabel = {
            .overflow = Overflow::Hide,
            .text = { .size = 11_px, .wrap = Wrap::False }
        };

        Style SettingsButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 22_px, .height = 22_px },
            .margin = { .left = 10_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 2_px, .bottom = 2_px },
            .cursor = Cursor::Hand
        };

        Style DeleteButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 22_px, .height = 22_px },
            .margin = { .left = 6_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 2_px, .bottom = 2_px },
            .cursor = Cursor::Hand
        };

        Style SettingsIcon = {
            .size = { 15_px, 15_px }
        };

        Style DeleteIcon = {
            .size = { 15_px, 15_px }
        };

        Style ToolDropdown = {
            .margin = { .left = 6_px, .top = 0_px, .bottom = 0_px, .right = 0_px }
        };

        Style ToolDropdownLabelHidden = {
            .visibility = { Visibility::Hidden }
        };

        Style ToolDropdownField = {
            .margin = { .top = 0_px, .bottom = 0_px },
            .padding = { .left = 6_px, .right = 4_px, .top = 4_px, .bottom = 4_px },
            .overflow = Overflow::Hide
        };

        Style ToolDropdownFieldText = {
            .overflow = Overflow::Hide,
            .text = { .size = 11_px, .wrap = Wrap::False }
        };
    };

    using namespace StageRowStyle;

    struct StageRow : public Box {

        Cam::App::AppState* app = nullptr;
        Cam::App::Stage* state = nullptr;

        size_t index = 0;

        // The stage's inspectable components, in display order. Each maps to a
        // visibility flag on the App stage and (where geometry exists) a 3D actor.
        static constexpr int ComponentCount = 5;

        static const char* componentName(int component) {
            switch (component) {
                case 0:  return "Prior Model";
                case 1:  return "Model";
                case 2:  return "Operation";
                case 3:  return "Delta";
                case 4:  return "Toolpath";
                default: return "";
            }
        }

        bool* componentFlag(int component) {
            if (!state) { return nullptr; }
            switch (component) {
                case 0:  return &state->visible.priorModel;
                case 1:  return &state->visible.model;
                case 2:  return &state->visible.operation;
                case 3:  return &state->visible.delta;
                case 4:  return &state->visible.toolPath;
                default: return nullptr;
            }
        }

        Box* header = nullptr;
        Text* indexLabel = nullptr;
        Box* content = nullptr;
        Text* label = nullptr;
        Text* subtitle = nullptr;
        Dropdown* toolDropdown = nullptr;
        Box* settingsButton = nullptr;
        Svg* settingsIcon = nullptr;
        Box* deleteButton = nullptr;
        Svg* deleteIcon = nullptr;

        Box* components = nullptr;
        Box* componentRows[ComponentCount] = {};
        Text* componentLabels[ComponentCount] = {};

        std::function<void(Event&, Cam::App::Stage*)> onSelect;
        std::function<void(Event&, Cam::App::Stage*, const std::string&)> onToolPathToolChanged;
        std::function<void(Event&, Cam::App::Stage*)> onOpenToolPathSettings;
        std::function<void(Event&, Cam::App::Stage*)> onDelete;
        std::function<void(Event&)> onComponentToggled;

        StageRow(Element* parent, StyleList styles = {}) : Box(parent, styles, "StageRow") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&Styles::Self);
            this->styles.add(&Theme::Styles::Row);
            this->styles.add(&Theme::Styles::RowHover);

            header = new Box(this, { &Styles::Header }, "StageRowHeader");

            indexLabel = new Text(
                header,
                "0",
                Theme::layer(
                    { &Styles::IndexLabel },
                    { &Theme::Styles::MutedText }
                )
            );

            content = new Box(header, { &Styles::Content }, "StageRowContent");

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

            toolDropdown = new Dropdown(
                header,
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
            toolDropdown->dropdownArrow->styles.add(&Theme::Styles::Icon);
            toolDropdown->dropdownArrow->styles.add(&Theme::Styles::IconHover);
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
                header,
                { &Styles::SettingsButton },
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

                settingsButton->onClick([this](Event& e) {
                    if (!canEditToolPath()) { return; }
                    if (onOpenToolPathSettings && state) {
                        onOpenToolPathSettings(e, state);
                    }
                    e.propagate = false;
                });

            deleteButton = new Box(
                header,
                { &Styles::DeleteButton },
                "DeleteStageRowButton"
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
                    "DeleteStageRowIcon"
                );

                deleteButton->onClick([this](Event& e) {

                    if (!canDelete()) { return; }

                    Cam::App::Project* project = activeProject();

                    if (onDelete && project && index < project->stages.size()) {
                        onDelete(e, project->stages[index]);
                    }

                    e.propagate = false;
                });

            // Component visibility toggles
            //--------------------------------------------------

            components = new Box(this, { &Styles::Components }, "StageRowComponents");

            for (int i = 0; i < ComponentCount; i++) {

                componentRows[i] = new Box(
                    components,
                    { &Styles::ComponentRow },
                    "StageComponentRow"
                );

                componentRows[i]->styles.add(&Theme::Styles::Row);
                componentRows[i]->styles.add(&Theme::Styles::RowHover);

                componentLabels[i] = new Text(
                    componentRows[i],
                    componentName(i),
                    Theme::layer(
                        { &Styles::ComponentLabel },
                        { &Theme::Styles::Text }
                    )
                );

                componentRows[i]->onClick([this, i](Event& e) {

                    e.propagate = false;

                    if (bool* flag = componentFlag(i)) {
                        *flag = !*flag;
                    }

                    if (onComponentToggled) {
                        onComponentToggled(e);
                    }
                });
            }

            this->onClick([this](Event& e) {
                if (onSelect && state) { onSelect(e, state); }
            });
        }

        Cam::App::Project* activeProject() {

            if (!app) { return nullptr; }

            return app->activeProject;
        }

        Cam::App::Stage* displayedState() {

            Cam::App::Project* project = activeProject();

            if (!project) { return nullptr; }

            return project->displayedStage;
        }

        void setState(Cam::App::Stage* state, size_t index) {

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

            return index < project->stages.size();
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

            // Component toggles reflect the stage's visibility flags. A dot
            // glyph + muted styling marks a hidden component.
            if (components) {
                components->style->visibility = state
                    ? Visibility::Visible
                    : Visibility::Hidden;
            }

            for (int i = 0; i < ComponentCount; i++) {

                if (!componentLabels[i]) { continue; }

                const bool* flag = componentFlag(i);
                const bool on = flag ? *flag : true;

                componentLabels[i]->content =
                    std::string(on ? "[x] " : "[ ] ") + componentName(i);

                if (on) { componentLabels[i]->styles.remove(&Theme::Styles::MutedText); }
                else    { componentLabels[i]->styles.add(&Theme::Styles::MutedText); }
            }

            const bool showToolDropdown = canEditToolPath();

            if (toolDropdown) {
                toolDropdown->style->visibility = showToolDropdown
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
