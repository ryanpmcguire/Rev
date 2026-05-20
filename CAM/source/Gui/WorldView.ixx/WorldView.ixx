module;

#include <cstddef>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

#include <dbg.hpp>

export module Cam.Gui.WorldView;

import Rev.Element;
import Rev.Element.Style;
import Rev.Element.Event;

import Rev.Element.Box;

import Rev.Core.Pos3;
import Rev.Core.Color;
import Rev.Core.Vertex3;

import Rev.Primitive.Lines3d;

import Rev.Element.View3d;
import Rev.Element.View3d.Actor3d;

import Cam.App;
import Cam.App.Project;
import Cam.App.Model;
import Cam.App.MaterialState;

import Cam.Gui.World.MaterialState;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace View3d = Rev::Element::View3d;

    struct WorldView : public Box {

        Cam::App::AppState* app = nullptr;

        View3d::View* view3d = nullptr;

        Cam::App::Project* representedProject = nullptr;
        Cam::App::MaterialState* representedDisplayedState = nullptr;
        Cam::App::MaterialState* representedWorkingState = nullptr;
        size_t representedStateCount = 0;

        std::vector<Cam::Gui::World::MaterialState*> materialViews;

        View3d::Actor* lineActor = nullptr;
        std::vector<Rev::Core::Vertex3> testLines;

        bool partInView = false;

        std::function<void(Event&)> onStateChanged;

        // Create
        //--------------------------------------------------

        WorldView(
            Element* parent,
            StyleList styles = {}
        ) : Box(parent, styles, "WorldView") {

            app = Cam::App::AppState::Get(shared->state);

            this->style->size = { .width = Grow(), .height = Grow() };
            this->style->background.color = rgba(0, 0, 0, 0.0);

            view3d = new View3d::View(this);

            createTestLines();

            syncRepresentedProject();

            if (view3d) {
                view3d->camera.setDefaultView();
                view3d->fitToActors();
            }

            partInView = true;
        }

        // Destroy
        //--------------------------------------------------

        ~WorldView() {

            clearMaterialViews();

            if (view3d && lineActor) {
                view3d->removeActor(lineActor);
            }

            delete lineActor;
            lineActor = nullptr;
        }

        // Axis lines
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

                Rev::Core::Pos3 origin = { 0.0f, 0.0f, 0.0f };

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

            lineActor->lines = new Rev::Primitives::Lines3d(shared->canvas, {
                .lines = &testLines
            });

            lineActor->lines->color = {
                1.0f,
                1.0f,
                1.0f,
                1.0f
            };

            if (view3d) {
                view3d->addActor(lineActor);
            }
        }

        // App/project access
        //--------------------------------------------------

        Cam::App::Project* activeProject() {

            if (!app) { return nullptr; }

            return app->activeProject;
        }

        Cam::App::MaterialState* displayedState() {

            Cam::App::Project* project = activeProject();

            if (!project) { return nullptr; }

            return project->displayedState;
        }

        Cam::App::MaterialState* workingState() {

            Cam::App::Project* project = activeProject();

            if (!project) { return nullptr; }

            return project->workingState;
        }

        Cam::App::Model* selectionModel() {

            Cam::App::MaterialState* state = workingState();

            if (!state) { return nullptr; }

            return &state->model;
        }

        bool displayedModelIsEditable() {

            Cam::App::Project* project = activeProject();

            if (!project) { return false; }

            return (
                project->displayedState &&
                project->workingState &&
                project->displayedState == project->workingState
            );
        }

        // Material-state world views
        //--------------------------------------------------

        void clearMaterialViews() {

            for (Cam::Gui::World::MaterialState* view : materialViews) {
                delete view;
            }

            materialViews.clear();

            representedProject = nullptr;
            representedDisplayedState = nullptr;
            representedWorkingState = nullptr;
            representedStateCount = 0;
        }

        Cam::Gui::World::MaterialState* viewForState(
            Cam::App::MaterialState* state
        ) {
            for (Cam::Gui::World::MaterialState* view : materialViews) {

                if (view && view->state == state) {
                    return view;
                }
            }

            return nullptr;
        }

        Cam::Gui::World::MaterialState* displayedMaterialView() {
            return viewForState(displayedState());
        }

        Cam::Gui::World::MaterialState* workingMaterialView() {
            return viewForState(workingState());
        }

        Cam::Gui::World::MaterialState* createMaterialView(
            Cam::App::MaterialState* state
        ) {
            Cam::Gui::World::MaterialState* worldState =
                new Cam::Gui::World::MaterialState(shared->canvas);

            worldState->setState(state);
            worldState->attach(view3d);

            materialViews.push_back(worldState);

            return worldState;
        }

        bool projectOwnsState(
            Cam::App::Project* project,
            Cam::App::MaterialState* state
        ) {
            if (!project || !state) { return false; }

            for (Cam::App::MaterialState* candidate : project->states) {
                if (candidate == state) { return true; }
            }

            return false;
        }

        void syncRepresentedProject() {

            Cam::App::Project* project = activeProject();

            if (project == representedProject) {
                return;
            }

            clearMaterialViews();

            representedProject = project;

            if (!representedProject) {
                return;
            }

            for (Cam::App::MaterialState* state : representedProject->states) {
                createMaterialView(state);
            }

            representedDisplayedState = representedProject->displayedState;
            representedWorkingState = representedProject->workingState;
            representedStateCount = representedProject->states.size();

            applyDefaultVisibilityPolicy();
            syncAllMaterialViews();
        }

        void syncMaterialViewList() {

            Cam::App::Project* project = activeProject();

            if (!project) {
                clearMaterialViews();
                return;
            }

            if (project != representedProject) {
                syncRepresentedProject();
                return;
            }

            // Add new material states.
            for (Cam::App::MaterialState* state : project->states) {
                if (!viewForState(state)) {
                    createMaterialView(state);
                }
            }

            // Remove deleted material states.
            for (size_t i = 0; i < materialViews.size();) {

                Cam::Gui::World::MaterialState* view = materialViews[i];

                if (
                    view &&
                    projectOwnsState(project, view->state)
                ) {
                    i += 1;
                    continue;
                }

                delete view;
                materialViews.erase(materialViews.begin() + i);
            }

            representedStateCount = project->states.size();
        }

        void applyDefaultVisibilityPolicy() {

            Cam::App::MaterialState* displayed = displayedState();
            Cam::App::MaterialState* working = workingState();

            for (Cam::Gui::World::MaterialState* view : materialViews) {

                if (!view) { continue; }

                view->hideAll();

                if (view->state == displayed) {
                    view->showDisplayed();
                }

                if (
                    view->state == working &&
                    displayed == working
                ) {
                    view->enablePicking();
                }
            }
        }

        void syncAllMaterialViews() {

            for (Cam::Gui::World::MaterialState* view : materialViews) {
                if (view) { view->sync(); }
            }
        }

        void syncRepresentation() {

            Cam::App::Project* project = activeProject();

            if (project != representedProject) {
                syncRepresentedProject();
                return;
            }

            if (!project) {
                clearMaterialViews();
                return;
            }

            bool stateListChanged = (
                project->states.size() != representedStateCount
            );

            bool displayedChanged = (
                project->displayedState != representedDisplayedState
            );

            bool workingChanged = (
                project->workingState != representedWorkingState
            );

            if (stateListChanged) {
                syncMaterialViewList();
            }

            if (
                stateListChanged ||
                displayedChanged ||
                workingChanged
            ) {
                representedDisplayedState = project->displayedState;
                representedWorkingState = project->workingState;
                representedStateCount = project->states.size();

                applyDefaultVisibilityPolicy();
            }

            syncAllMaterialViews();
        }

        // External sync hook
        //--------------------------------------------------

        void sync(Event& e) {

            syncRepresentation();

            if (view3d) {
                view3d->refresh(e);
            }
        }

        void notifyStateChanged(Event& e) {

            if (onStateChanged) {
                onStateChanged(e);
            }
        }

        // Selection
        //--------------------------------------------------

        void selectFaceAtMouse(Event& e) {

            if (!app || !view3d) { return; }

            if (!displayedModelIsEditable()) {
                dbg("Selected material state is read-only. Select the working state to edit.");
                return;
            }

            Cam::Gui::World::MaterialState* worldState = workingMaterialView();

            if (!worldState || !worldState->pickActor) { return; }

            Cam::App::Model* editable = selectionModel();

            if (!editable) { return; }

            View3d::Hit hit;

            if (!view3d->hitTest(e.mouse.pos, hit)) { return; }
            if (hit.actor != worldState->pickActor) { return; }

            size_t tri = hit.triangleId;

            if (tri >= editable->render.triangleFaceIds.size()) { return; }

            size_t faceId = editable->render.triangleFaceIds[tri];

            editable->toggleFace(faceId);

            worldState->applyFaceColors();

            view3d->refresh(e);
        }

        // App operations
        //--------------------------------------------------

        bool defeatureSelected(Event& e) {

            if (!app || !app->defeatureSelected()) {
                dbg("defeature failed or no selected faces");
                return false;
            }

            sync(e);

            dbg("defeatured selected faces");

            notifyStateChanged(e);

            return true;
        }

        bool commitWorkingState(Event& e) {

            if (!app || !app->commitWorkingState()) {
                dbg("commit ignored");
                return false;
            }

            sync(e);

            dbg("committed working material state");

            notifyStateChanged(e);

            return true;
        }

        // Computing
        //--------------------------------------------------

        void computeChildren(Event& e) override {

            syncRepresentation();

            Box::computeChildren(e);
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
    };
}