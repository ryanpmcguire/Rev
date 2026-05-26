module;

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <vector>

export module Cam.Gui.World.MaterialState;

import Rev.Graphics.Canvas;

import Rev.Core.Color;
import Rev.Core.Pos3;
import Rev.Core.Vertex3;

import Rev.Primitive.Mesh3d;
import Rev.Primitive.Lines3d;

import Rev.Element.View3d;
import Rev.Element.View3d.Actor3d;

import Cam.App.MaterialState;
import Cam.App.Model;
import Cam.App.ToolPath;

import Cam.Gui.ToolPath;

export namespace Cam::Gui::World {

    namespace View3d = Rev::Element::View3d;

    struct MaterialState {

        Rev::Graphics::Canvas* canvas = nullptr;
        View3d::View* view = nullptr;

        Cam::App::MaterialState* state = nullptr;

        View3d::Actor* partActor = nullptr;
        View3d::Actor* deltaActor = nullptr;
        View3d::Actor* pickActor = nullptr;
        View3d::Actor* axisPickMarkerActor = nullptr;

        std::vector<Rev::Core::Vertex3> axisPickMarkers;

        Cam::Gui::ToolPath toolPath;

        bool attached = false;

        // View settings
        //--------------------------------------------------

        bool showPart = false;
        bool showDelta = false;
        bool showPick = false;
        bool showToolPath = false;

        bool selectable = false;
        bool includeInFit = false;

        // Create / destroy
        //--------------------------------------------------

        MaterialState() {}

        MaterialState(
            Rev::Graphics::Canvas* canvas
        ) {
            create(canvas);
        }

        ~MaterialState() {
            destroy();
        }

        void create(
            Rev::Graphics::Canvas* canvas
        ) {
            this->canvas = canvas;

            createPartActor();
            createDeltaActor();
            createPickActor();
            createAxisPickMarkerActor();

            toolPath.create(canvas);
        }

        void destroy() {

            detach();

            delete partActor;
            delete deltaActor;
            delete pickActor;
            delete axisPickMarkerActor;

            toolPath.destroy();

            partActor = nullptr;
            deltaActor = nullptr;
            pickActor = nullptr;
            axisPickMarkerActor = nullptr;

            canvas = nullptr;
            state = nullptr;
        }

        void attach(
            View3d::View* view
        ) {
            if (!view) { return; }
            if (attached) { return; }

            this->view = view;

            // Draw order:
            // base model, toolpath, transparent delta, invisible pick actor.
            view->addActor(partActor);
            view->addActor(toolPath.actor);
            view->addActor(deltaActor);
            view->addActor(axisPickMarkerActor);
            view->addActor(pickActor);

            attached = true;
        }

        void detach() {

            if (!view || !attached) { return; }

            if (partActor) { view->removeActor(partActor); }
            if (deltaActor) { view->removeActor(deltaActor); }
            if (axisPickMarkerActor) { view->removeActor(axisPickMarkerActor); }
            if (pickActor) { view->removeActor(pickActor); }
            if (toolPath.actor) { view->removeActor(toolPath.actor); }

            view = nullptr;
            attached = false;
        }

        // Actor creation
        //--------------------------------------------------

        void createPartActor() {

            partActor = new View3d::Actor();

            partActor->visible = false;
            partActor->selectable = false;
            partActor->ownsMesh = true;
            partActor->ownsTriangles = false;
            partActor->includeInFit = false;

            partActor->mesh = new Rev::Primitives::Mesh3d(canvas, {});

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

            deltaActor->mesh = new Rev::Primitives::Mesh3d(canvas, {});

            deltaActor->mesh->color = {
                1.0f,
                0.0f,
                0.0f,
                0.30f
            };
        }

        void createPickActor() {

            pickActor = new View3d::Actor();

            pickActor->visible = false;
            pickActor->selectable = false;
            pickActor->ownsMesh = true;
            pickActor->ownsTriangles = false;
            pickActor->includeInFit = false;

            pickActor->mesh = new Rev::Primitives::Mesh3d(canvas, {});

            pickActor->mesh->color = {
                0.0f,
                0.0f,
                0.0f,
                0.0f
            };
        }

        void createAxisPickMarkerActor() {

            axisPickMarkerActor = new View3d::Actor();

            axisPickMarkerActor->visible = false;
            axisPickMarkerActor->selectable = false;
            axisPickMarkerActor->ownsLines = true;
            axisPickMarkerActor->includeInFit = false;

            axisPickMarkerActor->lines = new Rev::Primitives::Lines3d(canvas, {
                .lines = &axisPickMarkers
            });

            axisPickMarkerActor->lines->color = {
                1.0f,
                0.55f,
                0.12f,
                1.0f
            };
        }

        static void appendMarkerCross(
            std::vector<Rev::Core::Vertex3>& lines,
            const Rev::Core::Pos3& center,
            float size,
            const Rev::Core::Color& color
        ) {
            const Rev::Core::Pos3 axes[3] = {
                { size, 0.0f, 0.0f },
                { 0.0f, size, 0.0f },
                { 0.0f, 0.0f, size }
            };

            for (const Rev::Core::Pos3& axis : axes) {

                Rev::Core::Pos3 a = center - axis;
                Rev::Core::Pos3 b = center + axis;

                lines.push_back({
                    a.x, a.y, a.z, color
                });

                lines.push_back({
                    b.x, b.y, b.z, color
                });
            }
        }

        static float axisPickMarkerSize(
            const std::vector<Rev::Core::Pos3>& points
        ) {
            if (points.empty()) {
                return 1.0f;
            }

            Rev::Core::Pos3 min = points[0];
            Rev::Core::Pos3 max = points[0];

            for (const Rev::Core::Pos3& point : points) {
                min.x = std::min(min.x, point.x);
                min.y = std::min(min.y, point.y);
                min.z = std::min(min.z, point.z);
                max.x = std::max(max.x, point.x);
                max.y = std::max(max.y, point.y);
                max.z = std::max(max.z, point.z);
            }

            const float span = (max - min).pythag();

            return std::clamp(span * 0.06f, 0.5f, 8.0f);
        }

        // Model access
        //--------------------------------------------------

        Cam::App::Model* displayModel() {

            if (!state) { return nullptr; }

            // Inspect parent stock for every step; final state has no parent.
            if (state->parent) {
                return &state->parent->model;
            }

            return &state->model;
        }

        Cam::App::Model* selectionModel() {

            if (!state) { return nullptr; }

            return &state->model;
        }

        // Part mesh: parent stock when inspecting, own model when editing faces.
        Cam::App::Model* partModel() {

            if (showPick || selectable) {
                return selectionModel();
            }

            return displayModel();
        }

        // Settings
        //--------------------------------------------------

        void hideAll() {

            showPart = false;
            showDelta = false;
            showPick = false;
            showToolPath = false;

            selectable = false;
            includeInFit = false;
        }

        void showDisplayed() {

            showPart = true;
            showDelta = true;
            showToolPath = true;

            showPick = false;
            selectable = false;

            includeInFit = true;
        }

        void showOverlays() {

            showPart = false;
            showDelta = true;
            showToolPath = true;

            showPick = false;
            selectable = false;
            includeInFit = false;
        }

        void enablePicking() {

            showPick = true;
            selectable = true;
            showPart = true;
        }

        // Sync
        //--------------------------------------------------

        void setState(
            Cam::App::MaterialState* state
        ) {
            this->state = state;
        }

        void sync(double toolPathPreviewProgress = 1.0) {

            syncPart();
            syncDelta();
            syncPick();
            syncToolPath(toolPathPreviewProgress);
            syncAxisPickMarkers();
        }

        void syncPart() {

            if (!partActor || !partActor->mesh) { return; }

            Cam::App::Model* model = partModel();

            if (!state || !model || !showPart) {
                partActor->visible = false;
                partActor->selectable = false;
                partActor->includeInFit = false;
                return;
            }

            partActor->mesh->pTriangles = &model->render.triangles;
            partActor->mesh->dirty = true;
            partActor->visible = true;
            partActor->selectable = false;
            partActor->includeInFit = includeInFit;

            applyFaceColors();
        }

        void syncDelta() {

            if (!deltaActor || !deltaActor->mesh) { return; }

            if (!state || !state->hasDelta || !showDelta) {
                deltaActor->visible = false;
                deltaActor->selectable = false;
                deltaActor->includeInFit = false;
                return;
            }

            deltaActor->mesh->pTriangles = &state->delta.render.triangles;
            deltaActor->mesh->dirty = true;
            deltaActor->visible = true;
            deltaActor->selectable = false;
            deltaActor->includeInFit = false;
        }

        void syncPick() {

            if (!pickActor || !pickActor->mesh) { return; }

            Cam::App::Model* model = selectionModel();

            if (!state || !model || !showPick) {
                pickActor->visible = false;
                pickActor->selectable = false;
                pickActor->includeInFit = false;
                return;
            }

            pickActor->mesh->pTriangles = &model->render.triangles;
            pickActor->mesh->dirty = true;
            pickActor->visible = false;
            pickActor->selectable = selectable;
            pickActor->includeInFit = false;
        }

        void syncToolPath(double toolPathPreviewProgress = 1.0) {

            if (!toolPath.actor) { return; }

            if (!state || !showToolPath) {
                toolPath.actor->visible = false;
                return;
            }

            toolPath.sync(state, toolPathPreviewProgress);
        }

        void syncAxisPickMarkers() {

            if (!axisPickMarkerActor || !axisPickMarkerActor->lines) { return; }

            axisPickMarkers.clear();

            Cam::App::Model* model = selectionModel();

            if (
                !state ||
                !model ||
                !showPick ||
                !model->hasAxisPickFace()
            ) {
                axisPickMarkerActor->visible = false;
                axisPickMarkerActor->lines->dirty = true;
                return;
            }

            std::vector<Rev::Core::Pos3> points =
                model->faceAxisPickMarkers(model->axisPickFaceId);

            if (points.empty()) {
                axisPickMarkerActor->visible = false;
                axisPickMarkerActor->lines->dirty = true;
                return;
            }

            const float markerSize = axisPickMarkerSize(points);

            Rev::Core::Color markerColor = {
                1.0f,
                0.55f,
                0.12f,
                1.0f
            };

            for (const Rev::Core::Pos3& point : points) {
                appendMarkerCross(axisPickMarkers, point, markerSize, markerColor);
            }

            axisPickMarkerActor->visible = true;
            axisPickMarkerActor->lines->dirty = true;
        }

        // Selection display
        //--------------------------------------------------

        void applyFaceColors() {

            Cam::App::Model* model = partModel();

            if (!model || !partActor || !partActor->mesh) { return; }

            std::vector<Rev::Core::Vertex3>* pTriangles =
                partActor->mesh->getTriangles();

            if (!pTriangles) { return; }

            std::vector<Rev::Core::Vertex3>& triangles = *pTriangles;
            std::vector<size_t>& triangleFaceIds = model->render.triangleFaceIds;

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

            Rev::Core::Color axisPick = {
                0.55f,
                0.95f,
                0.45f,
                1.0f
            };

            Rev::Core::Color slicePlane = {
                0.55f,
                0.82f,
                1.0f,
                1.0f
            };

            size_t sliceFaceId = Cam::App::ToolPath::NoSliceFaceId;

            if (state && state->toolPath.hasSliceFace()) {
                sliceFaceId = state->toolPath.sliceFaceId;
            }

            size_t triangleCount = triangles.size() / 3;

            for (size_t tri = 0; tri < triangleCount; tri++) {

                if (tri >= triangleFaceIds.size()) { continue; }

                size_t faceId = triangleFaceIds[tri];

                Rev::Core::Color color = base;

                if (faceId == sliceFaceId) {
                    color = slicePlane;
                }
                else if (model->isAxisPickFace(faceId)) {
                    color = axisPick;
                }
                else if (model->isFaceSelected(faceId)) {
                    color = selected;
                }

                triangles[tri * 3 + 0].color = color;
                triangles[tri * 3 + 1].color = color;
                triangles[tri * 3 + 2].color = color;
            }

            partActor->mesh->dirty = true;
        }
    };
}