module;

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <vector>

export module Cam.Gui.World.Stage;

import Rev.Graphics.Canvas;

import Rev.Core.Color;
import Rev.Core.Pos3;
import Rev.Core.Vertex3;

import Rev.Primitive.Mesh3d;
import Rev.Primitive.Lines3d;

import Rev.Element.View3d;
import Rev.Element.View3d.Actor3d;

import Cam.App.Stage;
import Cam.App.Model;
import Cam.App.Probe;
import Cam.App.ToolPath;

import Cam.Gui.ToolPath;

export namespace Cam::Gui::World {

    namespace View3d = Rev::Element::View3d;

    struct Stage {

        Rev::Graphics::Canvas* canvas = nullptr;
        View3d::View* view = nullptr;

        Cam::App::Stage* state = nullptr;

        View3d::Actor* partActor = nullptr;   // prior model (the stock) + edit surface
        View3d::Actor* modelActor = nullptr;  // this stage's resulting model
        View3d::Actor* deltaActor = nullptr;
        View3d::Actor* pickActor = nullptr;
        View3d::Actor* axisPickMarkerActor = nullptr;
        View3d::Actor* probeMarkerActor = nullptr;
        View3d::Actor* probePlanActor = nullptr;

        std::vector<Rev::Core::Vertex3> axisPickMarkers;
        std::vector<Rev::Core::Vertex3> probeMarkers;
        std::vector<Rev::Core::Vertex3> probePlan;

        static constexpr size_t NoAxisPickHover = static_cast<size_t>(-1);
        size_t axisPickHoveredCandidate = NoAxisPickHover;

        static constexpr size_t NoHoveredFace = static_cast<size_t>(-1);
        size_t hoveredFaceId = NoHoveredFace;

        bool setHoveredFace(size_t faceId) {
            if (hoveredFaceId == faceId) { return false; }
            hoveredFaceId = faceId;
            return true;
        }

        Cam::Gui::ToolPath toolPath;

        bool attached = false;

        // View settings
        //--------------------------------------------------

        bool showPart = false;   // prior model
        bool showModel = false;  // resulting model
        bool showDelta = false;
        bool showPick = false;
        bool showToolPath = false;

        bool selectable = false;
        bool includeInFit = false;

        // Create / destroy
        //--------------------------------------------------

        Stage() {}

        Stage(
            Rev::Graphics::Canvas* canvas
        ) {
            create(canvas);
        }

        ~Stage() {
            destroy();
        }

        void create(
            Rev::Graphics::Canvas* canvas
        ) {
            this->canvas = canvas;

            createPartActor();
            createModelActor();
            createDeltaActor();
            createPickActor();
            createAxisPickMarkerActor();
            createProbeMarkerActor();
            createProbePlanActor();

            toolPath.create(canvas);
        }

        void destroy() {

            detach();

            delete partActor;
            delete modelActor;
            delete deltaActor;
            delete pickActor;
            delete axisPickMarkerActor;
            delete probeMarkerActor;
            delete probePlanActor;

            toolPath.destroy();

            partActor = nullptr;
            modelActor = nullptr;
            deltaActor = nullptr;
            pickActor = nullptr;
            axisPickMarkerActor = nullptr;
            probeMarkerActor = nullptr;
            probePlanActor = nullptr;

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
            view->addActor(modelActor);
            view->addActor(toolPath.actor);
            view->addActor(deltaActor);
            view->addActor(axisPickMarkerActor);
            view->addActor(probeMarkerActor);
            view->addActor(probePlanActor);
            view->addActor(pickActor);

            attached = true;
        }

        void detach() {

            if (!view || !attached) { return; }

            if (partActor) { view->removeActor(partActor); }
            if (modelActor) { view->removeActor(modelActor); }
            if (deltaActor) { view->removeActor(deltaActor); }
            if (axisPickMarkerActor) { view->removeActor(axisPickMarkerActor); }
            if (probeMarkerActor) { view->removeActor(probeMarkerActor); }
            if (probePlanActor) { view->removeActor(probePlanActor); }
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

        void createModelActor() {

            modelActor = new View3d::Actor();

            modelActor->visible = false;
            modelActor->selectable = false;
            modelActor->ownsMesh = true;
            modelActor->ownsTriangles = false;
            modelActor->includeInFit = false;

            modelActor->mesh = new Rev::Primitives::Mesh3d(canvas, {});

            // A calm green so the resulting model reads distinctly from the grey
            // prior model and the red delta when shown together.
            modelActor->mesh->color = {
                0.56f,
                0.80f,
                0.62f,
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

        void createProbeMarkerActor() {

            probeMarkerActor = new View3d::Actor();

            probeMarkerActor->visible = false;
            probeMarkerActor->selectable = false;
            probeMarkerActor->ownsLines = true;
            probeMarkerActor->includeInFit = false;

            probeMarkerActor->lines = new Rev::Primitives::Lines3d(canvas, {
                .lines = &probeMarkers
            });

            probeMarkerActor->lines->color = {
                0.62f,
                0.40f,
                0.85f,
                1.0f
            };
        }

        void createProbePlanActor() {

            probePlanActor = new View3d::Actor();

            probePlanActor->visible = false;
            probePlanActor->selectable = false;
            probePlanActor->ownsLines = true;
            probePlanActor->includeInFit = false;

            probePlanActor->lines = new Rev::Primitives::Lines3d(canvas, {
                .lines = &probePlan
            });

            // Per-vertex colour carries the rapid/plunge distinction; this is a
            // neutral fallback.
            probePlanActor->lines->color = {
                0.62f,
                0.40f,
                0.85f,
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

        float axisPickMarkerSizeForModel(Cam::App::Model* model) const {

            if (!model || !model->hasAxisPickFace()) {
                return 1.0f;
            }

            return axisPickMarkerSize(model->axisPickCandidates);
        }

        static float rayPointDistance(
            const View3d::Ray& ray,
            const Rev::Core::Pos3& point
        ) {
            Rev::Core::Pos3 along = point - ray.origin;

            const float t = along.dot(ray.direction);

            if (t < 0.0f) {
                return std::numeric_limits<float>::max();
            }

            Rev::Core::Pos3 closest = ray.origin + ray.direction * t;

            return (point - closest).pythag();
        }

        bool hitTestDisplayedPickPoint(
            const View3d::Ray& ray,
            Cam::App::Model* model,
            Rev::Core::Pos3& outPoint,
            size_t* outCandidateIndex = nullptr
        ) const {

            outPoint = {};

            if (outCandidateIndex) {
                *outCandidateIndex = NoAxisPickHover;
            }

            if (!model) {
                return false;
            }

            const bool hasCandidates = !model->axisPickCandidates.empty();
            const bool hasSelected = !model->axisPickSelectedPoints.empty();

            if (!hasCandidates && !hasSelected) {
                return false;
            }

            std::vector<Rev::Core::Pos3> sizingPoints;

            if (hasCandidates) {
                sizingPoints = model->axisPickCandidates;
            }

            for (const Rev::Core::Pos3& point : model->axisPickSelectedPoints) {
                sizingPoints.push_back(point);
            }

            const float markerSize = axisPickMarkerSize(sizingPoints);
            const float pickRadius = markerSize * 1.35f;

            float bestDistance = pickRadius;
            bool found = false;
            size_t bestCandidateIndex = NoAxisPickHover;

            for (size_t i = 0; i < model->axisPickCandidates.size(); i++) {

                const float distance = rayPointDistance(
                    ray,
                    model->axisPickCandidates[i]
                );

                if (distance <= bestDistance) {
                    bestDistance = distance;
                    outPoint = model->axisPickCandidates[i];
                    bestCandidateIndex = i;
                    found = true;
                }
            }

            for (const Rev::Core::Pos3& point : model->axisPickSelectedPoints) {

                const float distance = rayPointDistance(ray, point);

                if (distance > bestDistance) {
                    continue;
                }

                bestDistance = distance;
                outPoint = point;
                bestCandidateIndex = NoAxisPickHover;

                for (size_t i = 0; i < model->axisPickCandidates.size(); i++) {

                    if ((model->axisPickCandidates[i] - point).pythag() <= 1e-3f) {
                        bestCandidateIndex = i;
                        break;
                    }
                }

                found = true;
            }

            if (found && outCandidateIndex) {
                *outCandidateIndex = bestCandidateIndex;
            }

            return found;
        }

        bool hitTestAxisPickCandidate(
            const View3d::Ray& ray,
            Cam::App::Model* model,
            size_t& outIndex
        ) const {

            Rev::Core::Pos3 hitPoint;

            return hitTestDisplayedPickPoint(ray, model, hitPoint, &outIndex);
        }

        void setAxisPickHoveredCandidate(size_t index) {

            if (axisPickHoveredCandidate == index) {
                return;
            }

            axisPickHoveredCandidate = index;

            if (axisPickMarkerActor && axisPickMarkerActor->lines) {
                axisPickMarkerActor->lines->dirty = true;
            }
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

        // Part mesh: parent stock after any modification; own model only while selecting faces on unmodified geometry.
        Cam::App::Model* partModel() {

            if (state && state->hasDelta) {
                return displayModel();
            }

            if (showPick || selectable) {
                return selectionModel();
            }

            return displayModel();
        }

        // Settings
        //--------------------------------------------------

        void hideAll() {

            showPart = false;
            showModel = false;
            showDelta = false;
            showPick = false;
            showToolPath = false;

            selectable = false;
            includeInFit = false;
            hoveredFaceId = NoHoveredFace;
        }

        // Enables face picking on this view. The pick actor is invisible and
        // works independently of which model layers are shown, so this does not
        // force any model visible — that stays driven by the visibility requests.
        void enablePicking() {

            showPick = true;
            selectable = true;
        }

        // Sync
        //--------------------------------------------------

        void setState(
            Cam::App::Stage* state
        ) {
            this->state = state;
        }

        void sync(double toolPathPreviewProgress = 1.0) {

            syncPart();
            syncModel();
            syncDelta();
            syncPick();
            // The combined progress covers the probe operation THEN the cut, so
            // the cut lines only begin colouring once the probe window is past.
            syncToolPath(cutProgressFromCombined(toolPathPreviewProgress));
            syncAxisPickMarkers();
            syncProbeMarkers();
            syncProbePlan();
        }

        // Split the stage's combined (probe + cut) scrub progress into the cut's
        // own 0..1 progress: 0 throughout the probe window, ramping over the cut.
        double cutProgressFromCombined(double progress) const {
            if (!state) { return progress; }
            const double probeDur = state->probePreviewPath.durationSeconds();
            const double cutDur   = state->toolPath.durationSeconds();
            if (cutDur   <= 1e-9) { return 1.0; }
            if (probeDur <= 1e-9) { return progress; }
            const double elapsed = progress * (probeDur + cutDur);
            return std::clamp((elapsed - probeDur) / cutDur, 0.0, 1.0);
        }

        // Draw the machine's PROBE PLAN: the actual motion it will make to probe
        // each target, assuming the part is exactly where we currently believe it
        // is (the stored probe correction, identity when un-probed, is applied to
        // both the standoff and the expected contact).  Per target: a rapid leg
        // (from the previous target's expected contact to this standoff) and a
        // plunge leg (from the standoff down the approach normal to the expected
        // contact point).  The plunge STOPS at the expected contact -- the
        // overtravel beyond it is the strictly-automatic "until it touches"
        // continuation whose true endpoint isn't known at preview time, so it is
        // intentionally not drawn.  Rapid legs are faint blue; plunge legs violet.
        void syncProbePlan() {

            if (!probePlanActor || !probePlanActor->lines) { return; }

            probePlan.clear();

            const bool show =
                state &&
                state->visible.probe &&
                state->probe.enabled &&
                !state->probe.targets.empty();

            if (!show) {
                probePlanActor->visible = false;
                probePlanActor->lines->dirty = true;
                return;
            }

            // The probe moves to fixed NOMINAL positions (it is never driven by
            // the correction), so the plan is drawn at the nominal geometry.  The
            // part's measured orientation is shown by the part pose itself (the
            // world transform), which the markers ride along with.
            const Rev::Core::Color rapidColor  = { 0.45f, 0.60f, 0.92f, 0.55f };
            const Rev::Core::Color plungeColor = { 0.66f, 0.42f, 0.92f, 0.95f };
            // Prospective overtravel: the region the probe will keep driving
            // through IF it hasn't touched yet, up to the "no contact" fail point.
            const Rev::Core::Color prospectColor = { 0.55f, 0.55f, 0.58f, 0.45f };

            bool            havePrev = false;
            Rev::Core::Pos3 prevStandoff{};

            for (const Cam::App::ProbeTarget& t : state->probe.targets) {

                const float nlen = t.normal.pythag();
                if (nlen < 1e-4f) { continue; }
                const Rev::Core::Pos3 n = t.normal * (1.0f / nlen);

                const Rev::Core::Pos3 standoff = t.point + n * static_cast<float>(t.standoff);
                const Rev::Core::Pos3 contact  = t.point;
                // The "no contact" fail point: how far past the expected contact
                // the probe will keep driving before giving up (overtravel).
                const Rev::Core::Pos3 failAt = t.point - n * static_cast<float>(t.overtravel);

                // Rapid leg: from the PREVIOUS standoff to this standoff -- a
                // lateral move at standoff height, because the machine retracts
                // to the standoff after each plunge (it never slides across the
                // surface).  The retract itself retraces the plunge line.
                if (havePrev) {
                    probePlan.push_back({ prevStandoff.x, prevStandoff.y, prevStandoff.z, rapidColor });
                    probePlan.push_back({ standoff.x,     standoff.y,     standoff.z,     rapidColor });
                }

                // Plunge leg: standoff -> expected contact (where we believe the
                // surface is, so the machine stops here on a normal probe).
                probePlan.push_back({ standoff.x, standoff.y, standoff.z, plungeColor });
                probePlan.push_back({ contact.x,  contact.y,  contact.z,  plungeColor });

                // Prospective overtravel: expected contact -> fail point, grey.
                // The probe only travels here if it hasn't touched yet; reaching
                // the end is the "no contact" failure.
                probePlan.push_back({ contact.x, contact.y, contact.z, prospectColor });
                probePlan.push_back({ failAt.x,  failAt.y,  failAt.z,  prospectColor });

                prevStandoff = standoff;
                havePrev = true;
            }

            probePlanActor->visible = !probePlan.empty();
            probePlanActor->lines->dirty = true;
        }

        // Draw the probe targets as violet crosses, each with a short stick along
        // its outward surface normal (the approach direction).  A measured point,
        // once a probing run has filled it, is drawn as a green cross.
        void syncProbeMarkers() {

            if (!probeMarkerActor || !probeMarkerActor->lines) { return; }

            probeMarkers.clear();

            const bool show =
                state &&
                state->visible.probe &&
                state->probe.enabled &&
                !state->probe.targets.empty();

            if (!show) {
                probeMarkerActor->visible = false;
                probeMarkerActor->lines->dirty = true;
                return;
            }

            std::vector<Rev::Core::Pos3> pts;
            for (const Cam::App::ProbeTarget& t : state->probe.targets) { pts.push_back(t.point); }

            const float size = std::max(axisPickMarkerSize(pts), 1.5f);

            const Rev::Core::Color pointColor   = { 0.66f, 0.42f, 0.92f, 1.0f };
            const Rev::Core::Color normalColor  = { 0.80f, 0.58f, 1.0f, 0.9f };
            const Rev::Core::Color measuredColor= { 0.35f, 0.95f, 0.55f, 1.0f };

            for (const Cam::App::ProbeTarget& t : state->probe.targets) {

                appendMarkerCross(probeMarkers, t.point, size, pointColor);

                // Approach stick: from the surface point outward along +normal.
                const Rev::Core::Pos3 tip = t.point + t.normal * (size * 3.0f);
                probeMarkers.push_back({ t.point.x, t.point.y, t.point.z, normalColor });
                probeMarkers.push_back({ tip.x, tip.y, tip.z, normalColor });

                if (t.measured) {
                    appendMarkerCross(probeMarkers, t.measuredPoint, size * 1.1f, measuredColor);
                }
            }

            probeMarkerActor->visible = true;
            probeMarkerActor->lines->dirty = true;
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
            partActor->mesh->bvhBuilt   = false;  // geometry changed — invalidate accel
            partActor->mesh->dirty      = true;
            partActor->visible = true;
            partActor->selectable = false;
            partActor->includeInFit = includeInFit;

            applyFaceColors();
        }

        // The stage's own resulting model — a distinct, separately-toggleable
        // layer from the prior model shown by partActor.
        void syncModel() {

            if (!modelActor || !modelActor->mesh) { return; }

            Cam::App::Model* model = selectionModel();

            if (!state || !model || !showModel) {
                modelActor->visible = false;
                modelActor->selectable = false;
                modelActor->includeInFit = false;
                return;
            }

            modelActor->mesh->pTriangles = &model->render.triangles;
            modelActor->mesh->bvhBuilt   = false;
            modelActor->mesh->dirty      = true;
            modelActor->visible = true;
            modelActor->selectable = false;
            modelActor->includeInFit = includeInFit;
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
            deltaActor->mesh->bvhBuilt   = false;  // geometry changed — invalidate accel
            deltaActor->mesh->dirty      = true;
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

            const bool hasCandidates =
                model &&
                model->hasAxisPickFace() &&
                !model->axisPickCandidates.empty();

            const bool hasSelected =
                model && !model->axisPickSelectedPoints.empty();

            if (
                !state ||
                !model ||
                !showPick ||
                (!hasCandidates && !hasSelected)
            ) {
                axisPickHoveredCandidate = NoAxisPickHover;
                axisPickMarkerActor->visible = false;
                axisPickMarkerActor->lines->dirty = true;
                return;
            }

            std::vector<Rev::Core::Pos3> sizingPoints;

            if (hasCandidates) {
                sizingPoints = model->axisPickCandidates;
            }

            for (const Rev::Core::Pos3& point : model->axisPickSelectedPoints) {
                sizingPoints.push_back(point);
            }

            const float markerSize = axisPickMarkerSize(sizingPoints);

            const Rev::Core::Color candidateColor = {
                1.0f,
                0.55f,
                0.12f,
                0.45f
            };

            const Rev::Core::Color candidateHoverColor = {
                1.0f,
                0.70f,
                0.20f,
                0.85f
            };

            const Rev::Core::Color selectedColor = {
                0.35f,
                0.95f,
                0.55f,
                1.0f
            };

            if (hasCandidates) {

                for (size_t i = 0; i < model->axisPickCandidates.size(); i++) {

                    if (model->isAxisPickCandidateSelected(i)) {
                        continue;
                    }

                    const bool hovered = (i == axisPickHoveredCandidate);

                    appendMarkerCross(
                        axisPickMarkers,
                        model->axisPickCandidates[i],
                        hovered ? markerSize * 1.12f : markerSize,
                        hovered ? candidateHoverColor : candidateColor
                    );
                }
            }
            else {
                axisPickHoveredCandidate = NoAxisPickHover;
            }

            for (const Rev::Core::Pos3& point : model->axisPickSelectedPoints) {
                appendMarkerCross(
                    axisPickMarkers,
                    point,
                    markerSize * 1.2f,
                    selectedColor
                );
            }

            axisPickMarkerActor->visible = true;
            axisPickMarkerActor->lines->dirty = true;
        }

        // Selection display
        //--------------------------------------------------

        bool isHighlightedOperationFace(size_t faceId) const {

            if (!state) { return false; }

            for (std::size_t id : state->highlightedOperationFaces) {
                if (id == faceId) { return true; }
            }

            return false;
        }

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

            // 40 % blend from the base part colour (0.75, 0.75, 0.82) toward
            // warm orange (1.0, 0.55, 0.1) — visibly distinct but not jarring.
            Rev::Core::Color hover = {
                0.85f,
                0.67f,
                0.53f,
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

            // Light pink for faces referenced by a hovered operation.
            Rev::Core::Color referenced = {
                1.0f,
                0.72f,
                0.80f,
                1.0f
            };

            const bool highlightActive =
                state && !state->highlightedOperationFaces.empty();

            // Hover only makes sense when partActor and pickActor share the same
            // geometry (no delta yet); after a defeature they diverge and face IDs
            // from the pick actor no longer correspond to the displayed model.
            const bool canShowHover = (
                state &&
                !state->hasDelta &&
                hoveredFaceId != NoHoveredFace
            );

            size_t sliceFaceId = Cam::App::ToolPath::NoSliceFaceId;

            if (state && state->toolPath.hasSliceFace()) {
                sliceFaceId = state->toolPath.sliceFaceId;
            }

            size_t triangleCount = triangles.size() / 3;

            for (size_t tri = 0; tri < triangleCount; tri++) {

                if (tri >= triangleFaceIds.size()) { continue; }

                size_t faceId = triangleFaceIds[tri];

                Rev::Core::Color color = base;

                if (highlightActive && isHighlightedOperationFace(faceId)) {
                    color = referenced;
                }
                else if (faceId == sliceFaceId) {
                    color = slicePlane;
                }
                else if (model->isAxisPickFace(faceId)) {
                    color = axisPick;
                }
                else if (model->isFaceSelected(faceId)) {
                    color = selected;
                }
                else if (canShowHover && faceId == hoveredFaceId) {
                    color = hover;
                }

                triangles[tri * 3 + 0].color = color;
                triangles[tri * 3 + 1].color = color;
                triangles[tri * 3 + 2].color = color;
            }

            partActor->mesh->dirty = true;
        }
    };
}