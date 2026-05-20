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
import Cam.Gui.MaterialStates;
import Cam.Gui.WorldView;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    struct Interface : public Box {

        Cam::App::AppState* app = nullptr;

        TabView* tabView = nullptr;
        Box* body = nullptr;

        MaterialStates* materialStates = nullptr;
        WorldView* worldView = nullptr;

        // Create
        //--------------------------------------------------

        Interface(Element* parent) : Box(parent) {

            app = Cam::App::AppState::Get(shared->state);

            this->style->layout = { Axis::Vertical, Align::Start, Align::Start };
            this->style->background.color = rgba(0, 0, 0, 0.0);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 10_px, 10_px, 10_px, 10_px };

            tabView = new TabView(this);

            body = new Box(this, {}, "InterfaceBody");

            body->style->layout = { Axis::Horizontal, Align::Center, Align::Center };
            body->style->size = { .width = 100_pct, .height = Grow() };

            materialStates = new MaterialStates(body);
            worldView = new WorldView(body);

            materialStates->onSelectState = [this](Event& e) {
                if (worldView) { worldView->sync(e); }
            };

            materialStates->onDeleteState = [this](Event& e) {
                if (worldView) { worldView->sync(e); }
            };

            worldView->onStateChanged = [this](Event& e) {

                if (materialStates) {
                    materialStates->refresh(e);
                }

                refresh(e);
            };
        }

        // Events
        //--------------------------------------------------

        void keyDown(Event& e) override {

            if (e.keyboard.key == "delete" || e.keyboard.del) {

                if (worldView) {
                    worldView->defeatureSelected(e);
                }

                e.propagate = false;
                return;
            }

            if (e.keyboard.key == "enter" || e.keyboard.enter) {

                if (worldView) {
                    worldView->commitWorkingState(e);
                }

                e.propagate = false;
                return;
            }

            Box::keyDown(e);
        }
    };
}