module;

#include <string>

#include <dbg.hpp>

export module Cam.Gui;

import Rev.Element;
import Rev.Element.Style;
import Rev.Element.Event;

import Rev.Element.Box;

import Cam.App;

import Cam.Gui.TabView;
import Cam.Gui.LeftPanel;
import Cam.Gui.RightPanel;
import Cam.Gui.WorldView;

import Rev.Element.ControlTheme;
import Cam.Gui.MaterialState;
import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    struct Interface : public Box {

        Cam::App::AppState* app = nullptr;

        TabView* tabView = nullptr;
        Box* body = nullptr;

        LeftPanel* leftPanel = nullptr;
        WorldView* worldView = nullptr;
        RightPanel* rightPanel = nullptr;

        // Create
        //--------------------------------------------------

        Interface(Element* parent) : Box(parent) {

            app = Cam::App::AppState::Get(shared->state);

            Theme::applyMode(Theme::currentMode());

            this->style->layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
            this->styles.add(&Theme::Styles::Background);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            //this->style->padding = { 10_px, 10_px, 10_px, 10_px };

            tabView = new TabView(this);

            body = new Box(this, {}, "InterfaceBody");

            body->style->layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False };
            body->style->size = { .width = 100_pct, .height = Grow() };

            leftPanel = new LeftPanel(body);
            worldView = new WorldView(body);
            rightPanel = new RightPanel(body);

            tabView->onSelectProject = [this](Event& e) {

                if (app) {
                    app->loadTools();
                }

                if (leftPanel) {
                    leftPanel->refresh(e);
                }

                if (rightPanel) {
                    rightPanel->refresh(e);
                }

                if (worldView) {
                    worldView->sync(e);
                }

                refresh(e);
            };

            tabView->onCloseProject = [this](Event& e) {

                if (leftPanel) {
                    leftPanel->refresh(e);
                }

                if (worldView) {
                    worldView->sync(e);
                }

                refresh(e);
            };

            tabView->onNewProject = [this](Event& e) {

                if (leftPanel) {
                    leftPanel->refresh(e);
                }

                if (worldView) {
                    worldView->sync(e);
                }

                refresh(e);
            };

            leftPanel->onBeforeSelectFile = [this](Event& e) {

                if (worldView) {
                    worldView->requestClearMaterialViews(e);
                }
            };

            leftPanel->onSelectFile = [this](Event& e) {

                dbg("[Gui] selected/replaced project file");

                if (tabView) {
                    tabView->refresh(e);
                }

                if (leftPanel) {
                    leftPanel->refresh(e);
                }

                if (worldView) {
                    worldView->sync(e);
                }

                refresh(e);
            };

            leftPanel->onSelectState = [this](Event& e) {
                if (worldView) { worldView->sync(e); }
            };

            leftPanel->onDeleteState = [this](Event& e) {
                if (worldView) { worldView->sync(e); }
            };

            leftPanel->onToolPathEdited = [this](Event& e) {
                if (worldView) { worldView->sync(e); }
                refresh(e);
            };

            worldView->onStateChanged = [this](Event& e) {

                if (leftPanel) {
                    leftPanel->refresh(e);
                }

                refresh(e);
            };

            rightPanel->onSelectTool = [this](Event& e) {
                refresh(e);
            };

            if (rightPanel && rightPanel->tools) {
                rightPanel->tools->onToolEdited = [this](Event& e) {
                    if (rightPanel) {
                        rightPanel->refresh(e);
                    }
                    refresh(e);
                };
            }

            rightPanel->onSelectFolder = [this](Event& e) {
                if (rightPanel) {
                    rightPanel->refresh(e);
                }

                if (rightPanel && rightPanel->tools) {
                    rightPanel->tools->refresh(e);
                }

                refresh(e);
            };

        }

        // Events
        //--------------------------------------------------

        void keyDown(Event& e) override {

            // Save
            if (e.keyboard.ctrl && e.keyboard.key == "s") {

                if (app) {
                    app->saveProject();
                }

                e.propagate = false;
                return;
            }

            // Save as
            if (e.keyboard.ctrl && e.keyboard.shift && e.keyboard.key == "s") {

                if (app) {
                    app->saveProjectAs();
                }

                e.propagate = false;
                return;
            }

            // Toggle light/dark theme.
            if (e.keyboard.key == "d") {

                Theme::toggleMode();
                refresh(e);

                e.propagate = false;
                return;
            }

            // Style stress test: shared material-state name label + permuted row DOM.
            if (e.keyboard.key == "l") {

                static bool labelsRed = false;

                labelsRed = !labelsRed;

                Theme::Styles::Text.text.color = labelsRed
                    ? rgba(220, 38, 38, 1.0)
                    : Theme::palette.text;

                Theme::Styles::Text.dirty = true;

                if (leftPanel && leftPanel->materialStates) {
                    leftPanel->materialStates->stressTestPermuteRows(e);
                }

                refresh(e);

                e.propagate = false;
                return;
            }

            if (worldView) {
                worldView->keyDown(e);
                if (!e.propagate) { return; }
            }

            Box::keyDown(e);
        }
    };
}