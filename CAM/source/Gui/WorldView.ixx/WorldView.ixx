module;

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <cstdio>

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
import Cam.App.Tool;
import Cam.App.ToolLibrary;
import Cam.App.ToolPath;

import Cam.Gui.World.MaterialState;
import Cam.Gui.ToolPath;
import Cam.Gui.Theme;
import Cam.Gui.PreviewBar;
import Cam.Gui.ToolPathPreview;

import Cam.Machine.Pose;
import Cam.Machine.Definition;
import Cam.Machine.ToolPath;
import Cam.Machine.IKSolver;

import Carvera.MachineLink;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace View3d = Rev::Element::View3d;

    enum class WorldViewCommand {
        Defeature,
        ExtendFeature,
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

        // The tool whose cached mesh the preview actor currently points at;
        // used to re-upload to the GPU only when the tool/geometry changes.
        Cam::App::Tool* previewTool = nullptr;
        std::size_t previewToolRevision = 0;

        // While in EXECUTE mode, poll machine telemetry so the tool tracks the
        // real machine (incl. jogs), not the computed preview.
        Rev::Core::Animator executePoll { 30 };

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
            { "ef", WorldViewCommand::ExtendFeature },
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

            // Live telemetry polling for EXECUTE mode (tool follows the real
            // machine, including jogs, rather than the computed preview).
            executePoll.setFrequency(30.0);
            executePoll.onFrame([this](Rev::Core::AnimationEvent&) {
                if (previewMode != PreviewMode::Execute) { return; }
                if (!Carvera::MachineLink::instance().connected()) { return; }
                if (!shared || !shared->event) { return; }
                syncSharedToolPreview(activeProject());
                if (view3d) { view3d->refresh(*shared->event); }
            });

            // Push-based position updates: the machine link broadcasts every
            // telemetry frame, so the tool tracks the real machine immediately
            // (e.g. while jogging) without waiting on the poll.
            Carvera::MachineLink::instance().onTelemetry =
                [this](float, float, float, float) {
                    if (previewMode != PreviewMode::Execute) { return; }
                    if (!shared || !shared->event) { return; }
                    syncSharedToolPreview(activeProject());
                    if (view3d) { view3d->refresh(*shared->event); }
                };

            gestures.onGesture = [this](WorldViewCommand command, Event& e) {

                switch (command) {

                    case WorldViewCommand::Defeature: {
                        defeatureSelected(e);
                        break;
                    }

                    case WorldViewCommand::ExtendFeature: {
                        extendSelected(e);
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

            executePoll.stop();
            Carvera::MachineLink::instance().onTelemetry = nullptr;

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

                // EXECUTE: pressing play streams the program to the machine.
                if (previewMode == PreviewMode::Execute) {
                    streamExecuteProgram();
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

                // Leaving EXECUTE aborts any pending stream (already-buffered
                // moves on the controller still run; use the machine's stop).
                if (mode != PreviewMode::Execute) {
                    Carvera::MachineLink::instance().clearQueue();
                }

                // Poll telemetry only while showing the real machine.
                if (mode == PreviewMode::Execute) { executePoll.play(); }
                else { executePoll.stop(); }

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

        // Column-major model matrix that places a local-space tool (tip at the
        // origin, body along +Z) at `tip`, aligned to `directionIn`.
        static void toolPlacementMatrix(
            const Rev::Core::Pos3& tip,
            const Rev::Core::Pos3& directionIn,
            float out[16]
        ) {
            Rev::Core::Pos3 z = directionIn.normalized();

            const Rev::Core::Pos3 reference =
                std::fabs(z.z) < 0.9f
                    ? Rev::Core::Pos3(0.0f, 0.0f, 1.0f)
                    : Rev::Core::Pos3(1.0f, 0.0f, 0.0f);

            Rev::Core::Pos3 x = reference.cross(z);
            const float xLen = x.pythag();
            x = xLen > 1e-6f ? x / xLen : Rev::Core::Pos3(1.0f, 0.0f, 0.0f);

            const Rev::Core::Pos3 y = z.cross(x).normalized();

            out[0]  = x.x; out[1]  = x.y; out[2]  = x.z; out[3]  = 0.0f;
            out[4]  = y.x; out[5]  = y.y; out[6]  = y.z; out[7]  = 0.0f;
            out[8]  = z.x; out[9]  = z.y; out[10] = z.z; out[11] = 0.0f;
            out[12] = tip.x; out[13] = tip.y; out[14] = tip.z; out[15] = 1.0f;
        }

        void syncSharedToolPreview(
            Cam::App::Project* project
        ) {

            if (!toolPreviewActor || !toolPreviewActor->mesh) { return; }

            toolPreviewActor->visible = false;

            Carvera::MachineLink& link = Carvera::MachineLink::instance();

            // EXECUTE mode: register the CAD begin-work point as the reference,
            // computed IN the user frame so it matches what the streamer emits.
            const bool executeMode = (previewMode == PreviewMode::Execute);

            UserFrame frame;
            Rev::Core::Pos3 beginWorkInFrame = {};
            Rev::Core::Pos3 cadBeginWork = {};

            if (executeMode && project) {
                frame = currentUserFrame(project);
                cadBeginWork = beginWorkOrigin(project, frame);
                beginWorkInFrame = frame.toFrame(cadBeginWork);

                link.setCadOrigin(cadBeginWork.x, cadBeginWork.y, cadBeginWork.z);

                // If no precise origin has been set yet (via Set Origin), anchor
                // to the current machine position so jogging shows relative
                // motion immediately.  Set Origin later re-anchors precisely.
                float tcx, tcy, tcz, tca;
                float mx, my, mz, ocx2, ocy2, ocz2;

                if (link.telemetry(tcx, tcy, tcz, tca) &&
                    !link.workOrigin(mx, my, mz, ocx2, ocy2, ocz2)) {
                    link.captureMachineOrigin(tcx, tcy, tcz);
                }
            }

            const ToolPreviewTarget target = activeToolPreviewTarget(project);

            // Resolve which tool to show (the previewed op, or the displayed
            // state when nothing is actively previewing — e.g. while jogging).
            Cam::App::MaterialState* toolState = target.state;

            if (!toolState && project) { toolState = project->displayedState; }

            Cam::App::Tool* tool = (toolState && app && app->toolLibrary())
                ? app->toolLibrary()->find(toolState->toolPath.toolName)
                : nullptr;

            if (!tool || tool->mesh.empty()) { return; }

            Rev::Core::Pos3 tip;
            Rev::Core::Pos3 dir = { 0.0f, 0.0f, 1.0f };
            bool haveTip = false;

            // EXECUTE: show where the machine ACTUALLY is, from telemetry.
            float tx, ty, tz, ta;
            float omx, omy, omz, ocx, ocy, ocz;

            if (executeMode &&
                link.connected() &&
                link.telemetry(tx, ty, tz, ta) &&
                link.workOrigin(omx, omy, omz, ocx, ocy, ocz)) {

                // MPos - machineOrigin = WCS coords in the user/machine frame.
                // Add the begin-work offset (also in-frame) and rotate back into
                // CAD world via frame.toWorld.  This is the exact inverse of the
                // streamer's transform — no ad-hoc axis swaps.
                const Rev::Core::Pos3 wcsInFrame = {
                    tx - omx,
                    ty - omy,
                    tz - omz
                };

                const Rev::Core::Pos3 inFrame = {
                    wcsInFrame.x + beginWorkInFrame.x,
                    wcsInFrame.y + beginWorkInFrame.y,
                    wcsInFrame.z + beginWorkInFrame.z
                };

                tip = frame.toWorld(inFrame);
                dir = frame.Z;   // tool axis is the user's +Z
                haveTip = true;
            }
            else if (target.state && target.state->hasToolPath) {

                // Otherwise: the computed preview sample.
                Cam::App::ToolPathPoint sample = {};

                if (target.state->toolPath.sampleAtProgress(target.progress, sample)) {
                    tip = sample.position;
                    dir = sample.toolDirection;
                    haveTip = true;
                }
            }

            if (!haveTip) { return; }

            toolPreviewActor->mesh->pTriangles = &tool->mesh;

            // Only re-upload when the tool (or its geometry) actually changed.
            if (tool != previewTool || tool->meshRevision != previewToolRevision) {
                toolPreviewActor->mesh->dirty = true;
                previewTool = tool;
                previewToolRevision = tool->meshRevision;
            }

            // Placement goes in modelTransform so applyWorldTransforms can still
            // apply the machine pose through worldTransform.
            float placement[16];
            toolPlacementMatrix(tip, dir, placement);

            for (int i = 0; i < 16; i++) {
                toolPreviewActor->modelTransform[i] = placement[i];
            }

            // In EXECUTE the placement is absolute CAD (no machine-sim scene
            // rotation), so clear any stale world transform left by Machine Sim.
            if (executeMode) {
                static const float identity[16] = {
                    1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
                };
                toolPreviewActor->setWorldTransform(identity);
            }

            toolPreviewActor->visible = true;

            if (target.state) { repositionToolPreviewDrawOrder(target.state); }
            else if (toolState) { repositionToolPreviewDrawOrder(toolState); }
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

        // The single unified coordinate system: the user-defined axis frame.
        //
        // The frame is what the user established with the "co" (centre origin)
        // and "ax" (define axis) gestures.  Every CAD point P is expressed in
        // that frame as:
        //   machineX = (P - axisOrigin) . X̂
        //   machineY = (P - axisOrigin) . Ŷ
        //   machineZ = (P - axisOrigin) . Ẑ
        // This is what the IK solver uses for the rotary axis, what the machine
        // physically sees, and what the streamer must emit.  When the user
        // hasn't defined a frame yet we fall back to world axes.
        struct UserFrame {
            Rev::Core::Pos3 origin = { 0.0f, 0.0f, 0.0f };
            Rev::Core::Pos3 X = { 1.0f, 0.0f, 0.0f };
            Rev::Core::Pos3 Y = { 0.0f, 1.0f, 0.0f };
            Rev::Core::Pos3 Z = { 0.0f, 0.0f, 1.0f };

            Rev::Core::Pos3 toFrame(const Rev::Core::Pos3& p) const {
                const Rev::Core::Pos3 d = p - origin;
                return { d.dot(X), d.dot(Y), d.dot(Z) };
            }

            Rev::Core::Pos3 toWorld(const Rev::Core::Pos3& f) const {
                return origin + X * f.x + Y * f.y + Z * f.z;
            }
        };

        UserFrame currentUserFrame(Cam::App::Project* project) {

            UserFrame frame;

            Cam::App::MaterialState* state = project ? project->displayedState : nullptr;

            if (!state) { return frame; }

            const Cam::App::Model& m = state->model;

            // The axis/origin is shared across the project (propagateAxisToAllStates),
            // so the displayed state's frame is the project's frame.
            m.getOrthonormalAxisFrame(frame.X, frame.Y, frame.Z);

            if (m.hasAxisOrigin) { frame.origin = m.axisOrigin; }

            return frame;
        }

        // The "begin work" origin: the centre of the top surface of the stock,
        // expressed in CAD world space but computed IN THE USER FRAME so it
        // tracks the user's defined axes (the stock is built along those axes).
        // The operator physically jogs the tool tip here and zeroes; the program
        // is streamed in user-frame coordinates relative to it.
        Rev::Core::Pos3 beginWorkOrigin(Cam::App::Project* project, const UserFrame& frame) {

            if (!project) { return frame.origin; }

            Cam::App::MaterialState* state = nullptr;

            for (Cam::App::MaterialState* s : project->states) {
                if (s && s->stockGenerated) { state = s; }  // last stock state = full stock
            }

            if (!state) { state = project->stockBaseState(); }
            if (!state) { state = project->displayedState; }
            if (!state) { return frame.origin; }

            const std::vector<Rev::Core::Vertex3>& tris = state->model.render.triangles;

            if (tris.empty()) { return frame.origin; }

            // Bounding box in the user frame: project every vertex onto X/Y/Z.
            Rev::Core::Pos3 first = frame.toFrame(tris[0]);
            Rev::Core::Pos3 mn = first;
            Rev::Core::Pos3 mx = first;

            for (const Rev::Core::Vertex3& v : tris) {
                const Rev::Core::Pos3 f = frame.toFrame(v);
                mn = Rev::Core::Pos3::min(mn, f);
                mx = Rev::Core::Pos3::max(mx, f);
            }

            // Stock-top centre: centred in X/Y of the user frame, at max Z.
            const Rev::Core::Pos3 frameCentre = {
                (mn.x + mx.x) * 0.5f,
                (mn.y + mx.y) * 0.5f,
                mx.z
            };

            // Express back in CAD world for callers that need a world point.
            return frame.toWorld(frameCentre);
        }

        // 1-based tool slot inferred from the library order (best effort, for
        // M6 T<n>); 0 if the tool isn't found.
        int toolNumber(const std::string& name) {

            if (!app) { return 0; }

            Cam::App::ToolLibrary* library = app->toolLibrary();

            if (!library) { return 0; }

            for (size_t i = 0; i < library->order.size(); i++) {
                if (library->order[i] == name) { return static_cast<int>(i) + 1; }
            }

            return 0;
        }

        // EXECUTE: build the machine program from the IK-solved path(s) of the
        // previewed states and hand it to the (flow-controlled) machine link.
        // Strictly gated: only ever sends when armed + connected.  Assumes the
        // machine's work-coordinate zero matches the CAD frame (set up before
        // the real run).
        void streamExecuteProgram() {

            Carvera::MachineLink& link = Carvera::MachineLink::instance();

            if (!link.isArmed()) {
                dbg("[Execute] not armed/connected — refusing to stream");
                return;
            }

            Cam::App::Project* project = activeProject();

            if (!project) { return; }

            // Everything streamed to the machine is expressed in the USER FRAME:
            // origin = user axisOrigin, axes = user X/Y/Z.  The begin-work
            // origin (stock-top centre) is itself defined in that frame.
            const UserFrame frame = currentUserFrame(project);
            const Rev::Core::Pos3 cadBeginWork = beginWorkOrigin(project, frame);
            const Rev::Core::Pos3 beginWorkInFrame = frame.toFrame(cadBeginWork);

            std::vector<std::string> lines;
            lines.push_back("G90\n");  // absolute positioning

            const double radToDeg = 57.29577951308232;

            std::string loadedTool;  // none loaded yet
            bool spindleOn = false;
            double maxMachineZ = -1.0e30;  // highest point reached (for the clearance lift)

            auto emitToolChange = [&](const std::string& toolName) {

                // Spindle must be off across a tool change.
                if (spindleOn) {
                    lines.push_back("M5\n");
                    spindleOn = false;
                }

                char comment[96];
                std::snprintf(comment, sizeof(comment), "(tool change: %s)\n", toolName.c_str());
                lines.push_back(comment);

                const int n = toolNumber(toolName);

                if (n > 0) {
                    char m[24];
                    std::snprintf(m, sizeof(m), "M6 T%d\n", n);
                    lines.push_back(m);
                }
                else {
                    lines.push_back("M6\n");
                }
            };

            auto appendState = [&](Cam::App::MaterialState* state) {

                if (!state || !state->hasToolPath) { return; }

                const Cam::Machine::MachineToolPath* path = getMachineToolPath(state);

                if (!path || path->empty()) { return; }

                // Tool change goes between the previous op's retract and this
                // op's first move.  The very first op doesn't get one (the tool
                // is already set up for the manual start), only transitions
                // between operations that actually change tools.
                if (!loadedTool.empty() && state->toolPath.toolName != loadedTool) {
                    emitToolChange(state->toolPath.toolName);
                }
                loadedTool = state->toolPath.toolName;

                const double feed = state->toolPath.feedRate > 0.0
                    ? state->toolPath.feedRate
                    : 250.0;

                for (const Cam::Machine::MachinePose& p : path->points) {

                    const Rev::Core::Pos3 pos = p.toolWorldPose.position;
                    const double aDeg = p.rotaryAngle * radToDeg;

                    // CAD world -> user frame -> machine, in one step.  The
                    // user frame IS the machine frame: machine X/Y/Z = the
                    // user-defined X/Y/Z directions (set by the "co" / "ax"
                    // gestures), with the begin-work point as the origin.
                    const Rev::Core::Pos3 inFrame = frame.toFrame(pos);

                    const double machineX = inFrame.x - beginWorkInFrame.x;
                    const double machineY = inFrame.y - beginWorkInFrame.y;
                    const double machineZ = inFrame.z - beginWorkInFrame.z;

                    if (machineZ > maxMachineZ) { maxMachineZ = machineZ; }

                    // Spin up just before the first real cut — but only if the
                    // spindle is armed.  Unarmed = a motion-only dry run.
                    if (!p.rapid && !spindleOn &&
                        Carvera::MachineLink::instance().isSpindleArmed()) {
                        lines.push_back("M3 S12000\n");
                        spindleOn = true;
                    }

                    char line[160];

                    if (p.rapid) {
                        std::snprintf(line, sizeof(line),
                            "G0 X%.3f Y%.3f Z%.3f A%.3f\n",
                            machineX, machineY, machineZ, aDeg);
                    }
                    else {
                        std::snprintf(line, sizeof(line),
                            "G1 X%.3f Y%.3f Z%.3f A%.3f F%.1f\n",
                            machineX, machineY, machineZ, aDeg, feed);
                    }

                    lines.push_back(line);
                }
            };

            const std::vector<Cam::App::MaterialState*> sequence =
                ToolPathPreviewTimeline::previewSequence(project);

            if (!sequence.empty()) {
                for (Cam::App::MaterialState* state : sequence) {
                    appendState(state);
                }
            }
            else {
                appendState(materialStateWithToolPathForPreview(project));
            }

            // Spindle off at the end of the program.
            if (spindleOn) {
                lines.push_back("M5\n");
                spindleOn = false;
            }

            if (lines.size() <= 1) {
                dbg("[Execute] no machine path to stream");
                return;
            }

            // From the touch point (stock surface, WCS Z0) the very first motion
            // must lift UP clear of the whole path before any lateral move, not
            // dive into the stock.  Insert it right after the G90 preamble.
            {
                const double clearance = std::max(maxMachineZ, 0.0) + 5.0;
                char zline[48];
                std::snprintf(zline, sizeof(zline), "G0 Z%.3f\n", clearance);
                lines.insert(lines.begin() + 1, std::string(zline));
            }

            dbg("[Execute] streaming %zu lines to the machine", lines.size());

            link.enqueueProgram(lines);
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

        bool extendSelected(Event& e, double distance = 10.0) {

            if (!app || !app->extendSelected(distance)) {
                dbg("extend feature failed - see [Extend] logs above");
                return false;
            }

            sync(e);

            dbg("extended selected feature");

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

        void updateFaceHover(Event& e) {

            if (!view3d) { return; }

            const bool editing = displayedModelIsEditable();

            Cam::Gui::World::MaterialState* worldState =
                editing ? displayedMaterialView() : nullptr;

            size_t newFaceId = Cam::Gui::World::MaterialState::NoHoveredFace;

            if (worldState && worldState->pickActor) {

                Cam::App::Model* model = selectionModel();

                if (model && !model->render.triangleFaceIds.empty()) {

                    bool wasSelectable = worldState->pickActor->selectable;
                    worldState->pickActor->selectable = true;

                    View3d::Hit hit;
                    bool gotHit = view3d->hitTest(e.mouse.pos, hit);

                    worldState->pickActor->selectable = wasSelectable;

                    if (gotHit && hit.actor == worldState->pickActor) {
                        size_t tri = hit.triangleId;
                        if (tri < model->render.triangleFaceIds.size()) {
                            newFaceId = model->render.triangleFaceIds[tri];
                        }
                    }
                }
            }

            for (Cam::Gui::World::MaterialState* view : materialViews) {

                if (!view) { continue; }

                size_t faceId = (view == worldState)
                    ? newFaceId
                    : Cam::Gui::World::MaterialState::NoHoveredFace;

                if (view->setHoveredFace(faceId)) {
                    view->applyFaceColors();
                    if (view->partActor && view->partActor->mesh) {
                        view->partActor->mesh->dirty = true;
                    }
                }
            }
        }

        void mouseMove(Event& e) override {

            updateAxisPickHover(e);
            updateFaceHover(e);

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