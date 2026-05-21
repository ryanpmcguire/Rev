module;

#include <string>
#include <vector>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.TabView;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

import Cam.App;
import Cam.App.Project;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace TabViewStyle {

        Style Self = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { 4_px, 4_px, 0_px, 4_px },
            .padding = { 8_px, 8_px, 0_px, 0_px },

            .border = {
                .bottom = {
                    .color = rgba(0, 0, 0, 0.25),
                    .width = 1_px
                }
            },

            .background = { .color = rgba(0, 0, 0, 0.025) },

            .shadow = {
                .color = rgba(0, 0, 0, 0.10),
                .size = Px(-4),
                .blur = 8_px,
                .y = 1_px
            },
        };

        Style OpenButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .height = 24_px },
            .margin = { .right = 8_px },
            .padding = { 10_px, 10_px, 2_px, 2_px },
            .background = { .color = rgba(76, 120, 220, 0.85), .transition = 100_ms },
            .border = { .radius = 6_px },
            .cursor = Cursor::Hand
        };

        Style OpenButtonHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(86, 135, 245, 0.95) }
        };

        Style OpenButtonLabel = {
            .text = { .color = rgba(255, 255, 255, 0.96), .size = 13_px }
        };

        Style TabsHost = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .height = 100_pct },
            .margin = { .right = 8_px }
        };

        Style Tab = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .height = 34_px },
            .margin = { .right = 5_px },
            .padding = { 15_px, 8_px, 4_px, 2_px },
            .background = { .color = rgba(255, 255, 255, 0.22), .transition = 100_ms },
            .border = {
                .color = rgba(0, 0, 0, 0.16),
                .width = 1_px,
                .tl = { .radius = 7_px },
                .tr = { .radius = 7_px },
                .bottom = { .width = 0_px }
            },
            .cursor = Cursor::Hand
        };

        Style TabHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(255, 255, 255, 0.40) },
            .border = {
                .color = rgba(0, 0, 0, 0.23)
            }
        };

        Style TabActive = {
            .background = { .color = rgba(255, 255, 255, 0.86) },
            .border = {
                .color = rgba(0, 0, 0, 0.30),
                .width = 1_px,
                .tl = { .radius = 7_px },
                .tr = { .radius = 7_px },
                .bottom = {
                    .color = rgba(255, 255, 255, 0.86),
                    .width = 1_px
                }
            },
        };

        Style LabelBox = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { .right = 8_px }
        };

        Style Label = {
            .text = { .color = rgba(0, 0, 0, 0.68), .size = 13_px }
        };

        Style LabelActive = {
            .text = { .color = rgba(0, 0, 0, 0.94), .size = 13_px }
        };

        Style CloseButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { 18_px, 18_px },
            .background = { .color = rgba(0, 0, 0, 0.00), .transition = 100_ms },
            .border = { .radius = 9_px },
            .cursor = Cursor::Hand
        };

        Style CloseButtonHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(0, 0, 0, 0.12) }
        };

        Style CloseIcon = {
            .size = { 12_px, 12_px },
            .text = { .color = rgba(0, 0, 0, 0.55), .transition = 100_ms }
        };

        Style CloseIconHover = {
            .applies = { .hover = true, .focus = true },
            .text = { .color = rgba(0, 0, 0, 0.90) }
        };

        Style NewButtonHost = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .height = 100_pct }
        };

        Style NewButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { 18_px, 18_px },
            .background = { .color = rgba(0, 0, 0, 0.00), .transition = 100_ms },
            .border = { .radius = 9_px },
            .cursor = Cursor::Hand
        };

        Style NewButtonHover = {
            .applies = { .hover = true, .focus = true },
            .background = { .color = rgba(0, 0, 0, 0.12) }
        };

        Style NewIcon = {
            .size = { 12_px, 12_px },
            .text = { .color = rgba(0, 0, 0, 0.55), .transition = 100_ms }
        };

        Style NewIconHover = {
            .applies = { .hover = true, .focus = true },
            .text = { .color = rgba(0, 0, 0, 0.90) }
        };
    }

    struct TabView : public Box {

        Cam::App::AppState* app = nullptr;

        Box* openButton = nullptr;
        Text* openLabel = nullptr;

        Box* tabsHost = nullptr;

        std::vector<Box*> tabs;
        std::vector<Box*> labelBoxes;
        std::vector<Text*> labels;
        std::vector<Box*> closeButtons;
        std::vector<Svg*> closeIcons;

        Box* newButtonHost = nullptr;
        Box* newButton = nullptr;
        Svg* newIcon = nullptr;

        std::function<void(Event&)> onOpenProject;
        std::function<void(Event&)> onSelectProject;
        std::function<void(Event&)> onCloseProject;
        std::function<void(Event&)> onNewProject;

        TabView(Element* parent, StyleList styles = {}) : Box(parent, styles, "TabView") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&TabViewStyle::Self);

            createOpenButton();

            tabsHost = new Box(
                this,
                { &TabViewStyle::TabsHost },
                "ProjectTabsHost"
            );

            createNewButton();
        }

        // Project helpers
        //--------------------------------------------------

        size_t projectCount() const {

            if (!app) { return 0; }

            return app->projects.size();
        }

        Cam::App::Project* projectAt(size_t i) const {

            if (!app) { return nullptr; }
            if (i >= app->projects.size()) { return nullptr; }

            return app->projects[i];
        }

        bool projectIsActive(size_t i) const {

            Cam::App::Project* project = projectAt(i);

            return (
                app &&
                project &&
                app->activeProject == project
            );
        }

        std::string projectName(size_t i) const {

            Cam::App::Project* project = projectAt(i);

            if (!project) {
                return "Invalid Project";
            }

            if (!project->name.empty()) {
                return project->name;
            }

            return "Project " + std::to_string(i + 1);
        }

        // Buttons
        //--------------------------------------------------

        void createOpenButton() {

            openButton = new Box(
                this,
                { &TabViewStyle::OpenButton, &TabViewStyle::OpenButtonHover },
                "OpenProjectButton"
            );

            openLabel = new Text(
                openButton,
                "Open",
                { &TabViewStyle::OpenButtonLabel }
            );

            openButton->onMouseDown([this](Event& e) {

                if (!app) { return; }

                if (app->openProject()) {

                    if (onOpenProject) {
                        onOpenProject(e);
                    }

                    refresh(e);
                }

                e.propagate = false;
            });
        }

        void createNewButton() {

            newButtonHost = new Box(
                this,
                { &TabViewStyle::NewButtonHost },
                "NewProjectButtonHost"
            );

            newButton = new Box(
                newButtonHost,
                { &TabViewStyle::NewButton, &TabViewStyle::NewButtonHover },
                "NewProjectButton"
            );

            newIcon = new Svg(
                newButton,
                File("./New.svg"),
                { &TabViewStyle::NewIcon, &TabViewStyle::NewIconHover },
                "NewProjectIcon"
            );

            newButton->onMouseDown([this](Event& e) {

                if (!app) { return; }

                app->newProject();

                if (onNewProject) {
                    onNewProject(e);
                }

                refresh(e);

                e.propagate = false;
            });
        }

        // Tabs
        //--------------------------------------------------

        void createTab(size_t i) {

            if (!tabsHost) { return; }

            Box* tab = new Box(
                tabsHost,
                { &TabViewStyle::Tab, &TabViewStyle::TabHover },
                "ProjectTab"
            );

            Box* labelBox = new Box(
                tab,
                { &TabViewStyle::LabelBox },
                "ProjectTabLabelBox"
            );

            Text* label = new Text(
                labelBox,
                projectName(i),
                { &TabViewStyle::Label }
            );

            Box* closeButton = new Box(
                tab,
                { &TabViewStyle::CloseButton, &TabViewStyle::CloseButtonHover },
                "CloseProjectButton"
            );

            Svg* closeIcon = new Svg(
                closeButton,
                File("./Close.svg"),
                { &TabViewStyle::CloseIcon, &TabViewStyle::CloseIconHover },
                "CloseProjectIcon"
            );

            tab->onMouseDown([this, i](Event& e) {

                if (!app) { return; }

                if (app->setActiveProject(i)) {

                    if (onSelectProject) {
                        onSelectProject(e);
                    }

                    refresh(e);
                }

                e.propagate = false;
            });

            closeButton->onMouseDown([this, i](Event& e) {

                if (!app) { return; }

                if (app->closeProject(i)) {

                    if (onCloseProject) {
                        onCloseProject(e);
                    }

                    refresh(e);
                }

                e.propagate = false;
            });

            tabs.push_back(tab);
            labelBoxes.push_back(labelBox);
            labels.push_back(label);
            closeButtons.push_back(closeButton);
            closeIcons.push_back(closeIcon);
        }

        void syncTabs() {

            size_t newSize = projectCount();

            while (tabs.size() > newSize) {

                Box* tab = tabs.back();

                tabs.pop_back();
                labelBoxes.pop_back();
                labels.pop_back();
                closeButtons.pop_back();
                closeIcons.pop_back();

                delete tab;
            }

            while (tabs.size() < newSize) {
                createTab(tabs.size());
            }

            for (size_t i = 0; i < newSize; i++) {
                if (labels[i]) {
                    labels[i]->content = projectName(i);
                }
            }
        }

        // Compute
        //--------------------------------------------------

        void computeChildren(Event& e) override {

            syncTabs();

            for (size_t i = 0; i < tabs.size(); i++) {

                bool isActive = projectIsActive(i);

                if (isActive) { tabs[i]->styles.add(&TabViewStyle::TabActive); }
                else { tabs[i]->styles.remove(&TabViewStyle::TabActive); }

                if (isActive) { labels[i]->styles.add(&TabViewStyle::LabelActive); }
                else { labels[i]->styles.remove(&TabViewStyle::LabelActive); }
            }

            Box::computeChildren(e);
        }
    };
}