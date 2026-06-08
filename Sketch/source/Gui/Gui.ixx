module;

#include <string>

export module Sketch.Gui;

import Rev.Element;
import Rev.Element.Style;
import Rev.Element.Event;

import Rev.Element.Box;

import Sketch.App;
import Sketch.Gui.TabView;
import Sketch.Gui.Toolbar;
import Sketch.Gui.SketchView;
import Sketch.Gui.Theme;

export namespace Sketch::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    struct Interface : public Box {

        Sketch::App::AppState* app = nullptr;

        TabView* tabView = nullptr;
        Toolbar* toolbar = nullptr;
        Box* body = nullptr;
        SketchView* sketchView = nullptr;

        // Create
        //--------------------------------------------------

        Interface(Element* parent) : Box(parent) {

            app = Sketch::App::AppState::Get(shared->state);

            Theme::applyMode(Theme::currentMode());

            this->style->layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
            this->styles.add(&Theme::Styles::Background);
            this->style->size = { .width = 100_pct, .height = Grow() };

            // Children
            //--------------------------------------------------

            tabView = new TabView(this);

            // CAD-style tool strip: basic sketch primitives.
            toolbar = new Toolbar(this);

            // The main content section: the 2D sketch canvas.
            body = new Box(this, {}, "InterfaceBody");
            body->style->layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False };
            body->style->size = { .width = 100_pct, .height = Grow() };

            sketchView = new SketchView(body);

            // Events
            //--------------------------------------------------

            tabView->onSelectProject = [this](Event& e) { refresh(e); };
            tabView->onCloseProject = [this](Event& e) { refresh(e); };
            tabView->onNewProject = [this](Event& e) { refresh(e); };

            toolbar->onSelectTool = [this](Event& e) { refresh(e); };
        }

        // Events
        //--------------------------------------------------

        void keyDown(Event& e) override {

            // Save As
            if (e.keyboard.ctrl && e.keyboard.shift && e.keyboard.key == "s") {

                if (app) {
                    app->saveProjectAs();
                }

                refresh(e);
                e.propagate = false;
                return;
            }

            // Save
            if (e.keyboard.ctrl && e.keyboard.key == "s") {

                if (app) {
                    app->saveProject();
                }

                refresh(e);
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

            // Forward remaining keys (Enter / Escape) to the sketch view so the
            // active tool's state machine can react.
            if (sketchView) {
                sketchView->keyDown(e);
                if (!e.propagate) { return; }
            }

            Box::keyDown(e);
        }
    };
}
