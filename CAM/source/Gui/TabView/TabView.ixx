module;

#include <string>
#include <vector>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.TabView;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;

import Cam.App;
import Cam.App.Project;
import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace TabViewStyle {

        Style Self = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False, CrossAlign::True },
            .size = { .width = 100_pct },
            //.margin = { 4_px, 4_px, 0_px, 4_px },
            .padding = { 8_px, 8_px, 0_px, 0_px }
        };

        Style OpenButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .height = 24_px },
            .margin = { .right = 8_px },
            .padding = { 10_px, 10_px, 2_px, 2_px },
            .border = { .radius = 6_px },
            .cursor = Cursor::Hand
        };

        Style OpenButtonLabel = {
            .text = { .size = 13_px }
        };

        Style TabsHost = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            //.size = { .height = 100_pct },
            .margin = { .right = 8_px }
        };

        Style Tab = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .height = 34_px },
            .margin = { .right = 5_px },
            .padding = { 15_px, 8_px, 4_px, 2_px },
            .border = {
                .width = 1_px,
                .tl = { .radius = 7_px },
                .tr = { .radius = 7_px },
                .bottom = { .width = 0_px }
            },
            .cursor = Cursor::Hand
        };

        Style TabActiveShape = {
            .border = {
                .width = 1_px,
                .tl = { .radius = 7_px },
                .tr = { .radius = 7_px },
                .bottom = { .width = 1_px }
            }
        };

        Style LabelBox = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { .right = 8_px }
        };

        Style Label = {
            .text = { .size = 13_px }
        };

        Style CloseButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { 18_px, 18_px },
            .background = { .color = rgba(0, 0, 0, 0.00), .transition = 100_ms },
            .border = { .radius = 9_px },
            .cursor = Cursor::Hand
        };

        Style CloseIcon = {
            .size = { 12_px, 12_px }
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

        Style NewIcon = {
            .size = { 12_px, 12_px }
        };
    }

    struct ProjectTab : public Box {

        Cam::App::AppState* app = nullptr;
        Cam::App::Project* project = nullptr;

        std::function<void(Event&)> onSelectProject;

        ProjectTab(
            Element* parent,
            Cam::App::AppState* app,
            Cam::App::Project* project
        ) : Box(
            parent,
            Theme::withTab({
                &TabViewStyle::Tab,
                &Theme::Styles::TabHover
            }),
            "ProjectTab"
        ) {
            this->app = app;
            this->project = project;
        }

        void mouseDown(Event& e) override {

            // Children get the first chance to consume the event.
            Box::mouseDown(e);

            if (!e.propagate) {
                return;
            }

            if (!app || !project) {
                return;
            }

            if (app->setActiveProject(project)) {

                if (onSelectProject) {
                    onSelectProject(e);
                }

                refresh(e);
            }

            e.propagate = false;
        }
    };

    struct TabView : public Box {

        Cam::App::AppState* app = nullptr;

        Box* openButton = nullptr;
        Text* openLabel = nullptr;

        Box* tabsHost = nullptr;

        std::vector<ProjectTab*> tabs;
        std::vector<Cam::App::Project*> tabProjects;
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
            this->styles.add(&Theme::Styles::TabBar);

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

        bool projectIsActive(Cam::App::Project* project) const {

            return (
                app &&
                project &&
                app->activeProject == project
            );
        }

        std::string projectName(Cam::App::Project* project, size_t fallbackIndex) const {

            if (!project) {
                return "Invalid Project";
            }

            std::string label;

            if (!project->name.empty()) {
                label = project->name;
            }

            else {
                label = "Project " + std::to_string(fallbackIndex + 1);
            }

            if (project->dirty) {
                label += " *";
            }

            return label;
        }

        bool tabListMatchesProjects() const {

            if (!app) {
                return tabs.empty();
            }

            if (tabProjects.size() != app->projects.size()) {
                return false;
            }

            for (size_t i = 0; i < app->projects.size(); i++) {
                if (tabProjects[i] != app->projects[i]) {
                    return false;
                }
            }

            return true;
        }

        // Buttons
        //--------------------------------------------------

        void createOpenButton() {

            openButton = new Box(
                this,
                Theme::layer({
                    &TabViewStyle::OpenButton,
                    &Theme::Styles::AccentButtonHover
                }, {
                    &Theme::Styles::AccentButton
                }),
                "OpenProjectButton"
            );

            openLabel = new Text(
                openButton,
                "Open",
                Theme::layer(
                    { &TabViewStyle::OpenButtonLabel },
                    { &Theme::Styles::AccentButtonLabel }
                )
            );

            openButton->onClick([this](Event& e) {

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
                Theme::layer({
                    &TabViewStyle::NewButton,
                    &Theme::Styles::ChromeHover
                }, {}),
                "NewProjectButton"
            );

            newIcon = new Svg(
                newButton,
                File("./New.svg"),
                Theme::layer({
                    &TabViewStyle::NewIcon,
                    &Theme::Styles::ChromeIconHover
                }, {
                    &Theme::Styles::ChromeIcon
                }),
                "NewProjectIcon"
            );

            newButton->onClick([this](Event& e) {

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

        void clearTabs() {

            for (ProjectTab* tab : tabs) {
                delete tab;
            }

            tabs.clear();
            tabProjects.clear();
            labelBoxes.clear();
            labels.clear();
            closeButtons.clear();
            closeIcons.clear();
        }

        void createTab(
            Cam::App::Project* project,
            size_t i
        ) {
            if (!tabsHost) { return; }
            if (!project) { return; }

            ProjectTab* tab = new ProjectTab(
                tabsHost,
                app,
                project
            );

            tab->onSelectProject = [this](Event& e) {

                if (onSelectProject) {
                    onSelectProject(e);
                }
            };

            Box* labelBox = new Box(
                tab,
                { &TabViewStyle::LabelBox },
                "ProjectTabLabelBox"
            );

            Text* label = new Text(
                labelBox,
                projectName(project, i),
                Theme::layer(
                    { &TabViewStyle::Label },
                    { &Theme::Styles::TabLabel }
                )
            );

            Box* closeButton = new Box(
                tab,
                Theme::layer({
                    &TabViewStyle::CloseButton,
                    &Theme::Styles::ChromeHover
                }, {}),
                "CloseProjectButton"
            );

            Svg* closeIcon = new Svg(
                closeButton,
                File("./Close.svg"),
                Theme::layer({
                    &TabViewStyle::CloseIcon,
                    &Theme::Styles::ChromeIconHover
                }, {
                    &Theme::Styles::ChromeIcon
                }),
                "CloseProjectIcon"
            );

            closeButton->onClick([this, project](Event& e) {

                if (!app || !project) { return; }

                if (app->closeProject(project)) {

                    if (onCloseProject) {
                        onCloseProject(e);
                    }

                    refresh(e);
                }

                e.propagate = false;
            });

            tabs.push_back(tab);
            tabProjects.push_back(project);
            labelBoxes.push_back(labelBox);
            labels.push_back(label);
            closeButtons.push_back(closeButton);
            closeIcons.push_back(closeIcon);
        }

        void rebuildTabs() {

            clearTabs();

            if (!app) { return; }

            for (size_t i = 0; i < app->projects.size(); i++) {
                createTab(app->projects[i], i);
            }
        }

        void syncTabs() {

            if (!tabListMatchesProjects()) {
                rebuildTabs();
            }

            for (size_t i = 0; i < tabProjects.size(); i++) {

                Cam::App::Project* project = tabProjects[i];

                if (labels[i]) {
                    labels[i]->content = projectName(project, i);
                }
            }
        }

        // Compute
        //--------------------------------------------------

        void computeChildren(Event& e) override {

            syncTabs();

            for (size_t i = 0; i < tabs.size(); i++) {

                bool isActive = projectIsActive(tabProjects[i]);

                if (isActive) {
                    tabs[i]->styles.add(&TabViewStyle::TabActiveShape);
                    tabs[i]->styles.add(&Theme::Styles::TabActive);
                }
                else {
                    tabs[i]->styles.remove(&TabViewStyle::TabActiveShape);
                    tabs[i]->styles.remove(&Theme::Styles::TabActive);
                }

                if (isActive) { labels[i]->styles.add(&Theme::Styles::TabLabelActive); }
                else { labels[i]->styles.remove(&Theme::Styles::TabLabelActive); }
            }

            Box::computeChildren(e);
        }
    };
}