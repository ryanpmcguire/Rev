module;

#include <string>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.MaterialState;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.ControlTheme;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

import Cam.App;
import Cam.App.Project;
import Cam.App.MaterialState;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;
    using namespace ControlTheme;

    namespace MaterialStateStyle::Styles {

        Style Self = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { 0_px, 0_px, 0_px, 5_px },
            .padding = { 8_px, 10_px, 8_px, 10_px },
            .background = { .color = rgba(255, 255, 255, 0.0), .transition = 120_ms },
            .border = {
                .color = rgba(226, 232, 240, 0.0),
                .width = 1_px,
                .radius = 8_px
            },
            .cursor = Cursor::Hand
        };

        Style Hover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(241, 245, 249, 1.0) },
            .border = { .color = rgba(226, 232, 240, 1.0), .width = 1_px }
        };

        Style Selected = {
            .background = { .color = rgba(79, 99, 255, 0.08) },
            .border = { .color = rgba(79, 99, 255, 1.0), .width = 1_px },
            .shadow = subtleShadow
        };

        Style Working = {
            .border = { .color = rgba(148, 163, 184, 1.0), .width = 1_px }
        };

        Style Changed = {
            .border = { .color = rgba(245, 158, 11, 1.0), .width = 1_px }
        };

        Style IndexBadge = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { 26_px, 26_px },
            .margin = { 0_px, 10_px, 0_px, 0_px },
            .background = { .color = rgba(241, 245, 249, 1.0), .transition = 120_ms },
            .border = {
                .color = rgba(226, 232, 240, 1.0),
                .width = 1_px,
                .radius = 8_px
            }
        };

        Style IndexBadgeSelected = {
            .background = { .color = rgba(79, 99, 255, 1.0) },
            .border = { .color = rgba(79, 99, 255, 1.0), .width = 1_px }
        };

        Style IndexBadgeLabel = {
            .text = { .color = rgba(100, 116, 139, 1.0), .size = 12_px }
        };

        Style IndexBadgeLabelSelected = {
            .text = { .color = rgba(255, 255, 255, 1.0), .size = 12_px }
        };

        Style Content = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = Grow() }
        };

        Style Label = {
            .size = { .width = Grow() },
            .text = { .color = rgba(30, 41, 59, 1.0), .size = 14_px }
        };

        Style Subtitle = {
            .margin = { 2_px, 0_px, 0_px, 0_px },
            .text = { .color = rgba(100, 116, 139, 1.0), .size = 11_px }
        };

        Style SubtitleWorking = {
            .text = { .color = rgba(148, 163, 184, 1.0), .size = 11_px }
        };

        Style SubtitleChanged = {
            .text = { .color = rgba(245, 158, 11, 1.0), .size = 11_px }
        };

        Style IconButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { 28_px, 28_px },
            .margin = { 0_px, 0_px, 0_px, 4_px },
            .padding = { 4_px, 4_px, 4_px, 4_px },
            .background = { .color = rgba(255, 255, 255, 1.0), .transition = 120_ms },
            .border = {
                .color = rgba(226, 232, 240, 1.0),
                .width = 1_px,
                .radius = 6_px
            },
            .cursor = Cursor::Hand
        };

        Style IconButtonHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(241, 245, 249, 1.0) },
            .border = { .color = rgba(148, 163, 184, 1.0) }
        };

        Style IconButtonSelected = {
            .background = { .color = rgba(255, 255, 255, 0.85) },
            .border = { .color = rgba(79, 99, 255, 0.35), .width = 1_px }
        };

        Style SettingsIcon = {
            .size = { 16_px, 16_px },
            .text = { .color = rgba(100, 116, 139, 1.0), .transition = 120_ms }
        };

        Style SettingsIconHover = {
            .applies = { .hover = true, .focus = true },
            .text = { .color = rgba(79, 99, 255, 1.0) }
        };

        Style SettingsIconDisabled = {
            .applies = { .disabled = true },
            .text = { .color = rgba(203, 213, 225, 1.0) },
            .cursor = Cursor::Default
        };

        Style DeleteIcon = {
            .size = { 16_px, 16_px },
            .text = { .color = rgba(148, 163, 184, 1.0), .transition = 120_ms }
        };

        Style DeleteIconHover = {
            .applies = { .hover = true, .focus = true },
            .text = { .color = rgba(239, 68, 68, 1.0) }
        };

        Style DeleteIconDisabled = {
            .applies = { .disabled = true },
            .text = { .color = rgba(203, 213, 225, 1.0) },
            .cursor = Cursor::Default
        };
    };

    using namespace MaterialStateStyle;

    struct MaterialState : public Box {

        Cam::App::AppState* app = nullptr;
        Cam::App::MaterialState* state = nullptr;

        size_t index = 0;

        Box* indexBadge = nullptr;
        Text* indexLabel = nullptr;
        Box* content = nullptr;
        Text* label = nullptr;
        Text* subtitle = nullptr;
        Box* settingsButton = nullptr;
        Svg* settingsIcon = nullptr;
        Box* deleteButton = nullptr;
        Svg* deleteIcon = nullptr;

        std::function<void(Event&, Cam::App::MaterialState*)> onSelect;
        std::function<void(Event&, Cam::App::MaterialState*)> onOpenToolPathSettings;
        std::function<void(Event&, Cam::App::MaterialState*)> onDelete;

        MaterialState(Element* parent, StyleList styles = {}) : Box(parent, styles, "MaterialState") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&Styles::Self);
            this->styles.add(&Styles::Hover);

            indexBadge = new Box(this, { &Styles::IndexBadge }, "MaterialStateIndex");
            indexLabel = new Text(indexBadge, "0", { &Styles::IndexBadgeLabel });

            content = new Box(this, { &Styles::Content }, "MaterialStateContent");

            label = new Text(content, "", { &Styles::Label });
            subtitle = new Text(content, "", { &Styles::Subtitle });

            settingsButton = new Box(
                this,
                { &Styles::IconButton, &Styles::IconButtonHover },
                "ToolPathSettingsButton"
            );

            settingsIcon = new Svg(
                settingsButton,
                File("./ToolPath/Settings.svg"),
                {
                    &Styles::SettingsIcon,
                    &Styles::SettingsIconHover,
                    &Styles::SettingsIconDisabled
                },
                "ToolPathSettingsIcon"
            );

            deleteButton = new Box(
                this,
                { &Styles::IconButton, &Styles::IconButtonHover },
                "DeleteMaterialStateButton"
            );

            deleteIcon = new Svg(
                deleteButton,
                File("./delete.svg"),
                {
                    &Styles::DeleteIcon,
                    &Styles::DeleteIconHover,
                    &Styles::DeleteIconDisabled
                },
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
                if (onDelete && state) { onDelete(e, state); }
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

            if (!state) { return false; }
            if (!state->parent) { return false; }

            return true;
        }

        bool canEditToolPath() const {

            if (!state) { return false; }

            return state->parent != nullptr;
        }

        void computeChildren(Event& e) override {

            bool selected = state && displayedState() == state;
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

            if (selected) { styles.add(&Styles::Selected); }
            else { styles.remove(&Styles::Selected); }

            if (working && !selected) { styles.add(&Styles::Working); }
            else { styles.remove(&Styles::Working); }

            if (changed) { styles.add(&Styles::Changed); }
            else { styles.remove(&Styles::Changed); }

            if (indexBadge) {
                if (selected) { indexBadge->styles.add(&Styles::IndexBadgeSelected); }
                else { indexBadge->styles.remove(&Styles::IndexBadgeSelected); }
            }

            if (indexLabel) {
                if (selected) { indexLabel->styles.add(&Styles::IndexBadgeLabelSelected); }
                else { indexLabel->styles.remove(&Styles::IndexBadgeLabelSelected); }
            }

            if (subtitle) {
                subtitle->styles.remove(&Styles::SubtitleWorking);
                subtitle->styles.remove(&Styles::SubtitleChanged);

                if (changed) {
                    subtitle->styles.add(&Styles::SubtitleChanged);
                } else if (working) {
                    subtitle->styles.add(&Styles::SubtitleWorking);
                }
            }

            if (settingsButton) {
                if (selected) { settingsButton->styles.add(&Styles::IconButtonSelected); }
                else { settingsButton->styles.remove(&Styles::IconButtonSelected); }
            }

            if (deleteButton) {
                if (selected) { deleteButton->styles.add(&Styles::IconButtonSelected); }
                else { deleteButton->styles.remove(&Styles::IconButtonSelected); }
            }

            Box::computeChildren(e);
        }
    };
}
