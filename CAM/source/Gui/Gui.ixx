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
        View3d* view3d = nullptr;

        Actor3D* partActor = nullptr;
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

            view3d = new View3d(this);

            materialStates->onSelectState = [this](Event& e) {
                syncActorToDisplayedModel(e);
            };

            // 3d View
            //--------------------------------------------------

            try {

                partActor = new Actor3D();

                partActor->visible = true;
                partActor->ownsMesh = true;
                partActor->ownsTriangles = false;

                partActor->mesh = new Primitives::Mesh(shared->canvas, {
                    .triangles = &app->model.render.triangles
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

        void syncActorToDisplayedModel(Event& e) {

            if (!app || !app->displayedModel || !partActor || !partActor->mesh) {
                return;
            }

            partActor->mesh->pTriangles = &app->displayedModel->render.triangles;

            applyFaceColors();

            partActor->mesh->compute();

            view3d->refresh(e);
            refresh(e);
        }

        // Selection display
        //--------------------------------------------------

        void applyFaceColors() {

            if (!app || !partActor || !partActor->mesh) {
                return;
            }

            std::vector<Rev::Core::Vertex3>* pTriangles =
                partActor->mesh->getTriangles();

            if (!pTriangles) {
                return;
            }

            std::vector<Rev::Core::Vertex3>& triangles = *pTriangles;
            std::vector<size_t>& triangleFaceIds = app->model.render.triangleFaceIds;

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
                    app->model.isFaceSelected(triangleFaceIds[tri])
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

            View3d::Hit hit;

            if (!view3d->hitTest(e.mouse.pos, hit)) {
                return;
            }

            if (hit.actor != partActor) {
                return;
            }

            size_t tri = hit.triangleId;

            if (tri >= app->model.render.triangleFaceIds.size()) {
                return;
            }

            size_t faceId = app->model.render.triangleFaceIds[tri];

            app->model.toggleFace(faceId);

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

            if (e.keyboard.del) {

                if (app && app->defeatureSelected()) {

                    applyFaceColors();

                    if (partActor && partActor->mesh) {
                        partActor->mesh->compute();
                    }

                    view3d->refresh(e);
                    refresh(e);

                    dbg("defeatured selected faces");
                }

                else {
                    dbg("defeature failed or no selected faces");
                }

                e.propagate = false;
                return;
            }

            if (e.keyboard.input == "c" || e.keyboard.input == "C") {

                if (app) {
                    app->commitMaterialState();

                    applyFaceColors();

                    if (partActor && partActor->mesh) {
                        partActor->mesh->compute();
                    }

                    view3d->refresh(e);
                    refresh(e);

                    dbg("committed material state");
                    //dbg(app->materialStateCount());
                }

                e.propagate = false;
                return;
            }

            Box::keyDown(e);
        }
    };
}