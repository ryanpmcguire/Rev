module;

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <map>

#include <dbg.hpp>

export module Cam.Gui.WorldView;

import Rev.Element;
import Rev.Element.Style;
import Rev.Element.Event;
import Rev.Element.Event.GestureTracker;

import Rev.Element.Box;
import Rev.Element.Text;

import Rev.Core.Pos3;
import Rev.Core.Color;
import Rev.Core.Vertex3;
import Rev.Core.Animator;

import Rev.Primitive.Lines3d;
import Rev.Primitive.Mesh3d;

import Rev.Element.View3d;
import Rev.Element.View3d.Actor3d;

import Cam.App;
import Cam.App.Project;
import Cam.App.Model;
import Cam.App.MaterialState;

import Cam.Gui.World.MaterialState;
import Cam.Gui.ToolPath;
import Cam.Gui.Theme;
import Cam.Gui.PreviewBar;
import Cam.Gui.ToolPathPreview;

import Cam.Machine.Pose;
import Cam.Machine.Definition;
import Cam.Machine.ToolPath;
import Cam.Machine.IKSolver;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace View3d = Rev::Element::View3d;

    enum class WorldViewCommand {
        Defeature,
        OffsetFaces,
        AddTab,
        CenterOrigin,
        DefineAxisX,
        DefineAxisY,
        DefineAxisZ
    };

    struct WorldView : public Box {

        Cam::App::AppState* app = nullptr;

        View3d::View* view3d = nullptr;
        PreviewBar* previewBar = nullptr;
        ToolPathPreviewTimeline previewTimeline;
        bool previewTimelineDirty = true;

        Cam::App::Project* representedProject = nullptr;
        Cam::App::MaterialState* representedDisplayedState = nullptr;
        Cam::App::MaterialState* representedWorkingState = nullptr;
        size_t representedStateCount = 0;

        std::vector<Cam::Gui::World::MaterialState*> materialViews;

        View3d::Actor* lineActor = nullptr;
        std::vector<Rev::Core::Vertex3> testLines;

        View3d::Actor* toolPreviewActor = nullptr;
        std::vector<Rev::Core::Vertex3> toolPreviewTriangles;

        bool partInView = false;
        bool representationDirty = true;
        bool clearMaterialViewsRequested = false;

        // Machine simulation
        PreviewMode previewMode = PreviewMode::AbsoluteToolPath;

        // Per-state IK cache.  Keyed by MaterialState* so each state's
        // solved path lives independently — different setups have different
        // tool directions and require independent IK solves.
        std::map<Cam::App::MaterialState*, Cam::Machine::MachineToolPath> machineToolPaths;
        bool machineToolPathsDirty = true;

        std::function<void(Event&)> onStateChanged;

        GestureTracker<WorldViewCommand> gestures = {
            { "df", WorldViewCommand::Defeature },
            { "ef", WorldViewCommand::OffsetFaces },
            { "co", WorldViewCommand::CenterOrigin },
            { "ax", WorldViewCommand::DefineAxisX },
            { "ay", WorldViewCommand::DefineAxisY },
            { "az", WorldViewCommand::DefineAxisZ },
        };

        // Create
        //--------------------------------------------------

        WorldView(
            Element* parent,
            StyleList styles = {}
        ) : Box(parent, styles, "WorldView") {

            app = Cam::App::AppState::Get(shared->state);

            this->style->size = { .width = Grow(), .height = Grow() };
            this->style->layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
            this->style->background.color = rgba(0, 0, 0, 0.0);

            view3d = new View3d::View(this);

            createPreviewBar();

            createAxisLineActor();
            createToolPreviewActor();
            syncAxisLines();

            syncRepresentedProject();

            if (view3d) {
                view3d->camera.setDefaultView();
                view3d->fitToActors();
            }

            partInView = true;

            gestures.onGesture = [this](WorldViewCommand command, Event& e) {

                switch (command) {

                    case WorldViewCommand::Defeature: {
                        defeatureSelected(e);
                        break;
                    }

                    case WorldViewCommand::OffsetFaces: {
                        offsetSelected(e);
                        break;
                    }

                    case WorldViewCommand::AddTab: {
                        dbg("[WorldView] AddTab gesture (not implemented)");
                        break;
                    }

                    case WorldViewCommand::CenterOrigin: {
                        centerOriginFromSelection(e);
                        break;
                    }

                    case WorldViewCommand::DefineAxisX: {
                        defineAxisFromSelection(e, 'X');
                        break;
                    }

                    case WorldViewCommand::DefineAxisY: {
                        defineAxisFromSelection(e, 'Y');
                        break;
                    }

                    case WorldViewCommand::DefineAxisZ: {
                        defineAxisFromSelection(e, 'Z');
                        break;
                    }
                }

                refresh(e);
            };
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

            if (view3d && toolPreviewActor) {
                view3d->removeActor(toolPreviewActor);
            }

            delete toolPreviewActor;
            toolPreviewActor = nullptr;
            toolPreviewTriangles.clear();
        }

        // Axis lines
        //--------------------------------------------------

        void createPreviewBar() {

            previewBar = new PreviewBar(this);

            previewBar->onPercentChanged = [this](Event& e) {

                rebuildPreviewTimelineIfNeeded();

                if (previewBar) {
                    previewTimeline.setFromSliderPercent(previewBar->percent);
                }

                applyPreviewClock(e);
            };

            previewBar->onRefresh = [this](Event& e) {
                refresh(e);
            };

            previewBar->onPlayRequested = [this](Event& e) {
                rebuildPreviewTimelineIfNeeded();

                if (previewTimeline.atEnd()) {
                    previewTimeline.setElapsed(0.0);
                    syncPreviewSlider(e);
                }
            };

            previewBar->onAnimateFrame = [this](
                Rev::Core::AnimationEvent& frame,
                Event& e
            ) {
                rebuildPreviewTimelineIfNeeded();

                const double speed = previewBar
                    ? previewBar->playbackSpeedMultiplier()
                    : 1.0;

                previewTimeline.setElapsed(
                    previewTimeline.elapsedSeconds +
                    (double(frame.deltaMs) / 1000.0) * speed
                );

                if (previewTimeline.atEnd()) {
                    previewTimeline.setElapsed(previewTimeline.totalDurationSeconds);

                    if (previewBar) {
                        previewBar->stop(e);
                    }
                }

                applyPreviewClock(e);
            };

            previewBar->onStepForward = [this](Event& e) {
                stepPreviewForward(e);
            };

            previewBar->onStepBack = [this](Event& e) {
                stepPreviewBack(e);
            };

            previewBar->onViewModeChanged = [this](PreviewMode mode, Event& e) {
                previewMode           = mode;
                machineToolPathsDirty = true;
                if (view3d) { view3d->refresh(e); }
            };
        }

        void markPreviewTimelineDirty() {
            previewTimelineDirty  = true;
            machineToolPathsDirty = true;
        }

        void rebuildPreviewTimelineIfNeeded() {

            if (!previewTimelineDirty) { return; }

            const bool hadTimeline = !previewTimeline.segments.empty();
            const double savedElapsed = previewTimeline.elapsedSeconds;

            previewTimeline.rebuild(activeProject());

            if (hadTimeline) {
                previewTimeline.setElapsed(savedElapsed);
            }
            else if (previewBar) {
                previewTimeline.setFromSliderPercent(previewBar->percent);
            }

            previewTimelineDirty = false;
        }

        void syncPreviewSlider(Event& e) {

            if (!previewBar) { return; }

            previewBar->percent = previewTimeline.sliderPercent();
            previewBar->syncSliderDisplay(e);
        }

        void applyPreviewClock(Event& e) {

            syncPreviewSlider(e);

            if (previewBar) {
                previewBar->syncTimeDisplay(
                    previewTimeline.elapsedSeconds,
                    previewTimeline.totalDurationSeconds,
                    e
                );
            }

            syncAllMaterialViews();

            if (view3d) {
                view3d->refresh(e);
            }
        }

        void keepPreviewPlayingIfItWas(Event& e, bool wasPlaying) {

            if (!wasPlaying || !previewBar) { return; }

            if (!previewBar->isPlaying()) {
                previewBar->resumePlaying(e);
            }
        }

        bool selectMaterialStateForPreview(
            Cam::App::MaterialState* state,
            Event& e,
            bool wasPlaying = false
        ) {

            Cam::App::Project* project = activeProject();

            if (!state || !project || !app) { return false; }

            const bool addToSelection = project->viewSelection.size() > 1;

            if (!app->selectState(state, addToSelection)) { return false; }

            markPreviewTimelineDirty();
            rebuildPreviewTimelineIfNeeded();

            representationDirty = true;
            syncRepresentation();
            notifyStateChanged(e);
            keepPreviewPlayingIfItWas(e, wasPlaying);

            return true;
        }

        void stepPreviewForward(Event& e) {

            const bool wasPlaying = previewBar && previewBar->isPlaying();

            rebuildPreviewTimelineIfNeeded();

            Cam::App::Project* project = activeProject();

            if (!project || previewTimeline.segments.empty()) {
                applyPreviewClock(e);
                return;
            }

            const ToolPathPreviewTimeline::LocateResult here =
                previewTimeline.locate();

            if (!here.valid) {
                applyPreviewClock(e);
                return;
            }

            const PreviewSegment& current =
                previewTimeline.segments[here.segmentIndex];

            Cam::App::MaterialState* targetState = nullptr;
            double newElapsed = previewTimeline.elapsedSeconds;

            if (!here.atSegmentEnd) {
                newElapsed = current.startSeconds + current.durationSeconds;
                targetState = current.state;
            }
            else if (here.segmentIndex + 1 < previewTimeline.segments.size()) {

                const PreviewSegment& next =
                    previewTimeline.segments[here.segmentIndex + 1];

                newElapsed = next.startSeconds;
                targetState = next.state;
            }
            else {
                targetState = project->nextStateAfterViewSelection();

                if (targetState) {
                    selectMaterialStateForPreview(targetState, e, wasPlaying);

                    if (!previewTimeline.segments.empty()) {
                        const PreviewSegment& added =
                            previewTimeline.segments.back();

                        newElapsed = added.startSeconds;
                    }

                    previewTimeline.setElapsed(newElapsed);
                    applyPreviewClock(e);
                    keepPreviewPlayingIfItWas(e, wasPlaying);
                    return;
                }
            }

            if (targetState) {
                selectMaterialStateForPreview(targetState, e, wasPlaying);
            }

            previewTimeline.setElapsed(newElapsed);
            applyPreviewClock(e);
            keepPreviewPlayingIfItWas(e, wasPlaying);
        }

        void stepPreviewBack(Event& e) {

            const bool wasPlaying = previewBar && previewBar->isPlaying();

            rebuildPreviewTimelineIfNeeded();

            Cam::App::Project* project = activeProject();

            if (!project || previewTimeline.segments.empty()) {
                previewTimeline.setElapsed(0.0);
                applyPreviewClock(e);
                return;
            }

            const ToolPathPreviewTimeline::LocateResult here =
                previewTimeline.locate();

            if (!here.valid) {
                previewTimeline.setElapsed(0.0);
                applyPreviewClock(e);
                return;
            }

            const PreviewSegment& current =
                previewTimeline.segments[here.segmentIndex];

            Cam::App::MaterialState* targetState = current.state;
            double newElapsed = previewTimeline.elapsedSeconds;

            if (here.localProgress > 1e-9) {
                newElapsed = current.startSeconds;
                targetState = current.state;
            }
            else if (here.segmentIndex > 0) {

                const PreviewSegment& previous =
                    previewTimeline.segments[here.segmentIndex - 1];

                newElapsed = previous.startSeconds;
                targetState = previous.state;
            }
            else {
                targetState = project->previousStateBeforeViewSelection();

                if (targetState) {
                    selectMaterialStateForPreview(targetState, e, wasPlaying);

                    if (!previewTimeline.segments.empty()) {
                        newElapsed =
                            previewTimeline.segments.front().startSeconds;
                    }
                    else {
                        newElapsed = 0.0;
                    }

                    previewTimeline.setElapsed(newElapsed);
                    applyPreviewClock(e);
                    keepPreviewPlayingIfItWas(e, wasPlaying);
                    return;
                }

                newElapsed = 0.0;
                targetState = previewTimeline.segments.front().state;
            }

            if (targetState) {
                selectMaterialStateForPreview(targetState, e, wasPlaying);
            }

            previewTimeline.setElapsed(newElapsed);
            applyPreviewClock(e);
            keepPreviewPlayingIfItWas(e, wasPlaying);
        }

        void appendAxisLine(
            const Rev::Core::Pos3& origin,
            const Rev::Core::Pos3& direction,
            const Rev::Core::Color& color,
            float core,
            float far
        ) {
            Rev::Core::Color full = color;
            Rev::Core::Color soft = color;
            Rev::Core::Color fade = color;

            full.a = 1.0f;
            soft.a = 0.8f;
            fade.a = 0.0f;

            const Rev::Core::Pos3 nFar = origin + direction * -far;
            const Rev::Core::Pos3 nCore = origin + direction * -core;
            const Rev::Core::Pos3 pCore = origin + direction * core;
            const Rev::Core::Pos3 pFar = origin + direction * far;

            auto addPoint = [this](const Rev::Core::Pos3& p, const Rev::Core::Color& c) {
                testLines.push_back({ p.x, p.y, p.z, c });
            };

            addPoint(nFar, fade);
            addPoint(nCore, soft);

            addPoint(nCore, soft);
            addPoint(origin, full);

            addPoint(origin, full);
            addPoint(pCore, soft);

            addPoint(pCore, soft);
            addPoint(pFar, fade);
        }

        void syncAxisLines() {

            testLines.clear();

            if (!lineActor || !lineActor->lines) { return; }

            constexpr float core = 2.0f;
            constexpr float far = 90.0f;

            Rev::Core::Pos3 origin = { 0.0f, 0.0f, 0.0f };
            Rev::Core::Pos3 xDir = { 1.0f, 0.0f, 0.0f };
            Rev::Core::Pos3 yDir = { 0.0f, 1.0f, 0.0f };
            Rev::Core::Pos3 zDir = { 0.0f, 0.0f, 1.0f };

            Cam::App::Model* model = selectionModel();

            if (
                model &&
                (
                    model->hasAxisOrigin ||
                    model->hasAxisX ||
                    model->hasAxisY ||
                    model->hasAxisZ
                )
            ) {
                origin = model->axisOrigin;
            }

            if (model) {
                model->getOrthonormalAxisFrame(xDir, yDir, zDir);
            }

            appendAxisLine(origin, xDir, { 1.0f, 0.0f, 0.0f, 1.0f }, core, far);
            appendAxisLine(origin, yDir, { 0.0f, 1.0f, 0.0f, 1.0f }, core, far);
            appendAxisLine(origin, zDir, { 0.0f, 0.25f, 1.0f, 1.0f }, core, far);

            lineActor->lines->dirty = true;
        }

        void createAxisLineActor() {

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

        void createToolPreviewActor() {

            toolPreviewActor = new View3d::Actor();

            toolPreviewActor->visible = false;
            toolPreviewActor->selectable = false;
            toolPreviewActor->ownsMesh = true;
            toolPreviewActor->ownsTriangles = true;
            toolPreviewActor->includeInFit = false;

            toolPreviewActor->mesh = new Rev::Primitives::Mesh3d(shared->canvas, {
                .triangles = &toolPreviewTriangles
            });

            toolPreviewActor->mesh->color = {
                0.92f,
                0.78f,
                0.35f,
                0.85f
            };

            if (view3d) {
                view3d->addActor(toolPreviewActor);
            }
        }

        struct ToolPreviewTarget {
            Cam::App::MaterialState* state = nullptr;
            double progress = 0.0;
        };

        Cam::App::MaterialState* materialStateWithToolPathForPreview(
            Cam::App::Project* project
        ) {

            if (!project) { return nullptr; }

            const std::vector<Cam::App::MaterialState*> sequence =
                ToolPathPreviewTimeline::previewSequence(project);

            if (!sequence.empty()) {
                return sequence.front();
            }

            Cam::App::MaterialState* primary = project->primaryViewState();

            if (primary && primary->hasToolPath) {
                return primary;
            }

            for (Cam::App::MaterialState* state : project->viewSelection) {
                if (state && state->hasToolPath) {
                    return state;
                }
            }

            for (Cam::App::MaterialState* state : project->states) {
                if (state && state->hasToolPath) {
                    return state;
                }
            }

            return nullptr;
        }

        ToolPreviewTarget activeToolPreviewTarget(
            Cam::App::Project* project
        ) {

            ToolPreviewTarget target = {};

            if (!project) { return target; }

            const ToolPathPreviewTimeline::LocateResult here =
                previewTimeline.locate();

            if (here.valid && !previewTimeline.segments.empty()) {

                const PreviewSegment& segment =
                    previewTimeline.segments[here.segmentIndex];

                target.state = segment.state;
                target.progress = here.localProgress;

                return target;
            }

            target.state = materialStateWithToolPathForPreview(project);

            if (target.state) {
                target.progress = previewTimeline.pathProgressForState(
                    target.state,
                    project
                );
            }

            return target;
        }

        View3d::Actor* deltaActorForToolPreviewDrawOrder(
            Cam::App::MaterialState* activeState
        ) {

            if (!activeState) { return nullptr; }

            for (Cam::Gui::World::MaterialState* view : materialViews) {

                if (!view || view->state != activeState) { continue; }

                if (view->deltaActor) {
                    return view->deltaActor;
                }
            }

            for (Cam::Gui::World::MaterialState* view : materialViews) {

                if (view && view->deltaActor) {
                    return view->deltaActor;
                }
            }

            return nullptr;
        }

        void repositionToolPreviewDrawOrder(Cam::App::MaterialState* activeState) {

            if (!view3d || !toolPreviewActor) { return; }

            View3d::Actor* before = deltaActorForToolPreviewDrawOrder(activeState);

            if (!before) { return; }

            view3d->insertActorBefore(toolPreviewActor, before);
        }

        void syncSharedToolPreview(
            Cam::App::Project* project
        ) {

            if (!toolPreviewActor || !toolPreviewActor->mesh) { return; }

            toolPreviewTriangles.clear();
            toolPreviewActor->visible = false;

            const ToolPreviewTarget target = activeToolPreviewTarget(project);

            if (!target.state || !target.state->hasToolPath) {
                toolPreviewActor->mesh->dirty = true;
                return;
            }

            const bool hasMesh = Cam::Gui::ToolPath::syncToolPreviewMesh(
                toolPreviewTriangles,
                target.state,
                target.progress,
                toolPreviewActor->mesh->color
            );

            toolPreviewActor->visible = hasMesh;
            toolPreviewActor->mesh->dirty = true;

            repositionToolPreviewDrawOrder(target.state);
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

            Cam::App::MaterialState* state = displayedState();

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

        bool materialViewListMatchesProject(
            Cam::App::Project* project
        ) {
            if (!project) {
                return materialViews.empty();
            }

            if (materialViews.size() != project->states.size()) {
                return false;
            }

            for (Cam::App::MaterialState* state : project->states) {
                if (!viewForState(state)) {
                    return false;
                }
            }

            for (Cam::Gui::World::MaterialState* view : materialViews) {

                if (!view) { return false; }

                if (!projectOwnsState(project, view->state)) {
                    return false;
                }
            }

            return true;
        }

        void clearMaterialViews() {

            for (Cam::Gui::World::MaterialState* view : materialViews) {
                delete view;
            }

            materialViews.clear();

            representedProject = nullptr;
            representedDisplayedState = nullptr;
            representedWorkingState = nullptr;
            representedStateCount = 0;
            representationDirty = true;
        }

        void requestClearMaterialViews(Event& e) {

            clearMaterialViewsRequested = true;
            representationDirty = true;
            refresh(e);
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

            representedProject->linkMaterialStateToolPaths();

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

        void applyVisibilityPolicy() {

            Cam::App::Project* project = activeProject();
            Cam::App::MaterialState* primary = project ? project->primaryViewState() : nullptr;

            size_t primaryIndex = static_cast<size_t>(-1);

            if (project && primary) {
                primaryIndex = project->indexOf(primary);
            }

            const bool faceEditingActive = (
                project &&
                project->workingState &&
                project->displayedState == project->workingState
            );

            for (Cam::Gui::World::MaterialState* view : materialViews) {

                if (!view) { continue; }

                view->hideAll();

                if (!project || !view->state) { continue; }
                if (!project->isViewSelected(view->state)) { continue; }

                const size_t stateIndex = project->indexOf(view->state);

                if (view->state == primary) {
                    view->showDisplayed();
                    continue;
                }

                if (primary && stateIndex > primaryIndex) {
                    view->showOverlays();
                }
            }

            // Picking follows the editable working view (displayed == working),
            // not only the primary overlay/base layer.
            if (faceEditingActive) {

                Cam::Gui::World::MaterialState* editView =
                    viewForState(project->workingState);

                if (
                    editView &&
                    project->isViewSelected(project->workingState)
                ) {
                    editView->enablePicking();
                }
            }
        }

        void applyDefaultVisibilityPolicy() {
            applyVisibilityPolicy();
        }

        // Machine simulation
        //--------------------------------------------------

        // Build the machine definition for the current project setup:
        // 3+1 indexed, using the user-defined axis origin as the A-axis pivot
        // and the user-defined X direction as the rotary axis.
        Cam::Machine::MachineDefinition buildMachineDefinition(
            Cam::App::MaterialState* state
        ) {
            Rev::Core::Pos3 pivot      = {};
            Rev::Core::Pos3 rotaryAxis = { 1.0f, 0.0f, 0.0f };  // world X default

            if (state->model.hasAxisOrigin) {
                pivot = state->model.axisOrigin;
            }

            if (state->model.hasAxisX) {
                rotaryAxis = state->model.axisXDirection;
            }

            return Cam::Machine::MachineDefinition::ThreePlusOne(pivot, rotaryAxis);
        }

        // Return a reference to the solved MachineToolPath for a given state,
        // solving it on-demand if not yet cached or if the cache is dirty.
        Cam::Machine::MachineToolPath const* getMachineToolPath(
            Cam::App::MaterialState* state
        ) {
            if (!state || !state->hasToolPath) { return nullptr; }

            if (machineToolPathsDirty) {
                machineToolPaths.clear();
                machineToolPathsDirty = false;
            }

            auto it = machineToolPaths.find(state);

            if (it == machineToolPaths.end()) {

                Cam::Machine::MachineToolPath solved =
                    Cam::Machine::IKSolver::solve(
                        state->toolPath,
                        buildMachineDefinition(state)
                    );

                dbg(
                    "[WorldView] IK solved for '%s': %zu points, %zu invalid",
                    state->name.c_str(),
                    solved.size(),
                    solved.invalidCount()
                );

                it = machineToolPaths.emplace(state, std::move(solved)).first;
            }

            return &it->second;
        }

        // Compute the part's world transform matrix for the current preview
        // instant.  Returns false (and an identity matrix) when not applicable.
        //
        // KEY INSIGHT: the IK solver defines machineXYZ = M · (toolpath point),
        // where M is exactly this part-pose matrix.  So applying M to EVERY
        // actor built in absolute CAD space — part mesh, delta, toolpath lines,
        // AND the tool cylinder — places each one precisely where the machine
        // puts it.  There are not two tracks (part + tool); there is one rigid
        // transform applied to the whole scene.  This is what makes the machine
        // view exactly the IK output, with zero per-vertex CPU work.
        bool currentPartMatrix(float outM[16]) {

            Cam::Machine::Pose::identityMatrix(outM);

            const ToolPathPreviewTimeline::LocateResult here =
                previewTimeline.locate();

            if (!here.valid || previewTimeline.segments.empty()) { return false; }

            Cam::App::MaterialState* state =
                previewTimeline.segments[here.segmentIndex].state;

            Cam::Machine::MachineToolPath const* path = getMachineToolPath(state);

            if (!path || path->empty()) { return false; }

            // The exact IK transform: rotation about the machine's fixed rotary
            // axis by the interpolated index angle, pivoted at the stock centre.
            return path->partMatrixAtProgress(here.localProgress, outM);
        }

        // Set one world matrix on every actor that belongs to the physical
        // workpiece (all material views' meshes + toolpaths) and on the shared
        // tool preview.  In absolute mode this is identity; in machine mode it
        // is the current part pose.  Always set every frame — never stale.
        void applyWorldTransforms() {

            float M[16];

            const bool machine =
                (previewMode == PreviewMode::MachineSimulation) &&
                currentPartMatrix(M);

            if (!machine) { Cam::Machine::Pose::identityMatrix(M); }

            for (Cam::Gui::World::MaterialState* v : materialViews) {

                if (!v) { continue; }

                if (v->partActor)        { v->partActor->setWorldTransform(M); }
                if (v->deltaActor)       { v->deltaActor->setWorldTransform(M); }
                if (v->toolPath.actor)   { v->toolPath.actor->setWorldTransform(M); }
            }

            if (toolPreviewActor) { toolPreviewActor->setWorldTransform(M); }
        }

        void syncAllMaterialViews() {

            rebuildPreviewTimelineIfNeeded();

            Cam::App::Project* project = activeProject();

            for (Cam::Gui::World::MaterialState* view : materialViews) {

                if (!view) { continue; }

                const double previewProgress = previewTimeline.pathProgressForState(
                    view->state,
                    project
                );

                view->sync(previewProgress);
            }

            syncSharedToolPreview(project);

            // Every actor — part meshes, deltas, toolpath lines, tool cylinder —
            // is built in absolute CAD space by the calls above.  Now apply a
            // single world transform: identity in absolute mode, or the current
            // part pose in machine sim mode, which rigidly places the entire
            // scene exactly where the machine would have it.
            applyWorldTransforms();

            if (previewBar && shared && shared->event) {
                previewBar->syncTimeDisplay(
                    previewTimeline.elapsedSeconds,
                    previewTimeline.totalDurationSeconds,
                    *shared->event
                );
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

            bool stateListChanged = !materialViewListMatchesProject(project);

            if (stateListChanged) {
                syncMaterialViewList();
            }

            representedDisplayedState = project->displayedState;
            representedWorkingState = project->workingState;
            representedStateCount = project->states.size();

            project->linkMaterialStateToolPaths();

            markPreviewTimelineDirty();

            applyVisibilityPolicy();
            syncAllMaterialViews();
            syncAxisLines();
        }

        // External sync hook
        //--------------------------------------------------

        void sync(Event& e) {

            representationDirty = true;
            markPreviewTimelineDirty();

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

            Cam::Gui::World::MaterialState* worldState = displayedMaterialView();

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

            sync(e);
        }

        void selectAxisPickFaceAtMouse(Event& e) {

            if (!app || !view3d) { return; }

            if (!displayedModelIsEditable()) {
                dbg("Select the working state to pick axis reference faces.");
                return;
            }

            Cam::Gui::World::MaterialState* worldState = displayedMaterialView();

            if (!worldState || !worldState->pickActor) { return; }

            Cam::App::Model* editable = selectionModel();

            if (!editable) { return; }

            View3d::Hit hit;

            if (!view3d->hitTest(e.mouse.pos, hit)) { return; }
            if (hit.actor != worldState->pickActor) { return; }

            size_t tri = hit.triangleId;

            if (tri >= editable->render.triangleFaceIds.size()) { return; }

            size_t faceId = editable->render.triangleFaceIds[tri];

            editable->toggleAxisPickFace(faceId);

            dbg(
                "[WorldView] Axis pick face %zu%s",
                faceId,
                editable->hasAxisPickFace() ? "" : " (cleared)"
            );

            sync(e);
        }

        View3d::Ray pickRayFromMouse(Event& e) const {

            return view3d->camera.rayFromMouse(
                e.mouse.pos,
                view3d->canvasWidth(),
                view3d->canvasHeight()
            );
        }

        bool trySelectDisplayedPickPointAtMouse(Event& e) {

            if (!app || !view3d) { return false; }

            if (!displayedModelIsEditable()) {
                return false;
            }

            Cam::Gui::World::MaterialState* worldState = displayedMaterialView();

            if (!worldState) { return false; }

            Cam::App::Model* editable = selectionModel();

            if (!editable) { return false; }

            Rev::Core::Pos3 hitPoint;

            if (!worldState->hitTestDisplayedPickPoint(
                pickRayFromMouse(e),
                editable,
                hitPoint
            )) {
                return false;
            }

            if (!editable->toggleAxisPickPoint(hitPoint)) {
                return false;
            }

            dbg("[WorldView] selecting the point!");

            sync(e);

            return true;
        }

        void updateAxisPickHover(Event& e) {

            if (!view3d) { return; }

            Cam::Gui::World::MaterialState* worldState = displayedMaterialView();

            if (!worldState) { return; }

            Cam::App::Model* editable = selectionModel();

            const bool hasDisplayedPoints =
                editable &&
                (
                    !editable->axisPickCandidates.empty() ||
                    !editable->axisPickSelectedPoints.empty()
                );

            if (!hasDisplayedPoints) {
                worldState->setAxisPickHoveredCandidate(
                    Cam::Gui::World::MaterialState::NoAxisPickHover
                );
                worldState->syncAxisPickMarkers();
                return;
            }

            size_t hitIndex = Cam::Gui::World::MaterialState::NoAxisPickHover;

            Rev::Core::Pos3 hitPoint;

            worldState->hitTestDisplayedPickPoint(
                pickRayFromMouse(e),
                editable,
                hitPoint,
                &hitIndex
            );

            worldState->setAxisPickHoveredCandidate(hitIndex);

            if (worldState->axisPickMarkerActor && worldState->axisPickMarkerActor->lines) {
                worldState->syncAxisPickMarkers();
            }
        }

        // Copy the axis frame from one model to every state in the project.
        // The axis/origin is a property of the physical part, not a machining
        // step — all states share the same coordinate system.
        void propagateAxisToAllStates(Cam::App::Model* source) {

            Cam::App::Project* project = activeProject();

            if (!project || !source) { return; }

            for (Cam::App::MaterialState* state : project->states) {

                if (!state) { continue; }

                Cam::App::Model* dest = &state->model;

                if (dest == source) { continue; }

                dest->axisOrigin     = source->axisOrigin;
                dest->hasAxisOrigin  = source->hasAxisOrigin;

                dest->axisXDirection = source->axisXDirection;
                dest->hasAxisX       = source->hasAxisX;

                dest->axisYDirection = source->axisYDirection;
                dest->hasAxisY       = source->hasAxisY;

                dest->axisZDirection = source->axisZDirection;
                dest->hasAxisZ       = source->hasAxisZ;
            }

            // Axis changed — invalidate the machine toolpath so IK re-solves
            // with the updated frame on the next preview tick.
            machineToolPathsDirty = true;
        }

        bool centerOriginFromSelection(Event& e) {

            if (!displayedModelIsEditable()) {
                dbg("[WorldView] Select the working state to center the origin.");
                return false;
            }

            Cam::App::Model* editable = selectionModel();

            if (!editable) { return false; }

            if (!editable->centerOriginFromSelectedPoints()) {
                return false;
            }

            propagateAxisToAllStates(editable);

            sync(e);
            notifyStateChanged(e);

            return true;
        }

        bool defineAxisFromSelection(Event& e, char axis) {

            if (!displayedModelIsEditable()) {
                dbg("[WorldView] Select the working state to define axes.");
                return false;
            }

            Cam::App::Model* editable = selectionModel();

            if (!editable) { return false; }

            bool defined = false;

            switch (axis) {

                case 'X':
                case 'x':
                    defined = editable->defineAxisXFromSelectedPoints();
                    break;

                case 'Y':
                case 'y':
                    defined = editable->defineAxisYFromSelectedPoints();
                    break;

                case 'Z':
                case 'z':
                    defined = editable->defineAxisZFromSelectedPoints();
                    break;

                default:
                    return false;
            }

            if (!defined) {
                return false;
            }

            propagateAxisToAllStates(editable);

            sync(e);
            notifyStateChanged(e);

            return true;
        }

        void selectSliceFaceAtMouse(Event& e) {

            if (!app || !view3d) { return; }

            Cam::Gui::World::MaterialState* worldState = displayedMaterialView();

            if (!worldState || !worldState->pickActor) { return; }

            Cam::App::Model* visible = worldState->displayModel();

            if (!visible || !visible->loaded) { return; }

            View3d::Actor* pickActor = worldState->pickActor;

            bool wasSelectable = pickActor->selectable;

            pickActor->mesh->pTriangles = &visible->render.triangles;
            pickActor->selectable = true;

            View3d::Hit hit;

            bool gotHit = view3d->hitTest(e.mouse.pos, hit);

            pickActor->selectable = wasSelectable;

            if (!gotHit || hit.actor != pickActor) { return; }

            size_t tri = hit.triangleId;

            if (tri >= visible->render.triangleFaceIds.size()) { return; }

            size_t faceId = visible->render.triangleFaceIds[tri];

            if (!app->setDisplayedSlicePlaneFromFace(faceId, *visible)) { return; }

            dbg(
                "[WorldView] Slice plane from face %zu axis=(%.3f %.3f %.3f)",
                faceId,
                visible->faceNormal(faceId).x,
                visible->faceNormal(faceId).y,
                visible->faceNormal(faceId).z
            );

            sync(e);

            notifyStateChanged(e);
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

        bool offsetSelected(Event& e, double distance = 1) {

            if (!app || !app->offsetSelected(distance)) {
                dbg("offset faces failed — see [Offset] logs above");
                return false;
            }

            sync(e);

            dbg("offset selected faces");

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

        bool recalculateToolPath(Event& e) {

            if (!app || !app->recalculateToolPath()) {
                dbg("toolpath recalculate failed or nothing to compute");
                return false;
            }

            sync(e);

            dbg("recalculated toolpath");

            notifyStateChanged(e);

            return true;
        }

        // Computing
        //--------------------------------------------------

        void computeChildren(Event& e) override {

            if (clearMaterialViewsRequested) {
                clearMaterialViewsRequested = false;
                clearMaterialViews();
            }

            if (representationDirty) {
                representationDirty = false;
                syncRepresentation();
            }

            Box::computeChildren(e);
        }

        // Events
        //--------------------------------------------------

        void mouseMove(Event& e) override {

            updateAxisPickHover(e);

            Box::mouseMove(e);
        }

        void mouseDown(Event& e) override {

            if (e.keyboard.alt) {

                if (trySelectDisplayedPickPointAtMouse(e)) {
                    e.propagate = false;
                    return;
                }

                selectAxisPickFaceAtMouse(e);

                e.propagate = false;
                return;
            }

            if (e.keyboard.ctrl) {

                if (trySelectDisplayedPickPointAtMouse(e)) {
                    e.propagate = false;
                    return;
                }

                selectFaceAtMouse(e);

                e.propagate = false;
                return;
            }

            Box::mouseDown(e);
        }

        void mouseUp(Event& e) override {

            if (e.keyboard.shift) {

                float dragDistance = (e.mouse.pos - e.mouse.down).pythag();

                if (dragDistance < 5.0f) {

                    if (trySelectDisplayedPickPointAtMouse(e)) {
                        e.propagate = false;
                        return;
                    }

                    selectSliceFaceAtMouse(e);

                    e.propagate = false;
                    return;
                }
            }

            Box::mouseUp(e);
        }

        void clearAllSelections(Event& e) {

            bool changed = false;

            Cam::App::Model* model = selectionModel();

            if (model) {
                const bool hadModelSelection =
                    !model->selectedFaceIds.empty() ||
                    model->hasAxisPickFace() ||
                    !model->axisPickSelectedPoints.empty();

                if (hadModelSelection) {
                    model->clearAllSelections();
                    changed = true;
                }
            }

            Cam::App::MaterialState* material = displayedState();

            if (material && material->toolPath.hasSliceFace()) {
                material->toolPath.clearSlicePlane();
                changed = true;
            }

            Cam::Gui::World::MaterialState* worldState = displayedMaterialView();

            if (worldState) {
                worldState->setAxisPickHoveredCandidate(
                    Cam::Gui::World::MaterialState::NoAxisPickHover
                );
            }

            if (changed) {
                sync(e);
                notifyStateChanged(e);
            }
        }

        void keyDown(Event& e) override {

            if (e.keyboard.key == "escape") {
                clearAllSelections(e);
                e.propagate = false;
                return;
            }

            if (gestures.track(e)) {
                e.propagate = false;
                return;
            }

            if (e.keyboard.key == "delete" || e.keyboard.del) {
                defeatureSelected(e);
                e.propagate = false;
                return;
            }

            if (e.keyboard.key == "enter" || e.keyboard.enter) {
                commitWorkingState(e);
                e.propagate = false;
                return;
            }

            if (e.keyboard.key == "r") {
                recalculateToolPath(e);
                e.propagate = false;
                return;
            }

            // Preview transport shortcuts
            //--------------------------------------------------

            if (e.keyboard.key == " ") {
                if (previewBar) {
                    if (previewBar->isPlaying()) { previewBar->pause(e); }
                    else                         { previewBar->play(e);  }
                }
                e.propagate = false;
                return;
            }

            if (e.keyboard.arrows.left) {
                if (previewBar) { previewBar->stepBack(e); }
                e.propagate = false;
                return;
            }

            if (e.keyboard.arrows.right) {
                if (previewBar) { previewBar->stepForward(e); }
                e.propagate = false;
                return;
            }

            if (e.keyboard.arrows.up || e.keyboard.arrows.down) {
                if (previewBar) {
                    // Double or halve the speed, clamped to [min, max].
                    const double current = previewBar->playbackSpeed;
                    const double next = e.keyboard.arrows.up
                        ? std::min(current * 2.0, PreviewBar::MaxPlaybackSpeed)
                        : std::max(current / 2.0, PreviewBar::MinPlaybackSpeed);

                    if (std::fabs(next - current) > 1e-9) {
                        previewBar->playbackSpeed = next;

                        if (previewBar->speedInput) {
                            previewBar->speedInput->setValue(next);
                        }

                        refresh(e);
                    }
                }
                e.propagate = false;
                return;
            }

            Box::keyDown(e);
        }
    };
}