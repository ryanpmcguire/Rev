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

import Rev.Core.Pos3;
import Rev.Core.Color;
import Rev.Core.Vertex3;

import Rev.Primitive.Mesh3d;
import Rev.Primitive.Lines3d;

import Rev.Element.View3d;
import Rev.Element.View3d.Actor3d;

import Cam.App;
import Cam.App.Model;
import Cam.App.MaterialState;

import Cam.Gui.MaterialStates;
import Cam.Gui.ToolPath;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    struct Interface : public Box {

        Cam::App::AppState* app = nullptr;

        MaterialStates* materialStates = nullptr;
        View3d::View* view3d = nullptr;

        View3d::Actor* partActor = nullptr;
        View3d::Actor* deltaActor = nullptr;
        View3d::Actor* pickActor = nullptr;
        View3d::Actor* lineActor = nullptr;

        ToolPath toolPath;

        std::vector<Rev::Core::Vertex3> testLines;

        bool partInView = false;

        // Create
        Interface(Element* parent) : Box(parent) {

            app = Cam::App::AppState::Get(shared->state);

            this->style->layout = { Axis::Horizontal, Align::Center, Align::Center };
            this->style->background.color = rgba(0, 0, 0, 0.0);
            this->style->size = { .width = 100_pct, .height = 100_pct };
            this->style->padding = { 10_px, 10_px, 10_px, 10_px };

            materialStates = new MaterialStates(this);
            view3d = new View3d::View(this);

            materialStates->onSelectState = [this](Event& e) {
                syncActorToDisplayedModel(e);
            };

            materialStates->onDeleteState = [this](Event& e) {
                syncActorToDisplayedModel(e);
            };

            try {

                createPartActor();
                createDeltaActor();
                createPickActor();

                toolPath.create(shared->canvas);

                view3d->addActor(partActor);
                view3d->addActor(toolPath.actor);
                view3d->addActor(deltaActor);
                view3d->addActor(pickActor);
                
                applyFaceColors();

                syncDeltaActorToDisplayedState();
                syncPickActorToWorkingState();
                syncToolPathToDisplayedState();

                view3d->camera.setDefaultView();
                view3d->fitToActors();

                partInView = true;

                createTestLines();

                dbg("CAD model loaded");
            }

            catch (const std::exception& e) {
                dbg("Failed to load CAD model");
                dbg(e.what());
            }
        }

        // Destroy
        //--------------------------------------------------

        ~Interface() {

            if (view3d && partActor) { view3d->removeActor(partActor); }
            if (view3d && deltaActor) { view3d->removeActor(deltaActor); }
            if (view3d && pickActor) { view3d->removeActor(pickActor); }
            if (view3d && toolPath.actor) { view3d->removeActor(toolPath.actor); }
            if (view3d && lineActor) { view3d->removeActor(lineActor); }

            delete partActor;
            delete deltaActor;
            delete pickActor;
            delete lineActor;

            toolPath.destroy();

            partActor = nullptr;
            deltaActor = nullptr;
            pickActor = nullptr;
            lineActor = nullptr;
        }

        // Actor creation
        //--------------------------------------------------

        void createPartActor() {

            Cam::App::Model* displayed = displayedModel();

            if (!displayed) {
                throw std::runtime_error("No displayed model.");
            }

            partActor = new View3d::Actor();

            partActor->visible = true;
            partActor->selectable = false;
            partActor->ownsMesh = true;
            partActor->ownsTriangles = false;
            partActor->includeInFit = true;

            partActor->mesh = new Primitives::Mesh3d(shared->canvas, {
                .triangles = &displayed->render.triangles
            });

            partActor->mesh->color = {
                0.75f,
                0.75f,
                0.82f,
                1.0f
            };
        }

        void createDeltaActor() {

            deltaActor = new View3d::Actor();

            deltaActor->visible = false;
            deltaActor->selectable = false;
            deltaActor->ownsMesh = true;
            deltaActor->ownsTriangles = false;
            deltaActor->includeInFit = false;

            deltaActor->mesh = new Primitives::Mesh3d(shared->canvas, {});

            deltaActor->mesh->color = {
                1.0f,
                0.0f,
                0.0f,
                0.30f
            };
        }

        void createPickActor() {

            Cam::App::Model* editable = selectionModel();

            if (!editable) {
                throw std::runtime_error("No editable working model.");
            }

            pickActor = new View3d::Actor();

            pickActor->visible = false;
            pickActor->selectable = true;
            pickActor->ownsMesh = true;
            pickActor->ownsTriangles = false;
            pickActor->includeInFit = false;

            pickActor->mesh = new Primitives::Mesh3d(shared->canvas, {
                .triangles = &editable->render.triangles
            });

            pickActor->mesh->color = {
                0.0f,
                0.0f,
                0.0f,
                0.0f
            };
        }

        // Test lines
        //--------------------------------------------------

        void createTestLines() {

            testLines.clear();

            auto addLinePoint = [this](
                const Rev::Core::Pos3& p,
                const Rev::Core::Color& color
            ) {
                testLines.push_back({
                    p.x,
                    p.y,
                    p.z,
                    color
                });
            };

            auto addAxis = [this, &addLinePoint](
                const Rev::Core::Pos3& dir,
                Rev::Core::Color color
            ) {

                float core = 2.0f;
                float far = 90.0f;

                Rev::Core::Color full = color;
                Rev::Core::Color soft = color;
                Rev::Core::Color fade = color;

                full.a = 1.0f;
                soft.a = 0.8f;
                fade.a = 0.0f;

                Rev::Core::Pos3 origin = {
                    0.0f,
                    0.0f,
                    0.0f
                };

                Rev::Core::Pos3 nFar = dir * -far;
                Rev::Core::Pos3 nCore = dir * -core;
                Rev::Core::Pos3 pCore = dir * core;
                Rev::Core::Pos3 pFar = dir * far;

                addLinePoint(nFar, fade);
                addLinePoint(nCore, soft);

                addLinePoint(nCore, soft);
                addLinePoint(origin, full);

                addLinePoint(origin, full);
                addLinePoint(pCore, soft);

                addLinePoint(pCore, soft);
                addLinePoint(pFar, fade);
            };

            addAxis({ 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
            addAxis({ 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f });
            addAxis({ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.25f, 1.0f, 1.0f });

            lineActor = new View3d::Actor();

            lineActor->visible = true;
            lineActor->selectable = false;
            lineActor->ownsLines = true;
            lineActor->includeInFit = false;

            lineActor->lines = new Primitives::Lines3d(shared->canvas, {
                .lines = &testLines
            });

            lineActor->lines->color = {
                1.0f,
                1.0f,
                1.0f,
                1.0f
            };

            view3d->addActor(lineActor);
        }

        // Model display management
        //--------------------------------------------------

        Cam::App::Model* displayedModel() {

            if (!app) { return nullptr; }

            Cam::App::MaterialState* state = app->displayedState;

            if (!state) { return app->getDisplayedModel(); }

            if (state->parent && state->hasDelta) {
                return &state->parent->model;
            }

            return &state->model;
        }

        Cam::App::Model* selectionModel() {

            if (!app || !app->workingState) { return nullptr; }

            return &app->workingState->model;
        }

        Cam::App::MaterialState* displayedState() {

            if (!app) { return nullptr; }

            return app->displayedState;
        }

        bool displayedModelIsEditable() {

            if (!app) { return false; }

            return (
                app->displayedState &&
                app->workingState &&
                app->displayedState == app->workingState
            );
        }

        void syncActorToDisplayedModel(Event& e) {

            Cam::App::Model* displayed = displayedModel();

            if (!displayed || !partActor || !partActor->mesh) { return; }

            partActor->mesh->pTriangles = &displayed->render.triangles;

            applyFaceColors();

            partActor->mesh->compute();

            syncDeltaActorToDisplayedState();
            syncPickActorToWorkingState();
            syncToolPathToDisplayedState();

            if (view3d) { view3d->refresh(e); }

            refresh(e);
        }

        void syncDeltaActorToDisplayedState() {

            if (!deltaActor || !deltaActor->mesh) { return; }

            Cam::App::MaterialState* state = displayedState();

            if (!state || !state->hasDelta) {
                deltaActor->visible = false;
                return;
            }

            deltaActor->mesh->pTriangles = &state->delta.render.triangles;
            deltaActor->visible = true;
            deltaActor->mesh->compute();
        }

        void syncPickActorToWorkingState() {

            Cam::App::Model* editable = selectionModel();

            if (!editable || !pickActor || !pickActor->mesh) { return; }

            pickActor->mesh->pTriangles = &editable->render.triangles;
            pickActor->mesh->compute();
        }

        void syncToolPathToDisplayedState() {

            toolPath.sync(
                displayedState()
            );
        }

        // Selection display
        //--------------------------------------------------

        void applyFaceColors() {

            Cam::App::Model* displayed = displayedModel();

            if (!displayed || !partActor || !partActor->mesh) { return; }

            std::vector<Rev::Core::Vertex3>* pTriangles =
                partActor->mesh->getTriangles();

            if (!pTriangles) { return; }

            std::vector<Rev::Core::Vertex3>& triangles = *pTriangles;
            std::vector<size_t>& triangleFaceIds = displayed->render.triangleFaceIds;

            Rev::Core::Color base = { 0.0f, 0.0f, 0.0f, 0.0f };
            Rev::Core::Color selected = { 1.0f, 0.0f, 0.0f, 1.0f };

            size_t triangleCount = triangles.size() / 3;

            for (size_t tri = 0; tri < triangleCount; tri++) {

                if (tri >= triangleFaceIds.size()) { continue; }

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

            if (!app || !view3d || !pickActor) { return; }

            if (!displayedModelIsEditable()) {
                dbg("Selected material state is read-only. Select the working state to edit.");
                return;
            }

            Cam::App::Model* editable = selectionModel();

            if (!editable) { return; }

            View3d::Hit hit;

            if (!view3d->hitTest(e.mouse.pos, hit)) { return; }
            if (hit.actor != pickActor) { return; }

            size_t tri = hit.triangleId;

            if (tri >= editable->render.triangleFaceIds.size()) { return; }

            size_t faceId = editable->render.triangleFaceIds[tri];

            editable->toggleFace(faceId);

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

                    if (materialStates) { materialStates->refresh(e); }

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

                    if (materialStates) { materialStates->refresh(e); }

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