module;

#include <algorithm>
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
import Rev.Element.Event.GestureTracker;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Slider;

import Rev.Core.Pos3;
import Rev.Core.Color;
import Rev.Core.Vertex3;

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

import Rev.Core.Animator;

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

    namespace ToolPathPreviewStyle {

        Style Panel = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct }
        };

        Style Transport = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .margin = { 0_px, 0_px, 4_px, 0_px }
        };

        Style TransportButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size = { .width = 30_px, .height = 28_px },
            .margin = { .right = 6_px },
            .padding = { 4_px, 6_px, 4_px, 6_px },
            .border = { .radius = 6_px },
            .cursor = Cursor::Hand
        };

        Style TransportButtonLabel = {
            .text = { .size = 13_px }
        };
    }

    struct WorldView : public Box {

        Cam::App::AppState* app = nullptr;

        View3d::View* view3d = nullptr;
        Box* toolPathPreviewPanel = nullptr;
        Slider* toolPathPreviewSlider = nullptr;

        float toolPathPreviewPercent = 100.0f;
        Rev::Core::Animator toolPathPreviewAnimator;

        static constexpr float ToolPathPreviewPlaySpeed = 12.0f;
        static constexpr float ToolPathPreviewStepPercent = 1.0f;
        static constexpr double ToolPathPreviewFrameRate = 150.0;

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

            createToolPathPreviewSlider();

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

        void createToolPathPreviewSlider() {

            toolPathPreviewPanel = new Box(
                this,
                { &ToolPathPreviewStyle::Panel },
                "ToolPathPreviewPanel"
            );

            Box* transport = new Box(
                toolPathPreviewPanel,
                Theme::withButton({
                    &ToolPathPreviewStyle::Transport,
                    &Theme::Styles::ButtonHover,
                    &Theme::Styles::ButtonPress
                }),
                "ToolPathPreviewTransport"
            );

            auto makeTransportButton = [&](
                const char* label,
                const char* name,
                std::function<void(Event&)> onClick
            ) -> Box* {

                Box* button = new Box(
                    transport,
                    Theme::withButton({
                        &ToolPathPreviewStyle::TransportButton,
                        &Theme::Styles::ButtonHover,
                        &Theme::Styles::ButtonPress
                    }),
                    name
                );

                new Text(
                    button,
                    label,
                    Theme::layer(
                        { &ToolPathPreviewStyle::TransportButtonLabel },
                        { &Theme::Styles::ButtonLabel }
                    )
                );

                button->onClick([onClick](Event& e) {
                    onClick(e);
                    e.propagate = false;
                });

                return button;
            };

            makeTransportButton(
                "Play",
                "ToolPathPreviewPlay",
                [this](Event& e) { playToolPathPreview(e); }
            );

            makeTransportButton(
                "Pause",
                "ToolPathPreviewPause",
                [this](Event& e) { pauseToolPathPreview(e); }
            );

            makeTransportButton(
                "<",
                "ToolPathPreviewStepBack",
                [this](Event& e) { stepToolPathPreviewBack(e); }
            );

            makeTransportButton(
                ">",
                "ToolPathPreviewStepForward",
                [this](Event& e) { stepToolPathPreviewForward(e); }
            );

            Slider::SliderData previewSliderData;
            previewSliderData.min = 0.0f;
            previewSliderData.max = 100.0f;
            previewSliderData.def = 1.0f;
            previewSliderData.val = 100.0f;

            toolPathPreviewSlider = new Slider(
                toolPathPreviewPanel,
                previewSliderData,
                {},
                "ToolPathPreviewSlider"
            );

            toolPathPreviewSlider->labelText->setContent("Toolpath preview: ");
            toolPathPreviewSlider->style->size = { .width = 100_pct };
            toolPathPreviewSlider->style->padding = { 8_px, 12_px, 10_px, 12_px };
            toolPathPreviewSlider->labelText->styles.add(&Theme::Styles::MutedText);
            toolPathPreviewSlider->valueText->styles.add(&Theme::Styles::Text);

            auto onPreviewChanged = [this](Event& e) {
                pauseToolPathPreview(e);
                syncToolPathPreview(e);
            };

            toolPathPreviewSlider->sliderContainer->onMouseDown(onPreviewChanged);
            toolPathPreviewSlider->sliderContainer->onDrag(onPreviewChanged);

            toolPathPreviewAnimator.onFrame([this](Rev::Core::AnimationEvent& frame) {

                if (!shared || !shared->event) { return; }

                Event& e = *shared->event;

                const float deltaPercent =
                    (float(frame.deltaMs) / 1000.0f) * ToolPathPreviewPlaySpeed;

                setToolPathPreviewPercent(
                    toolPathPreviewPercent + deltaPercent,
                    e,
                    true
                );

                if (toolPathPreviewPercent >= 100.0f) {
                    toolPathPreviewAnimator.stop();
                }
            });

            toolPathPreviewAnimator.setFrequency(ToolPathPreviewFrameRate);
        }

        double toolPathPreviewProgress() const {
            return double(toolPathPreviewPercent) / 100.0;
        }

        void setToolPathPreviewPercent(
            float percent,
            Event& e,
            bool requestRepaint = false
        ) {

            toolPathPreviewPercent = std::clamp(percent, 0.0f, 100.0f);

            if (toolPathPreviewSlider) {
                toolPathPreviewSlider->setVal(toolPathPreviewPercent);
                toolPathPreviewSlider->refresh(e);
            }

            syncToolPathPreview(e);

            if (requestRepaint) {
                refresh(e);
            }
        }

        void pauseToolPathPreview(Event& e) {

            if (!toolPathPreviewAnimator.isPlaying()) { return; }

            toolPathPreviewAnimator.pause();
            refresh(e);
        }

        void playToolPathPreview(Event& e) {

            if (toolPathPreviewPercent >= 100.0f) {
                setToolPathPreviewPercent(0.0f, e);
            }

            toolPathPreviewAnimator.play();
            refresh(e);
        }

        void stepToolPathPreviewBack(Event& e) {

            pauseToolPathPreview(e);
            setToolPathPreviewPercent(
                toolPathPreviewPercent - ToolPathPreviewStepPercent,
                e
            );
        }

        void stepToolPathPreviewForward(Event& e) {

            pauseToolPathPreview(e);
            setToolPathPreviewPercent(
                toolPathPreviewPercent + ToolPathPreviewStepPercent,
                e
            );
        }

        void syncToolPathPreview(Event& e) {

            if (toolPathPreviewSlider) {
                toolPathPreviewPercent = toolPathPreviewSlider->data.val;
            }

            syncAllMaterialViews();

            if (view3d) {
                view3d->refresh(e);
            }
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
                toolPathPreviewSequence(project);

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
            Cam::App::Project* project,
            double globalProgress
        ) {

            ToolPreviewTarget target = {};

            if (!project) { return target; }

            const std::vector<Cam::App::MaterialState*> sequence =
                toolPathPreviewSequence(project);

            if (!sequence.empty()) {

                globalProgress = std::clamp(globalProgress, 0.0, 1.0);

                for (size_t i = 0; i < sequence.size(); i++) {

                    const double localProgress = sequentialToolPathPreviewProgress(
                        globalProgress,
                        i,
                        sequence.size()
                    );

                    if (localProgress > 1e-9 && localProgress < 1.0 - 1e-9) {
                        target.state = sequence[i];
                        target.progress = localProgress;
                        return target;
                    }
                }

                if (globalProgress >= 1.0 - 1e-9) {
                    target.state = sequence.back();
                    target.progress = 1.0;
                    return target;
                }

                target.state = sequence.front();
                target.progress = 0.0;
                return target;
            }

            target.state = materialStateWithToolPathForPreview(project);
            target.progress = std::clamp(globalProgress, 0.0, 1.0);

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
            Cam::App::Project* project,
            double globalProgress
        ) {

            if (!toolPreviewActor || !toolPreviewActor->mesh) { return; }

            toolPreviewTriangles.clear();
            toolPreviewActor->visible = false;

            const ToolPreviewTarget target = activeToolPreviewTarget(
                project,
                globalProgress
            );

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

        static double sequentialToolPathPreviewProgress(
            double globalProgress,
            size_t sequenceIndex,
            size_t sequenceCount
        ) {
            if (sequenceCount <= 1) {
                return globalProgress;
            }

            globalProgress = std::clamp(globalProgress, 0.0, 1.0);

            const double segmentSize = 1.0 / double(sequenceCount);
            const double segmentStart = segmentSize * double(sequenceIndex);
            const double segmentEnd = segmentStart + segmentSize;

            if (globalProgress <= segmentStart) { return 0.0; }
            if (globalProgress >= segmentEnd) { return 1.0; }

            return (globalProgress - segmentStart) / segmentSize;
        }

        std::vector<Cam::App::MaterialState*> toolPathPreviewSequence(
            Cam::App::Project* project
        ) {
            std::vector<Cam::App::MaterialState*> sequence;

            if (!project || project->viewSelection.size() <= 1) {
                return sequence;
            }

            Cam::App::MaterialState* primary = project->primaryViewState();

            if (!primary) { return sequence; }

            const size_t primaryIndex = project->indexOf(primary);

            for (Cam::App::MaterialState* state : project->viewSelection) {

                if (!state) { continue; }

                const size_t stateIndex = project->indexOf(state);

                if (stateIndex < primaryIndex) { continue; }
                if (!state->hasToolPath) { continue; }

                sequence.push_back(state);
            }

            return sequence;
        }

        double toolPathPreviewProgressForState(
            Cam::App::MaterialState* state,
            Cam::App::Project* project,
            double globalProgress
        ) {
            if (!state || !project) {
                return globalProgress;
            }

            const std::vector<Cam::App::MaterialState*> sequence =
                toolPathPreviewSequence(project);

            if (sequence.size() <= 1) {
                return globalProgress;
            }

            for (size_t i = 0; i < sequence.size(); i++) {

                if (sequence[i] != state) { continue; }

                return sequentialToolPathPreviewProgress(
                    globalProgress,
                    i,
                    sequence.size()
                );
            }

            return 0.0;
        }

        void syncAllMaterialViews() {

            const double globalProgress = toolPathPreviewProgress();

            Cam::App::Project* project = activeProject();

            for (Cam::Gui::World::MaterialState* view : materialViews) {

                if (!view) { continue; }

                const double previewProgress = toolPathPreviewProgressForState(
                    view->state,
                    project,
                    globalProgress
                );

                view->sync(previewProgress);
            }

            syncSharedToolPreview(project, globalProgress);
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

            applyVisibilityPolicy();
            syncAllMaterialViews();
            syncAxisLines();
        }

        // External sync hook
        //--------------------------------------------------

        void sync(Event& e) {

            representationDirty = true;

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

            Box::keyDown(e);
        }
    };
}