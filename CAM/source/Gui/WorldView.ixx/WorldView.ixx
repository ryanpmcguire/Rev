module;

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <string>
#include <format>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <cstdio>

#include <dbg.hpp>

export module Cam.Gui.WorldView;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Event;
import Rev.Element.Event.GestureTracker;

import Rev.Element.Box;
import Rev.Element.Text;

import Rev.Core.Pos;
import Rev.Core.Pos3;
import Rev.Core.Color;
import Rev.Core.Vertex3;
import Rev.Core.Animator;

import Rev.Primitive.Lines3d;
import Rev.Primitive.Mesh3d;

import Rev.Element.View3d;
import Rev.Element.View3d.Actor3d;

import Cam.CoordinateSystem;

import Cam.App;
import Cam.App.Project;
import Cam.App.Model;
import Cam.App.Stage;
import Cam.App.Probe;
import Cam.App.Tool;
import Cam.App.ToolLibrary;
import Cam.App.ToolPath;
import Cam.App.MachineProfile;

import Cam.Gui.World.Stage;
import Cam.Gui.ToolPath;
import Cam.Gui.Theme;
import Cam.Gui.PreviewBar;
import Cam.Gui.ToolPathPreview;

import Cam.Machine.Pose;
import Cam.Machine.Definition;
import Cam.Machine.ToolPath;
import Cam.Machine.IKSolver;

import CarveraAir;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace View3d = Rev::Element::View3d;

    enum class WorldViewCommand {
        Defeature,
        ExtendFeature,
        ExtrudeFeature,
        ThreadMill,
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
        Cam::App::Stage* representedDisplayedState = nullptr;
        Cam::App::Stage* representedWorkingState = nullptr;
        size_t representedStateCount = 0;

        std::vector<Cam::Gui::World::Stage*> materialViews;

        // The WORK frame: the rotary-axis / work coordinate system as the probe
        // established it (rides workCorrection alone).  Fixed in machine space --
        // it does NOT rotate with the chuck; the part rotates ABOUT it.  Identity
        // (nominal) until the first probe establishes the work frame.
        View3d::Actor* lineActor = nullptr;
        std::vector<Rev::Core::Vertex3> testLines;

        // The PART frame: the stock coordinate system (co/ax) as it is ACTUALLY
        // oriented -- it rides the full part pose (chuck rotation o W o P), so it
        // rotates with the chuck and carries the part's offset within the work
        // frame.  On the first probe pass the part offset is identity, so this
        // COINCIDES with the work frame; only a measured part offset makes them
        // diverge -- and that gap IS the part-in-work offset.
        View3d::Actor* partFrameActor = nullptr;
        std::vector<Rev::Core::Vertex3> partFrameLines;

        // The MEASURED rotary axis: the physical line the chuck turns the part
        // about, as located by multi-orientation probing (workRotaryAxis).  Drawn
        // STATIC (it is fixed in machine space; the part rotates about it), and
        // only when it has actually been measured.
        View3d::Actor* axisLineActor = nullptr;
        std::vector<Rev::Core::Vertex3> axisLineVerts;

        View3d::Actor* toolPreviewActor = nullptr;
        std::vector<Rev::Core::Vertex3> toolPreviewTriangles;

        View3d::Actor* spindlePreviewActor = nullptr;
        std::vector<Rev::Core::Vertex3> spindlePreviewTriangles;

        // The tool whose cached mesh the preview actor currently points at;
        // used to re-upload to the GPU only when the tool/geometry changes.
        Cam::App::Tool* previewTool = nullptr;
        std::size_t previewToolRevision = 0;

        std::size_t previewSpindleMeshRevision = 0;

        // While in EXECUTE mode, poll machine telemetry so the tool tracks the
        // real machine (incl. jogs), not the computed preview.
        Rev::Core::Animator executePoll { 30 };

        bool partInView = false;
        bool representationDirty = true;
        bool clearMaterialViewsRequested = false;

        // Machine simulation
        PreviewMode previewMode = PreviewMode::AbsoluteToolPath;

        // Execute-mode progress tracking — used to enforce monotonic forward
        // advancement through the toolpath so telemetry noise never causes the
        // scrubber/preview to jump backwards.  Reset when a new program starts.
        Cam::App::Stage* executeTrackedState    = nullptr;
        double                   executeTrackedProgress = 0.0;

        // Per-state IK cache.  Keyed by MaterialState* so each state's
        // solved path lives independently — different setups have different
        // tool directions and require independent IK solves.
        std::map<Cam::App::Stage*, Cam::Machine::MachineToolPath> machineToolPaths;
        bool machineToolPathsDirty = true;

        // --- Probe result collection -------------------------------------
        // A probe op streams its G38.2 moves in target order, one at a time, so
        // the k-th [PRB:...] reply matches the k-th entry recorded here at build
        // time.  As replies arrive we back-transform the machine contact into
        // the CAD frame; once all of a stage's entries are filled we fit a
        // ProbeResult and store it on that stage.  Everywhere else the stored
        // result is applied unconditionally (identity by default), so an
        // un-probed stage is simply an uncorrected one — no branch.
        struct ProbeSessionEntry {
            Cam::App::Stage* stage = nullptr;
            Rev::Core::Pos3  nominalCad{};   // RAW nominal target.point (CAD frame)
            Rev::Core::Pos3  normalCad{};    // RAW unit outward normal (CAD frame)
            double           commandedAngleDeg = 0.0;  // rotary A at the contact
            bool             filled = false;
            Rev::Core::Pos3  measuredCad{};      // contact, back-transformed to CAD
            Rev::Core::Pos3  machineContact{};   // raw [PRB] contact (machine coords)
            bool             triggered = false;
        };
        std::vector<ProbeSessionEntry> probeSession_;
        size_t probeFillIndex_ = 0;

        // HARD-CODED CALIBRATION + CONFIRM scheme (to be option-coded later as a
        // dedicated initial operation).  Each probe stage runs FOUR passes:
        //   pass 0: nearly flat (A = 0)          -- calibration
        //   pass 1: deliberately rotated +10 deg -- calibration
        //   pass 2: deliberately rotated -10 deg -- calibration
        //   pass 3: driven by the fitted work frame (physically squared) -- CONFIRM
        // The rotary axis is now established up front by MACHINE CALIBRATION (and
        // the stock by Measure Stock), so a stage probe NO LONGER infers the axis.
        // It runs a single pass that measures the part's offset WITHIN the known
        // work frame (probeCorrection) -- the only residual a stage probe can still
        // resolve.  (Was: flat + 4 deliberate tilts to locate the axis + a confirm.)
        static constexpr int    kProbeRepeatCount    = 1;     // single part-offset pass

        // Trust thresholds gating whether a re-probe (pass > 0) is DRIVEN by the
        // prior pass's correction.  RMS is the REAL trust signal: a tight fit is
        // trustworthy no matter how large the measured misalignment, and a large
        // misalignment is exactly what we WANT to physically correct before the
        // confirmation probe.  The tilt cap is therefore only a generous SANITY /
        // anti-collision bound -- it rejects numerically degenerate fits and
        // refuses to auto-swing the probe by an absurd amount in one shot, but it
        // must NOT block a confident, ordinary mis-mount (e.g. 20 deg).

        // Trust thresholds gating whether a re-probe (pass > 0) is DRIVEN by the
        // prior pass's correction.  RMS is the REAL trust signal: a tight fit is
        // trustworthy no matter how large the measured misalignment, and a large
        // misalignment is exactly what we WANT to physically correct before the
        // confirmation probe.  The tilt cap is therefore only a generous SANITY /
        // anti-collision bound -- it rejects numerically degenerate fits and
        // refuses to auto-swing the probe by an absurd amount in one shot, but it
        // must NOT block a confident, ordinary mis-mount (e.g. 20 deg).
        static constexpr double kProbeReprobeMaxRmsMm   = 0.5;
        static constexpr double kProbeReprobeMaxTiltDeg = 45.0;

        // Frame captured at the build that produced the current probeSession_,
        // used to map a machine-WCS contact back into the CAD frame.  Stored as
        // components (not a UserFrame) because UserFrame is declared later in
        // this class; toWorld(f) = origin + X*f.x + Y*f.y + Z*f.z.
        Rev::Core::Pos3 probeFrameOrigin_{};
        Rev::Core::Pos3 probeFrameX_{ 1, 0, 0 };
        Rev::Core::Pos3 probeFrameY_{ 0, 1, 0 };
        Rev::Core::Pos3 probeFrameZ_{ 0, 0, 1 };
        Rev::Core::Pos3 probeBeginWorkInFrame_{};

        // When the current probe session is a DRIVEN re-probe (pass > 0 with a
        // trusted prior fit), the correction it was driven by is stashed here and
        // probeSessionComposed_ is set: the session's nominal/normal entries are
        // already base-corrected, so handleProbeContact fits the RESIDUAL and
        // COMPOSES it onto this base instead of replacing.  Cleared (identity,
        // false) for a clean absolute pass, where the fit REPLACES.
        Cam::App::ProbeResult probeSessionBase_;
        bool                  probeSessionComposed_ = false;

        // Which phase of the calibrate+confirm scheme this probe op is:
        //   confirm  = false -> a CALIBRATION pass (flat / +10 / -10); contacts
        //                       feed the work frame + axis fit.
        //   confirm  = true  -> the CONFIRM pass (driven by the fitted work frame);
        //                       contacts feed the part offset.
        //   calibComplete    -> this is the LAST calibration pass, so once its
        //                       contacts are in we have all 3 orientations and
        //                       fit the work frame + locate the axis.
        bool probeSessionConfirm_      = false;
        bool probeSessionCalibComplete_ = false;

        // Liveness guard for the dispatcher-registered onProbe listener.  The
        // Dispatcher has no removal, and this view IS destructible, so the
        // captured lambda checks this flag (a weak_ptr) before touching `this`.
        std::shared_ptr<bool> probeAlive_ = std::make_shared<bool>(true);

        // "Set Top": when armed, the NEXT probe contact is interpreted as a one-shot
        // top touch-off -- it sets the part's Z offset within the work frame directly
        // (not a stage probe session).  Cleared once consumed.
        bool setTopPending_ = false;

        std::function<void(Event&)> onStateChanged;

        GestureTracker<WorldViewCommand> gestures = {
            { "df", WorldViewCommand::Defeature },
            { "ef", WorldViewCommand::ExtendFeature },
            { "exf", WorldViewCommand::ExtrudeFeature },
            { "tm", WorldViewCommand::ThreadMill },
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
            createPartFrameActor();
            createRotaryAxisActor();
            createToolPreviewActor();
            createSpindlePreviewActor();
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
            // 30 Hz execute tick — drives the full state update:
            //   timeline sync → syncAllMaterialViews → applyWorldTransforms
            // This is the authoritative path for material-removal display,
            // part rotation, and scrubber position while in Execute mode.
            executePoll.onFrame([this](Rev::Core::AnimationEvent&) {
                if (previewMode != PreviewMode::Execute) { return; }
                if (!Carvera::MachineLink::instance().connected()) { return; }
                if (!shared || !shared->event) { return; }

                if (Carvera::MachineLink::instance().isExecuting()) {
                    syncTimelineToMachinePosition(*shared->event);
                }

                // syncAllMaterialViews handles: per-state material sync,
                // syncSharedToolPreview, applyWorldTransforms, and the
                // scrubber / time display — everything in one coherent pass.
                syncAllMaterialViews();
                if (view3d) { view3d->refresh(*shared->event); }
            });

            // Push-based position updates: the machine link broadcasts every
            // telemetry frame, so the tool tracks the real machine immediately
            // (e.g. while jogging) without waiting on the poll.
            // 140 Hz position update — keeps tool position AND part rotation
            // smooth between the 30 Hz poll ticks.  Does NOT touch the timeline
            // or scrubber (that is the poll's job) to avoid scrubber flicker.
            Carvera::MachineLink::instance().onTelemetry =
                [this](float, float, float, float) {
                    if (previewMode != PreviewMode::Execute) { return; }
                    if (!shared || !shared->event) { return; }
                    syncSharedToolPreview(activeProject());
                    applyWorldTransforms();
                    if (view3d) { view3d->refresh(*shared->event); }
                };

            // Probe contacts flow back here: Air streams the G38.2 moves and
            // emits a ProbeEvent (machine-WCS contact) per touch.  CAM owns the
            // frame math, so it is CAM — not Air — that turns those contacts
            // into a fitted ProbeResult.  Guarded by probeAlive_ since the
            // dispatcher never unregisters and this view can be destroyed.
            {
                std::weak_ptr<bool> alive = probeAlive_;
                Carvera::MachineLink::instance().onProbe(
                    [this, alive](Carvera::MachineLink::ProbeEvent& e) {
                        if (alive.expired()) { return; }
                        handleProbeContact(e);
                    });
            }

            // Interface START button → Air asks US for the program, ONE
            // operation at a time, by index.  Air streams op N fully, then asks
            // for op N+1 -- so each operation is built JUST IN TIME with the
            // latest world model.  A probe that just ran has updated the part
            // pose correction, so the next operation (the second probe, or the
            // cut) is built already corrected: the part rotates right after the
            // probe, as a consequence of re-resolving, not a special case.
            // Returning nullopt means "no operation at this index" -- i.e. the
            // program is complete (or nothing to run at index 0).
            Carvera::MachineLink::instance().operationProvider =
                [this](size_t index) -> std::optional<Carvera::MachineLink::Operation> {

                if (previewMode != PreviewMode::Execute) { return std::nullopt; }

                if (index == 0) {
                    // Reset monotonic-progress tracker at the start of a run.
                    executeTrackedState    = nullptr;
                    executeTrackedProgress = 0.0;

                    // NOTE: the probe correction (and the measurement set behind
                    // it) is deliberately NOT cleared here.  It is a part property
                    // that persists across runs and steps; it is cleared only when
                    // the operator re-locates the part with "Set Origin" (see
                    // onWorkOriginSet).  Each probe ADDS its contacts and the pose
                    // is re-solved over the whole set, so re-running refines rather
                    // than double-corrects.

                    rebuildPreviewTimelineIfNeeded();
                    if (previewTimeline.atEnd()) {
                        previewTimeline.setElapsed(0.0);
                        if (previewBar) { previewBar->percent = 0.0f; }
                    }
                }

                std::vector<Carvera::MachineLink::Operation> v =
                    buildExecuteOperations(static_cast<long>(index));
                if (v.empty()) { return std::nullopt; }
                return std::move(v.front());
            };

            // "Set Origin" re-references the part, so the persistent probe
            // correction is now stale -- clear it (and re-solve / rebuild so the
            // view and any subsequent cut go back to nominal until the next probe).
            Carvera::MachineLink::instance().onWorkOriginSet = [this]() {
                if (Cam::App::Project* project = activeProject()) {
                    // Re-locating the part invalidates every prior contact: clear
                    // both measurement sets, the work-frame + part corrections, and
                    // any measured rotary-axis inference.
                    project->workMeasurements.clear();
                    project->partMeasurements.clear();
                    project->workCorrection.reset();
                    project->probeCorrection.reset();
                    // STRATEGY B: the scene's work frame is anchored at the user
                    // frame's axis origin (= the rotary axis); the part pivots about
                    // it directly (buildWorkFrame's f.origin fallback).  The calibrated
                    // axis lives on the machine definition and is used at Set Origin to
                    // pin the machine-space Y/Z -- it does NOT need to ride in
                    // workRotaryAxis, so clear any stale per-session inference here.
                    project->workRotaryAxis.reset();
                    project->dirty = true;
                }
                machineToolPathsDirty = true;
                previewTimelineDirty  = true;

                // Persist the freshly captured work origin so reopening the app (with
                // the machine still on) restores the part/tool anchor -- no manual
                // re-Set-Origin needed.  The controller keeps its WCS zero; this keeps
                // the HOST's matching part-origin location.
                if (app) {
                    float mx, my, mz, ma;
                    if (Carvera::MachineLink::instance().machineOrigin(mx, my, mz, ma)) {
                        app->machine.workOrigin = { true, mx, my, mz, ma };
                        app->saveSession();
                    }
                }
            };

            // "Set Top": probe the part's top, then set the part Z from the contact.
            Carvera::MachineLink::instance().onSetTop = [this]() { armSetTop(); };

            // Restore the persisted work origin (machine still on across a restart):
            // the controller kept its WCS zero, but the host forgot where the part
            // origin is -- without this the part/tool float until a manual Set Origin.
            if (app && app->machine.workOrigin.valid) {
                const auto& w = app->machine.workOrigin;
                Carvera::MachineLink::instance().captureMachineOrigin(
                    static_cast<float>(w.mx), static_cast<float>(w.my),
                    static_cast<float>(w.mz), static_cast<float>(w.ma));
            }

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

                    case WorldViewCommand::ExtrudeFeature: {
                        extrudeFeature(e);
                        break;
                    }

                    case WorldViewCommand::ThreadMill: {
                        threadMillFeature(e);
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
            Carvera::MachineLink::instance().onTelemetry       = nullptr;
            Carvera::MachineLink::instance().operationProvider = nullptr;
            Carvera::MachineLink::instance().onWorkOriginSet   = nullptr;
            Carvera::MachineLink::instance().onSetTop          = nullptr;

            clearMaterialViews();

            if (view3d && lineActor) {
                view3d->removeActor(lineActor);
            }

            delete lineActor;
            lineActor = nullptr;

            if (view3d && partFrameActor) {
                view3d->removeActor(partFrameActor);
            }

            delete partFrameActor;
            partFrameActor = nullptr;

            if (view3d && axisLineActor) {
                view3d->removeActor(axisLineActor);
            }

            delete axisLineActor;
            axisLineActor = nullptr;

            if (view3d && toolPreviewActor) {
                view3d->removeActor(toolPreviewActor);
            }

            if (view3d && spindlePreviewActor) {
                view3d->removeActor(spindlePreviewActor);
            }

            delete toolPreviewActor;
            toolPreviewActor = nullptr;
            toolPreviewTriangles.clear();

            delete spindlePreviewActor;
            spindlePreviewActor = nullptr;
            spindlePreviewTriangles.clear();
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

                // EXECUTE: pressing play asks Air to start -- the SAME single
                // pipeline as the interface START button (preflight -> ask the
                // provider -> validate -> stream).  Play itself never streams;
                // there is exactly ONE way a program reaches the machine.
                if (previewMode == PreviewMode::Execute) {
                    Carvera::MachineLink::instance().requestStart();
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

        // Build a stage's probe operation as a timed, CAD-space ToolPath so the
        // scrub timeline can animate it.  Per target: rapid to the standoff, slow
        // plunge to the EXPECTED contact (correction applied -- where we believe
        // the surface is), then rapid retract back to the standoff.  Empty when
        // the stage has no probe.  finalizePoints() stamps the times (the slow
        // plunge takes real scrub time at the probe feed; rapids are fast).
        void rebuildProbePreviewPath(Cam::App::Stage* state) {

            if (!state) { return; }

            state->probePreviewPath.clear();

            if (!state->probe.enabled || state->probe.targets.empty()) { return; }

            // The probe always moves to fixed NOMINAL positions (never driven by
            // the correction), so the preview shows exactly that.
            state->probePreviewPath.feedRate = 100.0;   // probe plunge feed (mm/min)

            auto add = [&](const Rev::Core::Pos3& pos, const Rev::Core::Pos3& dir,
                           bool rapid, bool cutting) {
                state->probePreviewPath.addWorldPoint(pos, rapid, cutting);
                state->probePreviewPath.points.back().toolDirection = dir;
            };

            for (const Cam::App::ProbeTarget& t : state->probe.targets) {

                const float nlen = t.normal.pythag();
                if (nlen < 1e-4f) { continue; }
                const Rev::Core::Pos3 n = t.normal * (1.0f / nlen);

                const Rev::Core::Pos3 standoff = t.point + n * static_cast<float>(t.standoff);
                const Rev::Core::Pos3 contact  = t.point;
                const Rev::Core::Pos3 dir      = n;

                add(standoff, dir, /*rapid*/true,  /*cutting*/false);   // approach
                add(contact,  dir, /*rapid*/false, /*cutting*/true);    // slow plunge
                add(standoff, dir, /*rapid*/true,  /*cutting*/false);   // retract
            }

            state->probePreviewPath.finalizePoints();
        }

        void rebuildPreviewTimelineIfNeeded() {

            if (!previewTimelineDirty) { return; }

            // Refresh each stage's probe-preview path so the timeline picks up its
            // duration (and the latest correction) when it rebuilds below.
            if (Cam::App::Project* project = activeProject()) {
                for (Cam::App::Stage* state : project->stages) {
                    rebuildProbePreviewPath(state);
                }
            }

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
            Cam::App::Stage* state,
            Event& e,
            bool wasPlaying = false
        ) {

            Cam::App::Project* project = activeProject();

            if (!state || !project || !app) { return false; }

            const bool addToSelection = project->viewSelection.size() > 1;

            if (!app->selectStage(state, addToSelection)) { return false; }

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

            Cam::App::Stage* targetState = nullptr;
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
                targetState = project->nextStageAfterViewSelection();

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

            Cam::App::Stage* targetState = current.state;
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
                targetState = project->previousStageBeforeViewSelection();

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

        void syncAxisLines() {

            testLines.clear();

            if (!lineActor || !lineActor->lines) { return; }

            Cam::App::Project* project = activeProject();

            // THE WORK frame, drawn DIRECTLY from its CoordinateSystem: the nominal
            // user frame placed where probing determined it actually is.  The lines
            // are baked at the frame's pose, so what you see IS project->workFrame
            // (the actor carries no transform -- see applyWorldTransforms).  The
            // PART frame is built per-telemetry-frame in applyWorldTransforms (it
            // rides the chuck), so it is not built here.
            if (project) {
                project->workFrame = buildWorkFrame(project);
                drawFrame(project->workFrame, testLines, 90.0f);
            }
            lineActor->lines->dirty = true;

            // The MEASURED rotary axis line (orange), drawn static at its located
            // position once probing has determined it.  This is the physical line
            // the part turns about -- the answer to "where is the chuck centreline".
            axisLineVerts.clear();
            if (Cam::App::Project* prj = activeProject()) {
                if (prj->workRotaryAxis.measured) {
                    Rev::Core::Pos3 dir = prj->workRotaryAxis.direction;
                    const float dl = dir.pythag();
                    if (dl > 1e-6f) {
                        dir = dir * (1.0f / dl);
                        const Rev::Core::Pos3 p = prj->workRotaryAxis.point;
                        const float L = 120.0f;
                        const Rev::Core::Color c = { 1.0f, 0.55f, 0.0f, 1.0f };
                        const Rev::Core::Pos3 a = p - dir * L;
                        const Rev::Core::Pos3 b = p + dir * L;
                        axisLineVerts.push_back({ a.x, a.y, a.z, c });
                        axisLineVerts.push_back({ b.x, b.y, b.z, c });
                    }
                }
            }
            if (axisLineActor && axisLineActor->lines) {
                axisLineActor->lines->dirty = true;
            }
        }

        void createRotaryAxisActor() {

            axisLineActor = new View3d::Actor();

            axisLineActor->visible = true;
            axisLineActor->selectable = false;
            axisLineActor->ownsLines = true;
            axisLineActor->includeInFit = false;

            axisLineActor->lines = new Rev::Primitives::Lines3d(shared->canvas, {
                .lines = &axisLineVerts
            });

            axisLineActor->lines->color = { 1.0f, 0.55f, 0.0f, 1.0f };

            if (view3d) {
                view3d->addActor(axisLineActor);
            }
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

        void createPartFrameActor() {

            partFrameActor = new View3d::Actor();

            partFrameActor->visible = true;
            partFrameActor->selectable = false;
            partFrameActor->ownsLines = true;
            partFrameActor->includeInFit = false;

            partFrameActor->lines = new Rev::Primitives::Lines3d(shared->canvas, {
                .lines = &partFrameLines
            });

            partFrameActor->lines->color = {
                1.0f,
                1.0f,
                1.0f,
                1.0f
            };

            if (view3d) {
                view3d->addActor(partFrameActor);
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

        void createSpindlePreviewActor() {

            spindlePreviewActor = new View3d::Actor();

            spindlePreviewActor->visible = false;
            spindlePreviewActor->selectable = false;
            spindlePreviewActor->ownsMesh = true;
            spindlePreviewActor->ownsTriangles = true;
            spindlePreviewActor->includeInFit = false;

            spindlePreviewActor->mesh = new Rev::Primitives::Mesh3d(shared->canvas, {
                .triangles = &spindlePreviewTriangles
            });

            spindlePreviewActor->mesh->color = {
                0.72f,
                0.74f,
                0.78f,
                0.92f
            };

            if (view3d) {
                view3d->addActor(spindlePreviewActor);
            }
        }

        struct ToolPreviewTarget {
            Cam::App::Stage* state = nullptr;
            double progress = 0.0;
        };

        Cam::App::Stage* materialStateWithToolPathForPreview(
            Cam::App::Project* project
        ) {

            if (!project) { return nullptr; }

            const std::vector<Cam::App::Stage*> sequence =
                ToolPathPreviewTimeline::previewSequence(project);

            if (!sequence.empty()) {
                return sequence.front();
            }

            Cam::App::Stage* primary = project->primaryViewStage();

            if (primary && primary->hasToolPath) {
                return primary;
            }

            for (Cam::App::Stage* state : project->viewSelection) {
                if (state && state->hasToolPath) {
                    return state;
                }
            }

            for (Cam::App::Stage* state : project->stages) {
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
            Cam::App::Stage* activeState
        ) {

            if (!activeState) { return nullptr; }

            for (Cam::Gui::World::Stage* view : materialViews) {

                if (!view || view->state != activeState) { continue; }

                if (view->deltaActor) {
                    return view->deltaActor;
                }
            }

            for (Cam::Gui::World::Stage* view : materialViews) {

                if (view && view->deltaActor) {
                    return view->deltaActor;
                }
            }

            return nullptr;
        }

        void repositionToolPreviewDrawOrder(Cam::App::Stage* activeState) {

            if (!view3d || !toolPreviewActor) { return; }

            View3d::Actor* before = deltaActorForToolPreviewDrawOrder(activeState);

            if (!before) { return; }

            view3d->insertActorBefore(toolPreviewActor, before);
        }

        void repositionSpindlePreviewDrawOrder() {

            if (!view3d || !spindlePreviewActor || !toolPreviewActor) { return; }

            view3d->insertActorBefore(spindlePreviewActor, toolPreviewActor);
        }

        static void identityMatrix(float out[16]) {
            out[0]  = 1.0f; out[1]  = 0.0f; out[2]  = 0.0f; out[3]  = 0.0f;
            out[4]  = 0.0f; out[5]  = 1.0f; out[6]  = 0.0f; out[7]  = 0.0f;
            out[8]  = 0.0f; out[9]  = 0.0f; out[10] = 1.0f; out[11] = 0.0f;
            out[12] = 0.0f; out[13] = 0.0f; out[14] = 0.0f; out[15] = 1.0f;
        }

        static void translationMatrix(float x, float y, float z, float out[16]) {
            identityMatrix(out);
            out[12] = x;
            out[13] = y;
            out[14] = z;
        }

        static void multiplyMatrix4(const float a[16], const float b[16], float out[16]) {
            for (int col = 0; col < 4; col++) {
                for (int row = 0; row < 4; row++) {
                    float sum = 0.0f;
                    for (int k = 0; k < 4; k++) {
                        sum += a[k * 4 + row] * b[col * 4 + k];
                    }
                    out[col * 4 + row] = sum;
                }
            }
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

            if (spindlePreviewActor) {
                spindlePreviewActor->visible = false;
            }

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
                    // Auto-capture all four axes so the A reference is set
                    // even before the operator presses "Set Origin".
                    link.captureMachineOrigin(tcx, tcy, tcz, tca);
                }
            }

            const ToolPreviewTarget target = activeToolPreviewTarget(project);

            // Resolve which tool to show (the previewed op, or the displayed
            // state when nothing is actively previewing — e.g. while jogging).
            Cam::App::Stage* toolState = target.state;

            if (!toolState && project) { toolState = project->displayedStage; }

            Cam::App::Tool* tool = (toolState && app && app->toolLibrary())
                ? app->toolLibrary()->find(toolState->toolPath.toolName)
                : nullptr;

            if (!tool || tool->mesh.empty()) { return; }

            Rev::Core::Pos3 tip;
            Rev::Core::Pos3 dir = { 0.0f, 0.0f, 1.0f };
            bool haveTip = false;

            // EXECUTE: show where the TOOL TIP actually is.  We read the
            // controller's own tip position (WPos) -- tool-length already folded
            // in by the machine -- rather than reconstructing the tip from MPos
            // minus a tool length we'd have to track.  This is why a long probe
            // and a short endmill both display correctly: the machine, not us,
            // owns the tip math.  Falls back to MPos until a WPos arrives.
            float tx, ty, tz, ta;
            float omx, omy, omz, ocx, ocy, ocz;

            if (executeMode &&
                link.connected() &&
                (link.tipTelemetry(tx, ty, tz, ta) || link.telemetry(tx, ty, tz, ta)) &&
                link.workOrigin(omx, omy, omz, ocx, ocy, ocz)) {

                // tipPos - machineOrigin = the tip in the user/machine frame.  Add
                // the begin-work offset (also in-frame) and rotate back into CAD
                // via frame.toWorld -- the exact inverse of the streamer's
                // transform, no ad-hoc axis swaps.
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
            else if (target.state &&
                     (target.state->hasToolPath || !target.state->probePreviewPath.empty())) {

                // Otherwise: the computed preview sample.  The stage's timeline
                // covers its probe THEN its cut, so split the combined progress:
                // sample the probe-preview path while in the probe window, the
                // cut path after.
                Cam::App::ToolPathPoint sample = {};

                const double probeDur = target.state->probePreviewPath.durationSeconds();
                const double cutDur   = target.state->toolPath.durationSeconds();
                const double elapsed  = target.progress * (probeDur + cutDur);

                bool sampled = false;
                if (probeDur > 1e-9 && elapsed < probeDur) {
                    sampled = target.state->probePreviewPath.sampleAtTime(elapsed, sample);
                }
                else if (cutDur > 1e-9) {
                    sampled = target.state->toolPath.sampleAtTime(elapsed - probeDur, sample);
                }
                else if (probeDur > 1e-9) {
                    sampled = target.state->probePreviewPath.sampleAtTime(elapsed, sample);
                }

                if (sampled) {
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

            // Honour the machine tree's Tool visibility request (the spindle is
            // still synced below, independently of the tool).
            toolPreviewActor->visible = (!app || app->machineVisible.tool);

            if (target.state) { repositionToolPreviewDrawOrder(target.state); }
            else if (toolState) { repositionToolPreviewDrawOrder(toolState); }

            syncSpindlePreview(tool, tip, dir, executeMode);
        }

        void syncSpindlePreview(
            Cam::App::Tool* tool,
            const Rev::Core::Pos3& tip,
            const Rev::Core::Pos3& dir,
            bool executeMode
        ) {
            if (!spindlePreviewActor || !spindlePreviewActor->mesh || !app) {
                return;
            }

            spindlePreviewActor->visible = false;

            if (!tool || tool->mesh.empty()) { return; }

            // Honour the machine tree's spindle visibility request.
            if (app && !app->machineVisible.spindle) { return; }

            Cam::App::MachineProfile* machine = app->selectedMachine();
            if (!machine || machine->spindleModel.render.triangles.empty()) { return; }

            spindlePreviewActor->mesh->pTriangles = &machine->spindleModel.render.triangles;

            if (machine->spindleMeshRevision != previewSpindleMeshRevision) {
                spindlePreviewActor->mesh->dirty = true;
                previewSpindleMeshRevision = machine->spindleMeshRevision;
            }

            float toolPlacement[16];
            toolPlacementMatrix(tip, dir, toolPlacement);

            const float collarTop = static_cast<float>(tool->totalLength());
            float offset[16];
            translationMatrix(0.0f, 0.0f, collarTop, offset);

            float spindlePlacement[16];
            multiplyMatrix4(toolPlacement, offset, spindlePlacement);

            for (int i = 0; i < 16; i++) {
                spindlePreviewActor->modelTransform[i] = spindlePlacement[i];
            }

            if (executeMode) {
                float identity[16];
                identityMatrix(identity);
                spindlePreviewActor->setWorldTransform(identity);
            }

            spindlePreviewActor->visible = true;
            repositionSpindlePreviewDrawOrder();
        }

        // App/project access
        //--------------------------------------------------

        Cam::App::Project* activeProject() const {

            if (!app) { return nullptr; }

            return app->activeProject;
        }

        Cam::App::Stage* displayedState() {

            Cam::App::Project* project = activeProject();

            if (!project) { return nullptr; }

            return project->displayedStage;
        }

        Cam::App::Stage* workingState() {

            Cam::App::Project* project = activeProject();

            if (!project) { return nullptr; }

            return project->workingStage;
        }

        Cam::App::Model* selectionModel() {

            Cam::App::Stage* state = displayedState();

            if (!state) { return nullptr; }

            return &state->model;
        }

        bool displayedModelIsEditable() {

            Cam::App::Project* project = activeProject();

            if (!project) { return false; }

            return (
                project->displayedStage &&
                project->workingStage &&
                project->displayedStage == project->workingStage
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

            if (materialViews.size() != project->stages.size()) {
                return false;
            }

            for (Cam::App::Stage* state : project->stages) {
                if (!viewForState(state)) {
                    return false;
                }
            }

            for (Cam::Gui::World::Stage* view : materialViews) {

                if (!view) { return false; }

                if (!projectOwnsState(project, view->state)) {
                    return false;
                }
            }

            return true;
        }

        void clearMaterialViews() {

            for (Cam::Gui::World::Stage* view : materialViews) {
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

        Cam::Gui::World::Stage* viewForState(
            Cam::App::Stage* state
        ) {
            for (Cam::Gui::World::Stage* view : materialViews) {

                if (view && view->state == state) {
                    return view;
                }
            }

            return nullptr;
        }

        Cam::Gui::World::Stage* displayedMaterialView() {
            return viewForState(displayedState());
        }

        Cam::Gui::World::Stage* workingMaterialView() {
            return viewForState(workingState());
        }

        Cam::Gui::World::Stage* createMaterialView(
            Cam::App::Stage* state
        ) {
            Cam::Gui::World::Stage* worldState =
                new Cam::Gui::World::Stage(shared->canvas);

            worldState->setState(state);
            worldState->attach(view3d);

            materialViews.push_back(worldState);

            return worldState;
        }

        bool projectOwnsState(
            Cam::App::Project* project,
            Cam::App::Stage* state
        ) {
            if (!project || !state) { return false; }

            for (Cam::App::Stage* candidate : project->stages) {
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

            for (Cam::App::Stage* state : representedProject->stages) {
                createMaterialView(state);
            }

            representedDisplayedState = representedProject->displayedStage;
            representedWorkingState = representedProject->workingStage;
            representedStateCount = representedProject->stages.size();

            representedProject->linkStageToolPaths();

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
            for (Cam::App::Stage* state : project->stages) {
                if (!viewForState(state)) {
                    createMaterialView(state);
                }
            }

            // Remove deleted material states.
            for (size_t i = 0; i < materialViews.size();) {

                Cam::Gui::World::Stage* view = materialViews[i];

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

            representedStateCount = project->stages.size();
        }

        void applyVisibilityPolicy() {

            Cam::App::Project* project = activeProject();
            Cam::App::Stage* primary = project ? project->primaryViewStage() : nullptr;

            const bool faceEditingActive = (
                project &&
                project->workingStage &&
                project->displayedStage == project->workingStage
            );

            for (Cam::Gui::World::Stage* view : materialViews) {

                if (!view) { continue; }

                view->hideAll();

                if (!project || !view->state) { continue; }
                if (!project->isViewSelected(view->state)) { continue; }

                // Each stage's per-component flags are visibility *requests*; the
                // selection policy decides what is *allowed*, and a component is
                // shown only when it is both requested and allowed.
                //
                //   - Models (prior + current) only on the primary selected
                //     stage, so the stock from other selected stages doesn't
                //     stack up and occlude. Their requests still "want" to be
                //     visible — they just can't be while multi-selected.
                //   - Delta + toolpath are allowed for every selected stage.
                const bool isPrimary = (view->state == primary);

                const bool allowModels   = isPrimary;
                const bool allowDelta    = true;
                const bool allowToolPath = true;

                const Cam::App::Stage::ComponentVisibility& vis = view->state->visible;

                view->showPart     = vis.priorModel && allowModels;
                view->showModel    = vis.model      && allowModels;
                view->showDelta    = vis.delta      && allowDelta;
                view->showToolPath = vis.toolPath   && allowToolPath;

                // A component actively selected in the tree is force-shown, even
                // if its visibility request is off ("select" temporarily reveals
                // it). Indices match the stage tree order.
                if (project->selectedComponentStage == view->state) {
                    switch (project->selectedComponentIndex) {
                        case 0: view->showPart     = true; break;  // Prior Model
                        case 1: view->showModel    = true; break;  // Model
                        case 2: view->showPart     = true; break;  // Operation → prior model (for face highlight)
                        case 3: view->showDelta    = true; break;  // Delta
                        case 4: view->showToolPath = true; break;  // Toolpath
                        default: break;
                    }
                }

                view->includeInFit = isPrimary;
            }

            // Picking follows the editable working view (displayed == working).
            // The pick actor is invisible, so this adds interaction without
            // forcing any model layer visible.
            if (faceEditingActive) {

                Cam::Gui::World::Stage* editView =
                    viewForState(project->workingStage);

                if (
                    editView &&
                    project->isViewSelected(project->workingStage)
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
        // The machine definition is derived ENTIRELY from the project-wide work
        // coordinate system (buildWorkFrame), so it no longer depends on any
        // particular stage -- hence no parameter.
        Cam::Machine::MachineDefinition buildMachineDefinition() {
            // UNIFIED: the machine is derived from the EXACT SAME work coordinate
            // system the world view draws (buildWorkFrame) -- one object whose X is
            // the rotary axis (measured when probing has found it, the user's
            // nominal X otherwise) and whose origin is the pivot.  Everything that
            // rotates -- the IK solve, every emitted cut, the drawn frame, and the
            // world view's part rotation -- now pivots about this single frame.
            Cam::Coord::CoordinateSystem workCS = buildWorkFrame(activeProject());

            return Cam::Machine::MachineDefinition::fromWorkFrame(workCS);
        }

        // "Set Top": probe straight down, then set the part Z from the contact.
        // A one-shot -- it loads the probe + probes Z down, and the next contact is
        // consumed by handleProbeContact's setTopPending_ branch (below).
        void armSetTop() {
            setTopPending_ = true;
            Carvera::MachineLink::instance().probeTest();
            Carvera::MachineLink::instance().log(
                "Set Top: probing the top face; the part Z will be set from contact.");
        }

        // Apply a top touch-off: shift the part along the work-frame up axis so its
        // KNOWN top (the selected material state's highest point) sits at the probed
        // height.  This sets the part coordinate system's Z relative to the work
        // frame from a single contact -- even with no stock defined.
        void applySetTop(Carvera::MachineLink::ProbeEvent& e) {
            Cam::App::Project* project = activeProject();
            if (!project) { return; }
            if (!e.triggered) {
                Carvera::MachineLink::instance().log(
                    "Set Top: probe did not contact -- part Z unchanged.");
                return;
            }
            Carvera::MachineLink& link = Carvera::MachineLink::instance();
            float omx, omy, omz, oma;
            if (!link.machineOrigin(omx, omy, omz, oma)) {
                link.log("Set Top: set the work origin first.");
                return;
            }

            const UserFrame frame = currentUserFrame(project);

            // The contact, in the work frame's up (Z) coordinate.
            const Rev::Core::Pos3 wcs{ e.x - omx, e.y - omy, e.z - omz };
            const double measuredTopF = frame.toFrame(frame.toWorld(wcs)).z;

            // The selected material state's KNOWN top: its highest point along the
            // work-frame up axis (in frame coords, relative to the axis origin).
            Cam::App::Stage* stage = project->displayedStage;
            double nominalTopF = 0.0; bool have = false;
            if (stage) {
                for (const Rev::Core::Vertex3& v : stage->model.render.triangles) {
                    const double z = frame.toFrame(v).z;
                    if (!have || z > nominalTopF) { nominalTopF = z; have = true; }
                }
            }
            if (!have) {
                link.log("Set Top: no material state geometry to reference.");
                return;
            }

            const double offset = measuredTopF - nominalTopF;   // how far above nominal

            // Set the part's offset within the work frame as a pure translation along
            // the work-frame up axis -- the part now sits where the probe found it.
            project->probeCorrection.reset();
            project->probeCorrection.t     = frame.Z * static_cast<float>(offset);
            project->probeCorrection.tTrue = project->probeCorrection.t;
            project->probeCorrection.valid = true;
            project->dirty = true;
            machineToolPathsDirty = true;
            previewTimelineDirty  = true;

            link.log(std::format(
                "Set Top: part top at {:.3f} mm (nominal {:.3f}) -> part Z offset "
                "{:.3f} mm above the work frame.", measuredTopF, nominalTopF, offset));

            syncAxisLines();
            if (view3d && shared && shared->event) { view3d->refresh(*shared->event); }
        }

        // A probe touched the part.  Air reports the contact in MACHINE-WCS;
        // map it back into the CAD frame, fill the next pending session entry,
        // and — once every entry for a stage is in — fit and store that stage's
        // ProbeResult.  From then on getMachineToolPath() applies it.
        void handleProbeContact(Carvera::MachineLink::ProbeEvent& e) {

            // Set Top consumes the contact directly (one-shot, no session).
            if (setTopPending_) {
                setTopPending_ = false;
                applySetTop(e);
                return;
            }

            if (probeFillIndex_ >= probeSession_.size()) {
                dbg("[Probe] contact with no pending session entry - ignoring");
                return;
            }

            ProbeSessionEntry& entry = probeSession_[probeFillIndex_++];

            // [PRB:x,y,z] is the ABSOLUTE MACHINE position at contact.  Map it back
            // to the part/CAD frame: subtract the machine work origin to get the
            // part-relative offset, add beginWorkInFrame, then frame.toWorld.  This
            // is the exact inverse of toMachine().  The probe is ALWAYS driven to
            // fixed nominal positions (never rotated by a correction), so the part
            // is at its mounted pose during every contact -- there is no part
            // rotation to undo, by construction.
            float omx = 0, omy = 0, omz = 0, ocx = 0, ocy = 0, ocz = 0;
            Carvera::MachineLink::instance().workOrigin(omx, omy, omz, ocx, ocy, ocz);

            const Rev::Core::Pos3 wcs{ e.x - omx, e.y - omy, e.z - omz };

            const Rev::Core::Pos3 inFrame{
                wcs.x + probeBeginWorkInFrame_.x,
                wcs.y + probeBeginWorkInFrame_.y,
                wcs.z + probeBeginWorkInFrame_.z
            };
            const Rev::Core::Pos3 cad =
                probeFrameOrigin_
                + probeFrameX_ * inFrame.x
                + probeFrameY_ * inFrame.y
                + probeFrameZ_ * inFrame.z;

            entry.filled         = true;
            entry.measuredCad    = cad;
            entry.machineContact = { e.x, e.y, e.z };   // absolute -- for axis fit
            entry.triggered      = e.triggered;

            dbg("[Probe] contact %zu/%zu: machineAbs(%.3f,%.3f,%.3f) trig=%d "
                "-> wcs(%.3f,%.3f,%.3f) -> cad(%.3f,%.3f,%.3f) nominal(%.3f,%.3f,%.3f)",
                probeFillIndex_, probeSession_.size(),
                e.x, e.y, e.z, e.triggered ? 1 : 0,
                wcs.x, wcs.y, wcs.z,
                cad.x, cad.y, cad.z,
                entry.nominalCad.x, entry.nominalCad.y, entry.nominalCad.z);

            // Fit this stage's correction once all of ITS entries are filled
            // (the last contact of a stage is the final entry, or the one before
            // the next stage's first entry).
            Cam::App::Stage* stage = entry.stage;
            const bool stageComplete =
                (probeFillIndex_ >= probeSession_.size()) ||
                (probeSession_[probeFillIndex_].stage != stage);
            if (!stageComplete) { return; }

            size_t stageContacts = 0;
            bool   allTriggered  = true;
            for (const ProbeSessionEntry& s : probeSession_) {
                if (s.stage != stage || !s.filled) { continue; }
                stageContacts++;
                allTriggered = allTriggered && s.triggered;
            }

            if (stageContacts == 0) { return; }

            if (!allTriggered) {
                Carvera::MachineLink::instance().log(
                    "Probe: a touch did not trigger - correction NOT applied "
                    "(re-run the probe).");
                dbg("[Probe] stage fit skipped: not all contacts triggered");
                return;
            }

            // Rotary axis (DIRECTION) in the model frame: needed both for the
            // achievable fit and to canonicalize multi-orientation contacts.
            const Cam::Machine::MachineDefinition def = buildMachineDefinition();
            Rev::Core::Pos3 rotaryAxis{};
            if (!def.part.dof.freeRotations.empty()) {
                rotaryAxis = def.part.dof.freeRotations.front();
            }
            // POINT on the rotary axis.  Until we have MEASURED the axis (Stage 3),
            // assume it passes through the user-frame origin (taken on faith).  Once
            // multi-orientation probing has located the real axis line, use THAT --
            // so corrections pivot about where the rotary axis actually is, not where
            // the user assumed (the "part rests above the axis" case).
            Cam::App::Project* project = activeProject();
            if (!project) { return; }

            // CALIBRATE + CONFIRM.  Calibration passes (flat / +10 / -10) feed the
            // work-frame + rotary-axis fit; the LAST one (all three orientations in)
            // actually fits.  The CONFIRM pass -- physically squared by the fitted
            // work frame -- feeds the part's residual offset within it.
            // The work frame (rotary axis) is fixed up front by machine calibration,
            // so a stage probe only measures the PART's offset within it -- the
            // contacts feed partMeasurements directly (no axis inference, and the
            // work correction is identity, so nothing to peel).
            for (const ProbeSessionEntry& s : probeSession_) {
                if (s.stage != stage || !s.filled) { continue; }
                Cam::App::ProbeMeasurement m;
                m.angleDeg = s.commandedAngleDeg;
                m.nominal  = s.nominalCad;
                m.normal   = s.normalCad;
                m.measured = s.measuredCad;
                m.machineContact = s.machineContact;
                project->partMeasurements.push_back(m);
            }

            auto tiltOf = [](const double* m) {
                const double tr = m[0] + m[4] + m[8];
                return std::acos(std::clamp((tr - 1.0) * 0.5, -1.0, 1.0)) * 57.29577951308232;
            };

            {
                // PART offset within the (machine-calibrated) work frame.
                const Rev::Core::Pos3 axisPt = project->workRotaryAxis.measured
                    ? project->workRotaryAxis.point : probeFrameOrigin_;
                project->probeCorrection = Cam::App::ProbeResult::solve(
                    project->partMeasurements, rotaryAxis, axisPt);
                const Cam::App::ProbeResult& p = project->probeCorrection;
                Carvera::MachineLink::instance().log(std::format(
                    "Probe: PART offset within work frame from {} contact(s) - "
                    "tilt={:.2f} deg, t=({:.3f},{:.3f},{:.3f}) rms={:.4f} mm.",
                    project->partMeasurements.size(), tiltOf(p.r),
                    p.t.x, p.t.y, p.t.z, p.rmsError));
            }
            project->dirty = true;

            // The correction changed; force a re-solve so cuts (and the preview)
            // pick it up, and rebuild the probe-preview path + timeline so a
            // post-run scrub shows the corrected probe motion.
            machineToolPathsDirty = true;
            previewTimelineDirty  = true;

            // Rebuild the drawn frames + the located rotary-axis line now (the
            // probe path does not run a full syncRepresentation, so the axis line
            // would otherwise not refresh until the next UI interaction).
            syncAxisLines();
            if (view3d && shared && shared->event) { view3d->refresh(*shared->event); }
        }

        // Return a reference to the solved MachineToolPath for a given state,
        // solving it on-demand if not yet cached or if the cache is dirty.
        Cam::Machine::MachineToolPath const* getMachineToolPath(
            Cam::App::Stage* state
        ) {
            if (!state || !state->hasToolPath) { return nullptr; }

            if (machineToolPathsDirty) {
                machineToolPaths.clear();
                machineToolPathsDirty = false;
            }

            auto it = machineToolPaths.find(state);

            if (it == machineToolPaths.end()) {

                // Apply the PROJECT-WIDE probe correction to the toolpath BEFORE
                // the IK solve.  Positions shift and tool directions rotate by the
                // fitted (R,t) (the achievable, rotary-axis part); the solver then
                // produces whatever rotary swing those directions require -- so a
                // corrected part pose rotates the chuck as a consequence of the
                // geometry.  The correction is GLOBAL (a part property), so EVERY
                // stage's cut is corrected by the one probe; the default identity
                // means an un-probed project solves exactly as before.
                // The cut is corrected by the FULL part pose: the work-frame
                // correction with the part's offset nested inside it (W o P).
                Cam::App::Project* corrProject = activeProject();
                const Cam::App::ProbeResult correction =
                    corrProject ? corrProject->totalPose() : Cam::App::ProbeResult{};

                // ToolPath is non-copyable (it owns strategy objects), and the IK
                // solver only reads `.points`, so build a points-only corrected
                // path rather than copying the whole toolpath.
                Cam::App::ToolPath corrected;
                corrected.points.reserve(state->toolPath.points.size());
                for (const Cam::App::ToolPathPoint& pt : state->toolPath.points) {
                    Cam::App::ToolPathPoint c = pt;
                    c.position      = correction.apply(pt.position);
                    c.toolDirection = correction.applyDirection(pt.toolDirection);
                    corrected.points.push_back(c);
                }

                Cam::Machine::MachineToolPath solved =
                    Cam::Machine::IKSolver::solve(
                        corrected,
                        buildMachineDefinition()
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

            Cam::App::Stage* state =
                previewTimeline.segments[here.segmentIndex].state;

            Cam::Machine::MachineToolPath const* path = getMachineToolPath(state);

            if (!path || path->empty()) { return false; }

            // The exact IK transform: rotation about the machine's fixed rotary
            // axis by the interpolated index angle, pivoted at the stock centre.
            return path->partMatrixAtProgress(here.localProgress, outM);
        }

        // Build the part-rotation matrix for Execute mode from live A-axis telemetry.
        // Uses the same axis/pivot as the IK solver so the scene matches what the
        // machine is actually doing.
        bool executePartMatrix(float out[16]) {

            Cam::Machine::Pose::identityMatrix(out);

            Carvera::MachineLink& link = Carvera::MachineLink::instance();

            float tx, ty, tz, ta;
            if (!link.telemetry(tx, ty, tz, ta)) { return false; }

            Cam::App::Project* project = activeProject();
            if (!project || !project->displayedStage) { return false; }

            Rev::Core::Pos3 rotaryAxis  = { 1.0f, 0.0f, 0.0f };
            Rev::Core::Pos3 rotaryPivot = {};

            // Prefer the already-solved path — it stores the exact axis/pivot
            // the streamer used, so the live rotation stays in sync.
            Cam::Machine::MachineToolPath const* path =
                getMachineToolPath(project->displayedStage);

            if (path && !path->empty()) {
                rotaryAxis  = path->rotaryAxis;
                rotaryPivot = path->rotaryPivot;
            }
            else {
                Cam::Machine::MachineDefinition def =
                    buildMachineDefinition();
                if (!def.part.dof.freeRotations.empty()) {
                    rotaryAxis = def.part.dof.freeRotations.front();
                }
                rotaryPivot = def.part.defaultPose.position;
            }

            // Part-relative rotary angle = MPos A − machineOriginA.
            //
            // We now drive the machine in ABSOLUTE coordinates (the WCS offset is
            // zeroed, so WPos == MPos and is NOT part-relative).  The part's
            // rotation relative to its mounted/nominal pose is therefore the
            // absolute A minus the A captured at "set work origin" (the mount
            // angle).  This is correct regardless of the machine's A-home and does
            // not depend on any controller-side offset.
            constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

            const float angleRad = (ta - link.machineOriginA()) * kDegToRad;

            Cam::Machine::Pose::axisAngleMatrix(
                rotaryAxis, angleRad, rotaryPivot, out
            );
            return true;
        }

        // Find the closest point on the solved MachineToolPath(s) to the given
        // CAD-space tool position and return the (state, progress [0,1]) pair.
        // Returns false when no path is available or the work origin is unknown.
        bool findToolpathProgress(
            const Rev::Core::Pos3& toolCadPos,
            Cam::App::Project*     project,
            Cam::App::Stage*& outState,
            double&                  outProgress
        ) {
            std::vector<Cam::App::Stage*> searchStates =
                ToolPathPreviewTimeline::previewSequence(project);

            if (searchStates.empty()) {
                if (Cam::App::Stage* s =
                        materialStateWithToolPathForPreview(project)) {
                    searchStates.push_back(s);
                }
            }

            float bestDist = 1.0e30f;
            outState    = nullptr;
            outProgress = 0.0;

            for (Cam::App::Stage* state : searchStates) {
                if (!state) { continue; }
                Cam::Machine::MachineToolPath const* path = getMachineToolPath(state);
                if (!path || path->empty()) { continue; }
                const double dur = path->durationSeconds();
                for (const auto& mp : path->points) {
                    const Rev::Core::Pos3 d = mp.toolWorldPose.position - toolCadPos;
                    const float dist = d.pythag();
                    if (dist < bestDist) {
                        bestDist    = dist;
                        outState    = state;
                        outProgress = (dur > 0.0) ? mp.t / dur : 0.0;
                    }
                }
            }

            return outState != nullptr;
        }

        // Convert live telemetry MPos into CAD world space.
        // Returns false if telemetry or work-origin is not yet established.
        bool telemetryToCadPos(Rev::Core::Pos3& outPos) {
            Carvera::MachineLink& link = Carvera::MachineLink::instance();
            float tx, ty, tz, ta;
            float omx, omy, omz, ocx, ocy, ocz;
            if (!link.telemetry(tx, ty, tz, ta))                    { return false; }
            if (!link.workOrigin(omx, omy, omz, ocx, ocy, ocz))    { return false; }

            Cam::App::Project* project = activeProject();
            if (!project) { return false; }

            const UserFrame frame          = currentUserFrame(project);
            const Rev::Core::Pos3 bw       = beginWorkOrigin(project, frame);
            const Rev::Core::Pos3 bwF      = frame.toFrame(bw);
            const Rev::Core::Pos3 wcsInF   = { tx - omx, ty - omy, tz - omz };
            const Rev::Core::Pos3 inF      = { wcsInF.x + bwF.x,
                                               wcsInF.y + bwF.y,
                                               wcsInF.z + bwF.z };
            outPos = frame.toWorld(inF);
            return true;
        }

        // Called each execute-poll tick while isExecuting().
        // 1. Finds the closest toolpath point to the live tool position.
        // 2. Enforces monotonic forward progress (noise can't jump us backward).
        // 3. Locks the view to whichever material state is currently executing.
        // 4. Advances the timeline so the material-removal preview stays in sync.
        //
        // NOTE: does NOT call syncPreviewSlider — that is handled by the
        // syncAllMaterialViews() call that follows in the execute tick.
        void syncTimelineToMachinePosition(Event& e) {

            Cam::App::Project* project = activeProject();
            if (!project) { return; }

            Rev::Core::Pos3 toolCadPos;
            if (!telemetryToCadPos(toolCadPos)) { return; }

            Cam::App::Stage* bestState    = nullptr;
            double                   bestProgress = 0.0;

            if (!findToolpathProgress(toolCadPos, project, bestState, bestProgress)) {
                return;
            }

            // -------------------------------------------------------
            // Monotonic progress: within the same executing state,
            // telemetry noise must not drive progress backward.
            // Allow up to 3 % backward tolerance (covers jitter at
            // slow feed rates where adjacent points are very close).
            // A genuine new execution resets the tracker (see the
            // operationProvider registration).
            // -------------------------------------------------------
            constexpr double kBackTolerance = 0.03;

            if (bestState == executeTrackedState) {
                if (bestProgress < executeTrackedProgress - kBackTolerance) {
                    bestProgress = executeTrackedProgress;   // clamp to last known
                }
            }

            executeTrackedState    = bestState;
            executeTrackedProgress = bestProgress;

            // -------------------------------------------------------
            // State lock: if the user navigated to a different material
            // state while the machine is running, force the view back
            // to the state that's actually being cut right now.
            // -------------------------------------------------------
            if (bestState && app && project->primaryViewStage() != bestState) {
                if (app->selectStage(bestState, false)) {
                    previewTimelineDirty  = true;   // rebuild segment map, not IK
                    representationDirty   = true;
                }
            }

            // -------------------------------------------------------
            // Advance the timeline to the matched progress.
            // -------------------------------------------------------
            rebuildPreviewTimelineIfNeeded();

            for (const PreviewSegment& seg : previewTimeline.segments) {
                if (seg.state != bestState) { continue; }
                const double segElapsed =
                    seg.startSeconds + bestProgress * seg.durationSeconds;
                previewTimeline.setElapsed(segElapsed);
                break;
            }
        }

        // 4x4 (column-major) of the project's ACHIEVABLE probe correction (r/t),
        // i.e. the pose the machine actually drives the part to.  Identity when
        // there is no valid correction.  We compose the ACHIEVABLE (not the true)
        // correction so the displayed part stays locked to the tool: the cut/IK
        // and the telemetry tool both use exactly this transform, so part and tool
        // never desync (composing the TRUE fit instead is what made the tool
        // plunge).  The off-axis residual the machine can't make is reported, not
        // drawn.
        // Column-major 4x4 from a correction's ACHIEVABLE (r/t) part.
        static void correctionToMatrix(const Cam::App::ProbeResult& c, float out[16]) {
            Cam::Machine::Pose::identityMatrix(out);
            out[0]  = float(c.r[0]); out[1]  = float(c.r[3]); out[2]  = float(c.r[6]);
            out[4]  = float(c.r[1]); out[5]  = float(c.r[4]); out[6]  = float(c.r[7]);
            out[8]  = float(c.r[2]); out[9]  = float(c.r[5]); out[10] = float(c.r[8]);
            out[12] = c.t.x;         out[13] = c.t.y;         out[14] = c.t.z;
        }

        // ===========================================================
        // CoordinateSystem bridge (the unified representation)
        // ===========================================================

        // A column-major 4x4 (the view/IK convention) -> a CoordinateSystem.
        static Cam::Coord::CoordinateSystem csFromColMajor(const float m[16]) {
            Cam::Coord::CoordinateSystem cs;
            cs.r[0] = m[0]; cs.r[1] = m[4]; cs.r[2] = m[8];
            cs.r[3] = m[1]; cs.r[4] = m[5]; cs.r[5] = m[9];
            cs.r[6] = m[2]; cs.r[7] = m[6]; cs.r[8] = m[10];
            cs.t = { m[12], m[13], m[14] };
            cs.resolveAxes();
            return cs;
        }

        // The user's nominal work frame as a CoordinateSystem (its X axis is the
        // rotary axis -- the "co"/"ax" gestures define the basis).
        Cam::Coord::CoordinateSystem userFrameCS(Cam::App::Project* project) {
            const UserFrame f = currentUserFrame(project);
            Cam::Coord::CoordinateSystem cs = Cam::Coord::CoordinateSystem::fromBasis(f.origin, f.X, f.Y, f.Z);
            // The rotary is the X axis: free + slow; the linears are free + fast.
            cs.rx.maxSpeed = 1.0;   // rotation: machine-driven (slow)
            cs.x.maxSpeed  = 1.0;   // translations: machine-driven (fast) -- relative
            cs.y.maxSpeed  = 0.0;   // lateral Y: faith-only on a 4-axis top probe
            cs.z.maxSpeed  = 1.0;
            return cs;
        }

        // THE WORK frame as a CoordinateSystem: the nominal user frame placed where
        // probing says it actually is (workCorrection applied).
        // THE one work coordinate system, shared by the display AND the machine
        // definition.  Its X axis rests EXACTLY on the rotary axis (measured when
        // probing has found it, the user's nominal X otherwise); its origin is a
        // point on that axis (the pivot).  Y/Z keep the user frame's orientation,
        // re-orthogonalised against X, so the frame reads like the user frame but
        // with its X snapped to the rotary line.  rx is the live rotary; its
        // certainty records whether the axis was measured or is taken on faith.
        Cam::Coord::CoordinateSystem buildWorkFrame(Cam::App::Project* project) {

            const UserFrame f = currentUserFrame(project);

            Rev::Core::Pos3 pivot = f.origin;
            Rev::Core::Pos3 X     = f.X;
            bool            measured = false;

            if (project && project->workRotaryAxis.measured) {
                pivot    = project->workRotaryAxis.point;
                X        = project->workRotaryAxis.direction;
                measured = true;
            }

            const float xl = X.pythag();
            X = (xl > 1e-6f) ? X * (1.0f / xl) : Rev::Core::Pos3{ 1.0f, 0.0f, 0.0f };

            // Y from the user frame, made perpendicular to X; fall back to any
            // perpendicular if the user's Y happens to be parallel to the rotary.
            Rev::Core::Pos3 Y = f.Y - X * f.Y.dot(X);
            float yl = Y.pythag();
            if (yl <= 1e-6f) {
                const Rev::Core::Pos3 ref =
                    (std::fabs(X.z) < 0.9f) ? Rev::Core::Pos3{ 0,0,1 } : Rev::Core::Pos3{ 1,0,0 };
                Y  = ref.cross(X);
                yl = Y.pythag();
            }
            Y = (yl > 1e-6f) ? Y * (1.0f / yl) : Rev::Core::Pos3{ 0.0f, 1.0f, 0.0f };
            const Rev::Core::Pos3 Z = X.cross(Y);

            Cam::Coord::CoordinateSystem cs =
                Cam::Coord::CoordinateSystem::fromBasis(pivot, X, Y, Z);
            cs.rx.maxSpeed  = 1.0;                    // 3+1: the rotary is live
            cs.rx.certainty = measured ? 1.0 : 0.25;  // measured vs taken on faith
            return cs;
        }

        // Append the THREE axes (X red, Y green, Z blue) of a CoordinateSystem as
        // line segments, baked at the frame's actual pose -- so what is drawn IS a
        // direct reflection of the CoordinateSystem object.
        static void drawFrame(const Cam::Coord::CoordinateSystem& cs,
                              std::vector<Rev::Core::Vertex3>& out, float L) {
            const Rev::Core::Pos3 o = cs.apply({ 0.0f, 0.0f, 0.0f });
            auto axis = [&](Rev::Core::Pos3 localDir, Rev::Core::Color c) {
                const Rev::Core::Pos3 d = cs.applyDirection(localDir);
                const Rev::Core::Pos3 e = o + d * L;
                out.push_back({ o.x, o.y, o.z, c });
                out.push_back({ e.x, e.y, e.z, c });
            };
            axis({ 1.0f, 0.0f, 0.0f }, { 1.0f, 0.20f, 0.20f, 1.0f });   // X red
            axis({ 0.0f, 1.0f, 0.0f }, { 0.20f, 1.0f, 0.20f, 1.0f });   // Y green
            axis({ 0.0f, 0.0f, 1.0f }, { 0.30f, 0.45f, 1.0f, 1.0f });   // Z blue
        }

        // The PART frame's correction: the full part pose (W o P), driven to the
        // part mesh and tool so they stay locked.
        void probeCorrectionMatrix(float out[16]) const {
            Cam::Machine::Pose::identityMatrix(out);
            Cam::App::Project* project = activeProject();
            if (!project) { return; }
            // Apply the part pose whenever EITHER correction is real.  In the
            // work-frame == machine-frame model the work frame is fixed by
            // calibration (workCorrection stays identity/un-"established"), and the
            // measured part offset lives in probeCorrection -- so gating on
            // workEstablished() alone would wrongly drop a Set-Top / stage-probe
            // result and leave the part unmoved.
            const Cam::App::ProbeResult pose = project->totalPose();
            if (!pose.valid) { return; }
            correctionToMatrix(pose, out);
        }

        // out = a * b  (both column-major 4x4).
        static void mul4(const float a[16], const float b[16], float out[16]) {
            for (int col = 0; col < 4; col++) {
                for (int row = 0; row < 4; row++) {
                    float s = 0.0f;
                    for (int k = 0; k < 4; k++) { s += a[k * 4 + row] * b[col * 4 + k]; }
                    out[col * 4 + row] = s;
                }
            }
        }

        // Place every actor that belongs to the physical workpiece (meshes,
        // toolpaths, probe markers) at the part's pose, AND the tool preview with
        // it so they stay locked.  The pose is:
        //
        //   Mc = (chuck / A-axis pose) * (achievable probe correction)
        //
        // So the moment a probe fits, the part SNAPS to its measured pose (chuck
        // still at the mount angle -> Mc = the correction); then as the chuck
        // physically rotates to compensate, the part animates from crooked into
        // square (Mc -> ~identity).  Because the cut, the IK, and the telemetry
        // tool all use this same achievable correction, the tool stays on the
        // surface throughout.  Identity correction (un-probed) => Mc == M.
        void applyWorldTransforms() {

            float M[16];

            bool transformed = false;

            if (previewMode == PreviewMode::MachineSimulation) {
                transformed = currentPartMatrix(M);
            }
            else if (previewMode == PreviewMode::Execute) {
                transformed = executePartMatrix(M);
            }

            if (!transformed) { Cam::Machine::Pose::identityMatrix(M); }

            // Compose the achievable probe correction (only in a machine view --
            // the plain design view stays nominal).
            float Mc[16];
            if (transformed) {
                float C[16];
                probeCorrectionMatrix(C);
                mul4(M, C, Mc);
            }
            else {
                for (int i = 0; i < 16; i++) { Mc[i] = M[i]; }
            }

            for (Cam::Gui::World::Stage* v : materialViews) {

                if (!v) { continue; }

                if (v->partActor)        { v->partActor->setWorldTransform(Mc); }
                if (v->modelActor)       { v->modelActor->setWorldTransform(Mc); }
                if (v->deltaActor)       { v->deltaActor->setWorldTransform(Mc); }
                if (v->toolPath.actor)   { v->toolPath.actor->setWorldTransform(Mc); }
                if (v->probeMarkerActor) { v->probeMarkerActor->setWorldTransform(Mc); }
                if (v->probePlanActor)   { v->probePlanActor->setWorldTransform(Mc); }
            }

            // THE FRAMES are drawn DIRECTLY from their CoordinateSystem objects --
            // the lines are baked at each frame's pose, so the actors carry NO
            // transform (identity); what is on screen IS the CoordinateSystem.
            //
            // The PART frame rides the full part pose Mc (chuck o W o P) -- it tracks
            // the part's actual orientation -- so we rebuild it here, every
            // telemetry frame, from project->partFrame.  The WORK frame is static
            // between probes (built in syncAxisLines); the part rotates ABOUT it.
            float identity[16];
            Cam::Machine::Pose::identityMatrix(identity);

            if (Cam::App::Project* project = activeProject()) {
                project->partFrame = csFromColMajor(Mc).composedWith(userFrameCS(project));

                // The MACHINE -> WORK -> PART chain made explicit: the part expressed
                // IN the work frame.  Its origin's offset perpendicular to work-X is
                // the eccentricity probing measured.
                project->partInWorkFrame = project->partFrame.relativeTo(project->workFrame);

                if (partFrameActor && partFrameActor->lines) {
                    partFrameLines.clear();
                    drawFrame(project->partFrame, partFrameLines, 90.0f);

                    // Connector from the work origin (on the rotary axis) to the part
                    // origin: the off-centre offset, drawn straight from the two
                    // CoordinateSystem objects.
                    const Rev::Core::Pos3 wo = project->workFrame.apply({ 0.0f, 0.0f, 0.0f });
                    const Rev::Core::Pos3 po = project->partFrame.apply({ 0.0f, 0.0f, 0.0f });
                    const Rev::Core::Color link = { 0.90f, 0.80f, 0.25f, 0.7f };
                    partFrameLines.push_back({ wo.x, wo.y, wo.z, link });
                    partFrameLines.push_back({ po.x, po.y, po.z, link });

                    partFrameActor->lines->dirty = true;
                }
            }
            if (partFrameActor) { partFrameActor->setWorldTransform(identity); }
            if (lineActor)      { lineActor->setWorldTransform(identity); }
            if (axisLineActor)  { axisLineActor->setWorldTransform(identity); }

            // In Execute mode the tool preview is positioned from live telemetry
            // (already absolute CAD), so it is NOT transformed here.  In the sim
            // view it is sampled in nominal space and must ride the SAME corrected
            // pose Mc as the part, or it would float off the corrected toolpath.
            if (previewMode != PreviewMode::Execute) {
                if (toolPreviewActor) { toolPreviewActor->setWorldTransform(Mc); }
                if (spindlePreviewActor) { spindlePreviewActor->setWorldTransform(Mc); }
            }
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

            Cam::App::Stage* state = project ? project->displayedStage : nullptr;

            if (!state) { return frame; }

            const Cam::App::Model& m = state->model;

            // The axis/origin is shared across the project (propagateAxisToAllStates),
            // so the displayed state's frame is the project's frame.
            m.getOrthonormalAxisFrame(frame.X, frame.Y, frame.Z);

            if (m.hasAxisOrigin) { frame.origin = m.axisOrigin; }

            return frame;
        }

        // The reference origin that every streamed coordinate is expressed relative
        // to.  STRATEGY B: the work frame IS the machine frame, whose origin is the
        // ROTARY AXIS.  So the reference is the user frame's origin -- the part's
        // axis origin, which lies on the rotary axis -- NOT the stock top.  At Set
        // Origin this CAD axis point is declared to sit at the machine rotary axis
        // (Y/Z from calibration, X/A from the operator), so the whole part is placed
        // by its axis.  (The old "jog the tip to the stock top and zero" reference is
        // vestigial -- it dates to visually positioning the bit for the first test
        // cuts, before the axis/probe/tool were calibrated.)
        Rev::Core::Pos3 beginWorkOrigin(Cam::App::Project* project, const UserFrame& frame) {
            (void)project;
            return frame.origin;
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

        // The probe tool's slot (identified by Tool::Type::Probe), or -1.  Also
        // pushed to Air so its spindle interlock knows which loaded slot is a probe.
        int probeToolSlot() {
            const int slot = (app && app->toolLibrary()) ? app->toolLibrary()->probeSlot() : -1;
            Carvera::MachineLink::instance().setProbeSlot(slot);
            return slot;
        }

        // EXECUTE: build the typed-operation program from the IK-solved
        // path(s) of the previewed states.  This is the CAM side of Air's
        // operationProvider seam: it BUILDS, it never streams.  Arming,
        // preflight, validation, and streaming are entirely Air's business.
        // Assumes the machine's work-coordinate zero matches the CAD frame
        // (Air's preflight verifies an origin was actually set this session).
        // Build the typed operations for execution.  When onlyOpIndex >= 0, build
        // and return ONLY the operation at that index (the op-by-op provider path,
        // so each op picks up the latest probe correction); when < 0, build the
        // whole list (legacy/diagnostic).  The operation order is, per stage in
        // forward-machining order: probe op(s) (repeated kProbeRepeatCount times),
        // then the cut op.
        std::vector<Carvera::MachineLink::Operation> buildExecuteOperations(long onlyOpIndex = -1) {

            Carvera::MachineLink& link = Carvera::MachineLink::instance();

            Cam::App::Project* project = activeProject();

            if (!project) { return {}; }

            // Everything streamed to the machine is expressed in the USER FRAME:
            // origin = user axisOrigin, axes = user X/Y/Z.  The begin-work
            // origin (stock-top centre) is itself defined in that frame.
            const UserFrame frame = currentUserFrame(project);
            const Rev::Core::Pos3 cadBeginWork = beginWorkOrigin(project, frame);
            const Rev::Core::Pos3 beginWorkInFrame = frame.toFrame(cadBeginWork);

            // The probe-collection session is reset PER probe operation (in the
            // commit lambda below), not once per run: Air streams ops one at a
            // time, so each probe op's [PRB] replies must attribute to its own
            // targets.  The frame used to back-transform contacts is captured at
            // the same point.

            // We hand Air a queue of high-level intents, NOT flat G-code.  Air
            // owns tool-change orchestration (spindle-down, M6, wait for the
            // touch-off, return to the pre-change position) — so this builder no
            // longer emits M5/M6/dwells/clearance lifts around changes; it just
            // says "change to tool N" and lets Air handle the dangerous parts.
            // We now hand Air a queue of TYPED OPERATIONS (cut / probe), each
            // owning a tool and a list of typed paths (travel / cut / intersect).
            // Air translates these to its Step pipeline, owning the dangerous
            // policy (spindle only for cut paths, G38.2 for intersect, tool
            // changes, spindle-off boundaries) and narrating the queue as it goes.
            using Op  = Carvera::MachineLink::Operation;
            using Pth = Carvera::MachineLink::Path;
            using Wp  = Carvera::MachineLink::Waypoint;

            const double radToDeg = 57.29577951308232;

            std::vector<Op> ops;
            bool buildAborted = false;

            // Where the PART'S ORIGIN actually is in absolute machine space,
            // captured at "set work origin".  Every coordinate we emit is
            // ABSOLUTE machine = this origin + the part-space offset.  (The WCS
            // offset is zeroed on the controller, so G90 == machine absolute.)
            float omx = 0, omy = 0, omz = 0, ocx = 0, ocy = 0, ocz = 0;
            const bool haveOrigin = link.workOrigin(omx, omy, omz, ocx, ocy, ocz);
            const double omA = link.machineOriginA();

            if (!haveOrigin) {
                dbg("[Execute] no work origin set - cannot emit absolute coords; aborting");
                link.log("Execute: work origin not set; nothing to run.");
                return {};
            }

            // part space -> ABSOLUTE MACHINE.  The user frame defines the part's
            // axes (the "co"/"ax" gestures) with the begin-work point as the
            // part origin; we express the point relative to that, then add the
            // part-origin's machine location so the machine is driven absolutely.
            auto toMachine = [&](const Rev::Core::Pos3& cad, double aDeg) -> Wp {
                const Rev::Core::Pos3 inFrame = frame.toFrame(cad);
                return Wp{
                    (inFrame.x - beginWorkInFrame.x) + omx,
                    (inFrame.y - beginWorkInFrame.y) + omy,
                    (inFrame.z - beginWorkInFrame.z) + omz,
                    aDeg + omA
                };
            };

            // Probe operation (a break to locate the part) BEFORE the stage's
            // cut: for each target, rapid to the standoff point, then drive
            // slowly along -normal through the nominal point (G38.2) until touch.
            auto appendProbeOp = [&](Cam::App::Stage* state,
                                     const Cam::App::ProbeResult* drive) {

                if (!state || !state->probe.enabled || state->probe.targets.empty()) { return; }

                // Build a world-space ToolPath for the probe approaches, then run
                // it through the SAME IK solver cuts use (getMachineToolPath ->
                // IKSolver::solve) so probe coordinates are produced identically:
                // the part's rotary axis is oriented and the position rotated into
                // the machine frame.  Without this the probe ignored the 4th axis
                // entirely and drove to the wrong place.
                //
                // toolDirection = the outward surface normal: the probe shaft
                // points out of the surface, exactly as a cutter axis points away
                // from the material, so the solver orients the feature to the tool.
                Cam::App::ToolPath probePath;

                // SAFETY INVARIANT: the probe is driven by a fitted correction
                // ONLY on a TRUSTED iterative re-probe (drive != null; the caller
                // has already gated on trustedForReprobe).  Pass 0 -- and any
                // untrusted pass -- has drive == null and moves to fixed, origin-
                // referenced NOMINAL positions, so the fragile probe can never be
                // swung into a bizarre orientation by a wild/uncertain correction.
                // When driven, the targets are mapped by the correction so the
                // probe approaches the part WHERE IT NOW IS.  The session entries
                // record the RAW nominal/normal plus the commanded rotary angle;
                // the pose solver canonicalizes the contact back to A=0 and fits
                // the raw nominal -> measured, so a driven re-probe just adds more
                // points to the global solve (no residual/compose bookkeeping).
                struct KeptTarget {
                    Rev::Core::Pos3 point;     Rev::Core::Pos3 normal;      // driven (physical approach)
                    Rev::Core::Pos3 rawPoint;  Rev::Core::Pos3 rawNormal;   // raw nominal (recorded)
                };
                std::vector<KeptTarget> keptTargets;

                for (const Cam::App::ProbeTarget& t : state->probe.targets) {

                    const float nlen = t.normal.pythag();
                    if (nlen < 1e-4f) {
                        dbg("[Probe] skipping target with zero normal at "
                            "(%.2f, %.2f, %.2f)", t.point.x, t.point.y, t.point.z);
                        continue;
                    }
                    const Rev::Core::Pos3 rawN = t.normal * (1.0f / nlen);
                    Rev::Core::Pos3 n = rawN;
                    Rev::Core::Pos3 p = t.point;

                    // Trusted re-probe: place the target where the correction says
                    // the part now is (point mapped, normal rotated + renormalized).
                    if (drive) {
                        p = drive->apply(t.point);
                        Rev::Core::Pos3 dn = drive->applyDirection(n);
                        const float dl = dn.pythag();
                        if (dl > 1e-5f) { n = dn * (1.0f / dl); }
                    }

                    keptTargets.push_back({ p, n, t.point, rawN });

                    Cam::App::ToolPathPoint a;   // standoff (outside surface, +normal)
                    a.position      = p + n * static_cast<float>(t.standoff);
                    a.toolDirection = n;
                    a.rapid = true;  a.cutting = false;
                    probePath.points.push_back(a);

                    Cam::App::ToolPathPoint b;   // through the point (-normal, overtravel)
                    b.position      = p - n * static_cast<float>(t.overtravel);
                    b.toolDirection = n;
                    b.rapid = false; b.cutting = false;
                    probePath.points.push_back(b);
                }

                if (probePath.points.empty()) { return; }

                const Cam::Machine::MachineDefinition machineDef = buildMachineDefinition();
                const bool hasRotary = !machineDef.part.dof.freeRotations.empty();

                const Cam::Machine::MachineToolPath solved =
                    Cam::Machine::IKSolver::solve(probePath, machineDef);

                // A probe operation ALWAYS task-switches to the probe tool first
                // (resolved by Tool::Type::Probe, not a magic slot).  Air orchestrates
                // the M6 like any tool change and its spindle interlock latches as
                // soon as the probe is loaded, so the probe can never spin.
                Op op = Op::probe(probeToolSlot(), "Wired probe");

                // Points come in (standoff, through) pairs, one pair per target.
                for (size_t k = 0; (2 * k + 1) < solved.points.size(); k++) {

                    const Cam::Machine::MachinePose& sPose = solved.points[2 * k];
                    const Cam::Machine::MachinePose& tPose = solved.points[2 * k + 1];

                    // On a rotary machine an un-achievable orientation means the
                    // solved position is bogus -- skip it.  On a 3-axis machine the
                    // direction residual is expected (no orientation needed) and
                    // the position is still correct, so don't skip.
                    if (hasRotary && (!sPose.valid() || !tPose.valid())) {
                        dbg("[Probe] target %zu unreachable on this machine's rotary "
                            "axis (IK invalid) - skipping", k);
                        continue;
                    }

                    const Wp standoffM = toMachine(sPose.toolWorldPose.position, sPose.rotaryAngle * radToDeg);
                    const Wp throughM  = toMachine(tPose.toolWorldPose.position, tPose.rotaryAngle * radToDeg);

                    dbg("[Probe] target %zu | standoff machine(%.2f,%.2f,%.2f A%.1f) "
                        "-> through machine(%.2f,%.2f,%.2f A%.1f)",
                        k, standoffM.x, standoffM.y, standoffM.z, standoffM.a,
                        throughM.x, throughM.y, throughM.z, throughM.a);

                    Pth travel = Pth::travel();
                    travel.points.push_back(standoffM);
                    op.paths.push_back(travel);

                    Pth inter = Pth::intersect(100.0);   // slow probe feed (mm/min)
                    inter.points.push_back(throughM);
                    op.paths.push_back(inter);

                    // RETRACT before any lateral motion.  After contact the probe
                    // is at/below the surface; rapiding straight to the next
                    // standoff from there would drag the stylus across the part.
                    // So lift back along the approach to THIS standoff first --
                    // exactly how linking lifts to clearance before repositioning
                    // between cut moves.  The next target's travel then happens at
                    // standoff height, clear of the surface.
                    Pth retract = Pth::travel();
                    retract.points.push_back(standoffM);
                    op.paths.push_back(retract);

                    // Record this target so the matching [PRB] reply (the moves
                    // stream in this order) can be back-transformed and fitted.
                    if (k < keptTargets.size()) {
                        ProbeSessionEntry entry;
                        entry.stage             = state;
                        entry.nominalCad        = keptTargets[k].rawPoint;
                        entry.normalCad         = keptTargets[k].rawNormal;
                        entry.commandedAngleDeg = tPose.rotaryAngle * radToDeg;
                        probeSession_.push_back(entry);
                    }
                }

                if (op.paths.empty()) { return; }   // nothing probeable

                ops.push_back(std::move(op));
            };

            // Cut operation from the stage's toolpath, grouping consecutive poses
            // into Travel (rapid) / Cut (feed) paths.
            auto appendCutOp = [&](Cam::App::Stage* state) {

                if (buildAborted) { return; }
                if (!state || !state->hasToolPath) { return; }

                const Cam::Machine::MachineToolPath* path = getMachineToolPath(state);
                if (!path || path->empty()) { return; }

                const std::string toolName = state->toolPath.toolName;
                const int slot = toolNumber(toolName);

                if (toolName.empty() || slot <= 0) {
                    dbg("[Execute] cut tool '%s' unresolved (slot %d) - aborting",
                        toolName.c_str(), slot);
                    buildAborted = true;
                    return;
                }

                const double feed = state->toolPath.feedRate > 0.0
                    ? state->toolPath.feedRate
                    : 250.0;

                Op op = Op::cut(slot, toolName, 12000.0);

                Pth  current;
                bool haveCurrent  = false;
                bool currentRapid = true;

                auto flush = [&]() {
                    if (haveCurrent && !current.points.empty()) { op.paths.push_back(current); }
                    haveCurrent = false;
                };

                for (const Cam::Machine::MachinePose& p : path->points) {

                    const double aDeg = p.rotaryAngle * radToDeg;

                    if (!haveCurrent || p.rapid != currentRapid) {
                        flush();
                        current      = p.rapid ? Pth::travel() : Pth::cut(feed);
                        currentRapid = p.rapid;
                        haveCurrent  = true;
                    }

                    current.points.push_back(toMachine(p.toolWorldPose.position, aDeg));
                }

                flush();

                if (!op.paths.empty()) { ops.push_back(std::move(op)); }
            };

            auto stageHasProbe = [](Cam::App::Stage* s) {
                return s && s->probe.enabled && !s->probe.targets.empty();
            };

            // Operations are addressed by a stable index.  We always advance the
            // cursor for every operation that EXISTS in the plan, but only build
            // (commit) the one matching onlyOpIndex (or all, when < 0).  This is
            // what lets Air pull ops one at a time and have each built fresh.
            long opCursor = 0;

            auto commitProbe = [&](Cam::App::Stage* state, int passIndex) {
                if (onlyOpIndex < 0 || opCursor == onlyOpIndex) {
                    // Reset the probe session for THIS op so its [PRB] replies
                    // attribute to its own targets, and capture the frame for the
                    // contact back-transform.
                    probeSession_.clear();
                    probeFillIndex_        = 0;
                    probeFrameOrigin_      = frame.origin;
                    probeFrameX_           = frame.X;
                    probeFrameY_           = frame.Y;
                    probeFrameZ_           = frame.Z;
                    probeBeginWorkInFrame_ = beginWorkInFrame;

                    // PART-OFFSET pass only.  The work frame (rotary axis) is fixed by
                    // machine calibration, so the stage probe no longer tilts to infer
                    // the axis -- it probes the part at its nominal orientation and
                    // fits the part's residual offset WITHIN that known work frame.
                    (void)passIndex;
                    probeSessionBase_.reset();
                    probeSessionComposed_      = false;
                    probeSessionConfirm_       = true;    // always the part-offset fit
                    probeSessionCalibComplete_ = false;

                    appendProbeOp(state, /*drive*/ nullptr);
                }
                opCursor++;
            };

            auto commitCut = [&](Cam::App::Stage* state) {
                if (onlyOpIndex < 0 || opCursor == onlyOpIndex) {
                    appendCutOp(state);
                }
                opCursor++;
            };

            // Per stage, in forward-machining order: the probe op(s) -- repeated
            // kProbeRepeatCount times so a re-probe confirms/refines the pose --
            // then the cut op.
            auto appendState = [&](Cam::App::Stage* state) {
                if (!state) { return; }
                if (stageHasProbe(state)) {
                    for (int r = 0; r < kProbeRepeatCount; r++) { commitProbe(state, r); }
                }
                if (state->hasToolPath) { commitCut(state); }
            };

            const std::vector<Cam::App::Stage*> sequence =
                ToolPathPreviewTimeline::previewSequence(project);

            if (!sequence.empty()) {
                for (Cam::App::Stage* state : sequence) {
                    appendState(state);
                }
            }
            else {
                // No cutting toolpaths in the sequence.  Still allow a probe-only
                // material state (no delta/toolpath) to run on its own -- e.g. a
                // single bench test probe.  previewSequence() filters to stages
                // with a toolpath, so fall back to the toolpath preview state, or
                // the displayed stage if it carries a probe.
                Cam::App::Stage* fallback = materialStateWithToolPathForPreview(project);
                if (!fallback && project->displayedStage &&
                    project->displayedStage->hasProbe()) {
                    fallback = project->displayedStage;
                }
                appendState(fallback);
            }

            if (buildAborted) {
                dbg("[Execute] operation build aborted - returning no operations");
                link.log("Execute: operation build aborted (unresolved tool); nothing to run.");
                return {};
            }

            dbg("[Execute] built %zu operation(s) for Air", ops.size());

            return ops;
        }

        void syncAllMaterialViews() {

            rebuildPreviewTimelineIfNeeded();

            Cam::App::Project* project = activeProject();

            for (Cam::Gui::World::Stage* view : materialViews) {

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

            representedDisplayedState = project->displayedStage;
            representedWorkingState = project->workingStage;
            representedStateCount = project->stages.size();

            project->linkStageToolPaths();

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

            // Keep Air's spindle interlock in step with the library: push which slot
            // holds the probe (by type) so a manually-loaded probe inhibits the
            // spindle even before any probe operation is built.
            probeToolSlot();

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

            Cam::Gui::World::Stage* worldState = displayedMaterialView();

            if (!worldState || !worldState->pickActor) { return; }

            Cam::App::Model* editable = selectionModel();

            if (!editable) { return; }

            View3d::Hit hit;

            if (!view3d->hitTest(e.mouse.pos, hit)) { return; }
            if (hit.actor != worldState->pickActor) { return; }

            size_t tri = hit.triangleId;

            if (tri >= editable->render.triangleFaceIds.size()) { return; }

            size_t faceId = editable->render.triangleFaceIds[tri];

            // If a face reference (e.g. an extrude's end face) is active in the
            // tree, this ctrl+click fills it rather than toggling selection.
            Cam::App::Project* project = activeProject();

            if (project && project->hasActiveFaceReference()) {
                project->assignActiveFaceReference(faceId);
                sync(e);
                notifyStateChanged(e);
                return;
            }

            editable->toggleFace(faceId);

            sync(e);
        }

        void selectAxisPickFaceAtMouse(Event& e) {

            if (!app || !view3d) { return; }

            if (!displayedModelIsEditable()) {
                dbg("Select the working state to pick axis reference faces.");
                return;
            }

            Cam::Gui::World::Stage* worldState = displayedMaterialView();

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

            Cam::Gui::World::Stage* worldState = displayedMaterialView();

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

            Cam::Gui::World::Stage* worldState = displayedMaterialView();

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
                    Cam::Gui::World::Stage::NoAxisPickHover
                );
                worldState->syncAxisPickMarkers();
                return;
            }

            size_t hitIndex = Cam::Gui::World::Stage::NoAxisPickHover;

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

            for (Cam::App::Stage* state : project->stages) {

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

            Cam::Gui::World::Stage* worldState = displayedMaterialView();

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

        // Start an extrude feature from the selected (profile) face. The end
        // face is left unset and auto-activated, so the next ctrl+click defines
        // the end plane (see selectFaceAtMouse).
        bool extrudeFeature(Event& e) {

            if (!app || !app->beginExtrude()) {
                dbg("extrude feature failed: select a profile face first");
                return false;
            }

            sync(e);

            dbg("extrude feature started - ctrl+click a face to set the end plane");

            notifyStateChanged(e);

            return true;
        }

        // Thread-mill the selected cylindrical hole: shrink it to its pre-thread
        // bore and auto-select the Thread Mill toolpath strategy (the operation
        // seeds the callout from the hole; the user refines it in the tree).
        bool threadMillFeature(Event& e) {

            if (!app || !app->beginThreadMill()) {
                dbg("thread mill failed: select a cylindrical hole face first");
                return false;
            }

            sync(e);

            dbg("thread mill started on selected hole");

            notifyStateChanged(e);

            return true;
        }

        bool commitWorkingState(Event& e) {

            if (!app || !app->commitWorkingStage()) {
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

            Cam::Gui::World::Stage* worldState =
                editing ? displayedMaterialView() : nullptr;

            size_t newFaceId = Cam::Gui::World::Stage::NoHoveredFace;

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

            for (Cam::Gui::World::Stage* view : materialViews) {

                if (!view) { continue; }

                size_t faceId = (view == worldState)
                    ? newFaceId
                    : Cam::Gui::World::Stage::NoHoveredFace;

                if (view->setHoveredFace(faceId)) {
                    view->applyFaceColors();
                    if (view->partActor && view->partActor->mesh) {
                        view->partActor->mesh->dirty = true;
                    }
                }
            }
        }

        void mouseMove(Event& e) override {

            // Remember the cursor position so keyboard shortcuts that act "at the
            // cursor" (e.g. P to drop a probe point) know where to aim.
            lastMousePos_ = e.mouse.pos;
            haveMousePos_ = true;

            updateAxisPickHover(e);
            updateFaceHover(e);

            Box::mouseMove(e);
        }

        // Add a probe target at the surface point under the cursor, with the
        // outward surface normal of the face there.  Targets attach to the
        // currently displayed stage's probe sub-component (auto-enabling it).
        // Coordinates are in the model/CAD frame -- the same frame toolpath
        // points use -- so the executor's UserFrame transform applies uniformly.
        bool addProbePointAtMouse(const Rev::Core::Pos& mousePos, Event& e) {

            if (!app || !view3d) { return false; }

            // Picking uses the (invisible, selectable) pick actor, which is only
            // enabled on the editable working state -- same gate as face picking.
            if (!displayedModelIsEditable()) {
                dbg("[Probe] select the working state to add probe points");
                return false;
            }

            Cam::App::Stage* state = displayedState();
            if (!state) { return false; }

            Cam::Gui::World::Stage* worldState = displayedMaterialView();
            if (!worldState || !worldState->pickActor) { return false; }

            Cam::App::Model* model = selectionModel();
            if (!model) { return false; }

            // GPU pick: gives BOTH the world-space surface point and the triangle
            // (-> face -> outward normal) under the cursor.
            View3d::Hit hit;
            if (!view3d->hitTest(mousePos, hit)) {
                dbg("[Probe] cursor not over the part");
                return false;
            }
            if (hit.actor != worldState->pickActor) { return false; }
            if (hit.triangleId >= model->render.triangleFaceIds.size()) { return false; }

            const size_t faceId = model->render.triangleFaceIds[hit.triangleId];

            Cam::App::ProbeTarget target;
            target.point  = hit.point;
            target.normal = model->faceNormal(faceId);

            state->probe.enabled = true;       // pressing P implies "probe this stage"
            state->probe.targets.push_back(target);
            state->probe.clearResult();        // new target -> stale fit

            if (Cam::App::Project* project = activeProject()) { project->markDirty(); }

            dbg("[Probe] added point %zu at (%.2f, %.2f, %.2f) n(%.2f, %.2f, %.2f)",
                state->probe.targets.size() - 1,
                target.point.x, target.point.y, target.point.z,
                target.normal.x, target.normal.y, target.normal.z);

            sync(e);
            notifyStateChanged(e);
            return true;
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

            Cam::App::Stage* material = displayedState();

            if (material && material->toolPath.hasSliceFace()) {
                material->toolPath.clearSlicePlane();
                changed = true;
            }

            Cam::Gui::World::Stage* worldState = displayedMaterialView();

            if (worldState) {
                worldState->setAxisPickHoveredCandidate(
                    Cam::Gui::World::Stage::NoAxisPickHover
                );
            }

            if (changed) {
                sync(e);
                notifyStateChanged(e);
            }
        }

        // Last known cursor position (updated on mouseMove), so keyboard
        // shortcuts can act at the cursor even though a key event carries no
        // fresh mouse coordinate.
        Rev::Core::Pos lastMousePos_{};
        bool           haveMousePos_ = false;

        void keyDown(Event& e) override {

            // Give children (e.g. a focused offset/number input) the event FIRST,
            // then bail if one of them consumed it. This inverts Rev's default
            // bubble-down order so our view-level shortcuts defer to focused
            // widgets — Enter inside an input edits that input instead of
            // committing the working state, etc.
            Box::keyDown(e);
            if (!e.propagate) { return; }

            // If a text field is focused anywhere (including a sibling panel that
            // is dispatched after us), let it own the keyboard. We leave the event
            // un-consumed so it still reaches that field — without this, our
            // shortcuts would steal Enter/Delete/letters from the focused input.
            if (shared && shared->focusedText) { return; }

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

            // P: drop a probe point at the cursor on the displayed stage.
            if (e.keyboard.key == "p") {
                if (haveMousePos_) { addProbePointAtMouse(lastMousePos_, e); }
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
        }
    };
}