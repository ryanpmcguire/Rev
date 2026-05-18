module;

#include <cstddef>
#include <cmath>
#include <string>
#include <stdexcept>

#include <vector>

#include <dbg.hpp>

export module Cam.Gui;

import Rev.Element;
import Rev.Element.Style;
import Rev.Element.Event;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Slider;
import Rev.Element.Dropdown;
import Rev.Element.Radio;
import Rev.Element.Checkbox;
import Rev.Element.TextInput;
import Rev.Element.Chart;

import Rev.Serial;
import Rev.Window;

import Rev.Core.Color;
import Rev.Core.Vertex3;

import Rev.Primitive.Mesh;
import Rev.Element.View3d;
import Rev.Element.View3d.Actor3d;

import Cam.App;
import Cam.App.Model;

import Cam.Gui.MaterialStates;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    struct Interface : public Box {

        Cam::App::AppState* app = nullptr;

        MaterialStates* materialStates = nullptr;
        View3d::View* view3d = nullptr;

        View3d::Actor* partActor = nullptr;
        bool partInView = false;

        // Create
        Interface(Element* parent) : Box(parent) {

            app = Cam::App::AppState::Get(shared->state);

            // Self
            this->style->layout = { Axis::Horizontal, Align::Center, Align::Center };
            this->style->background.color = rgba(0, 0, 0, 0.0);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 10_px, 10_px, 10_px, 10_px };

            // Material States
            //--------------------------------------------------

            materialStates = new MaterialStates(this);

            view3d = new View3d::View(this);

            materialStates->onSelectState = [this](Event& e) {
                syncActorToDisplayedModel(e);
            };

            // 3d View
            //--------------------------------------------------

            try {

                Cam::App::Model* displayed = app->getDisplayedModel();

                if (!displayed) {
                    throw std::runtime_error("No displayed model.");
                }

                partActor = new View3d::Actor();

                partActor->visible = true;
                partActor->ownsMesh = true;
                partActor->ownsTriangles = false;

                partActor->mesh = new Primitives::Mesh(shared->canvas, {
                    .triangles = &displayed->render.triangles
                });

                partActor->mesh->color = {
                    0.75f,
                    0.75f,
                    0.82f,
                    1.0f
                };

                applyFaceColors();

                view3d->addActor(partActor);
                partInView = true;

                dbg("CAD model loaded");
            }

            catch (const std::exception& e) {

                dbg("Failed to load CAD model");
                dbg(e.what());
            }
        }

        // Destroy
        ~Interface() {

            if (view3d && partActor) {
                view3d->removeActor(partActor);
            }

            delete partActor;
            partActor = nullptr;
        }

        // Model display management
        //--------------------------------------------------

        Cam::App::Model* displayedModel() {

            if (!app) {
                return nullptr;
            }

            return app->getDisplayedModel();
        }

        bool displayedModelIsEditable() {

            if (!app) {
                return false;
            }

            return (
                app->displayedState &&
                app->workingState &&
                app->displayedState == app->workingState
            );
        }

        void syncActorToDisplayedModel(Event& e) {

            Cam::App::Model* displayed = displayedModel();

            if (!displayed || !partActor || !partActor->mesh) {
                return;
            }

            partActor->mesh->pTriangles = &displayed->render.triangles;

            applyFaceColors();

            partActor->mesh->compute();

            if (view3d) {
                view3d->refresh(e);
            }

            refresh(e);
        }

        // Selection display
        //--------------------------------------------------

        void applyFaceColors() {

            Cam::App::Model* displayed = displayedModel();

            if (!displayed || !partActor || !partActor->mesh) {
                return;
            }

            std::vector<Rev::Core::Vertex3>* pTriangles =
                partActor->mesh->getTriangles();

            if (!pTriangles) {
                return;
            }

            std::vector<Rev::Core::Vertex3>& triangles = *pTriangles;
            std::vector<size_t>& triangleFaceIds = displayed->render.triangleFaceIds;

            Rev::Core::Color base = {
                0.0f,
                0.0f,
                0.0f,
                0.0f
            };

            Rev::Core::Color selected = {
                1.0f,
                0.0f,
                0.0f,
                1.0f
            };

            size_t triangleCount = triangles.size() / 3;

            for (size_t tri = 0; tri < triangleCount; tri++) {

                if (tri >= triangleFaceIds.size()) {
                    continue;
                }

                Rev::Core::Color color = (
                    displayed->isFaceSelected(triangleFaceIds[tri])
                    ? selected
                    : base
                );

                triangles[tri * 3 + 0].color = color;
                triangles[tri * 3 + 1].color = color;
                triangles[tri * 3 + 2].color = color;
            }

            partActor->mesh->compute();
        }

        void selectFaceAtMouse(Event& e) {

            if (!app || !view3d || !partActor) {
                return;
            }

            if (!displayedModelIsEditable()) {
                dbg("Selected material state is read-only. Select the working state to edit.");
                return;
            }

            Cam::App::Model* displayed = displayedModel();

            if (!displayed) {
                return;
            }

            View3d::Hit hit;

            if (!view3d->hitTest(e.mouse.pos, hit)) {
                return;
            }

            if (hit.actor != partActor) {
                return;
            }

            size_t tri = hit.triangleId;

            if (tri >= displayed->render.triangleFaceIds.size()) {
                return;
            }

            size_t faceId = displayed->render.triangleFaceIds[tri];

            displayed->toggleFace(faceId);

            applyFaceColors();

            view3d->refresh(e);
            refresh(e);
        }

        // Events
        //--------------------------------------------------

        void mouseDown(Event& e) override {

            if (e.keyboard.ctrl) {

                selectFaceAtMouse(e);

                e.propagate = false;
                return;
            }

            Box::mouseDown(e);
        }

        void keyDown(Event& e) override {

            if (e.keyboard.key == "delete" || e.keyboard.del) {

                if (app && app->defeatureSelected()) {

                    syncActorToDisplayedModel(e);

                    if (materialStates) {
                        materialStates->refresh(e);
                    }

                    dbg("defeatured selected faces");
                }

                else {
                    dbg("defeature failed or no selected faces");
                }

                e.propagate = false;
                return;
            }

            if (e.keyboard.key == "enter" || e.keyboard.enter) {

                if (app && app->commitWorkingState()) {

                    if (materialStates) {
                        materialStates->refresh(e);
                    }

                    syncActorToDisplayedModel(e);

                    dbg("committed working material state");
                }

                else {
                    dbg("commit ignored");
                }

                e.propagate = false;
                return;
            }

            Box::keyDown(e);
        }
    };
}