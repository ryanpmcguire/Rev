module;

#include <string>
#include <cstdio>
#include <cmath>
#include <memory>
#include <functional>
#include <optional>

export module Cam.Gui.ProbeCalibrationWindow;

import Rev.Window;
import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Dropdown;
import Rev.Element.Button;
import Rev.Element.NumberInput;

import Cam.App;
import Cam.App.Tool;
import Cam.App.ToolLibrary;
import Cam.App.ProbeCalibration;
import Cam.Gui.Theme;
import Cam.Gui.Form;

import CarveraAir;

// ------------------------------------------------------------------
// Cam::Gui::ProbeCalibrationWindow
//
// Child window of the tool-settings window (blue "Calibrate" button).  Drives a
// calibration meta-program against a rectangular gauge/stock to infer the
// probe's stylus radius, then stores it on the tool.  The routine:
//
//   1. LEVEL  -- probe the flat top face to get a true A=0 zero reference.
//   2. SAMPLE -- tilt across [angleMin, angleMax] (both signs) and probe
//                `samplePoints` locations across `linearBound` at each.
//   3. FIT    -- solve the contact model for the stylus radius.
//   4. REVIEW -- show it; SAVE to the tool or REDO.
//
// The window owns the SPEC + UI + state + result (Cam::App::ProbeCalibration).
// The MACHINE DRIVER is injected via `onRunRequested` -- the world-view / Air
// layer wires it to actually move the machine and feed contacts back (it then
// fills `calib.samples`, calls `calib.fit()`, and calls `calibrationComplete()`).
// This keeps the window testable and machine-agnostic.
// ------------------------------------------------------------------

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;
    using namespace Rev::Appearance;   // rgba(), sColor

    namespace ProbeCalibrationLayout {

        Style Root = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct, 100_pct }
        };

        Style Header = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { 100_pct },
            .padding = { .left = 20_px, .right = 20_px, .top = 16_px, .bottom = 12_px }
        };

        Style Body = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { Grow() }
        };

        Style Note = {
            .margin = { 4_px, 4_px, 0_px, 8_px },
            .text = { .size = 12_px }
        };

        Style Footer = {
            .layout = { Axis::Horizontal, Align::End, Align::Center, Wrap::False },
            .size = { 100_pct },
            .padding = { .left = 20_px, .right = 20_px, .top = 12_px, .bottom = 16_px }
        };

        Style FooterButton = {
            .margin = { .left = 8_px }
        };
    }

    struct ProbeCalibrationWindow : public Rev::Window {

        Cam::App::AppState* app = nullptr;
        std::string toolName;

        Cam::App::ProbeCalibration calib;

        // Run-button state machine: Set Origin (blue) -> Start (green) -> Stop (red).
        // The button only changes COLOUR (background) + label; its border/shape stay
        // the rich primary look.  Jogging after Set Origin reverts it to Set Origin.
        enum class RunState { NeedsOrigin, Ready, Running };
        RunState   runState  = RunState::NeedsOrigin;
        Button*    runButton = nullptr;
        Style      runBg{};   // background-color-only override (Null => primary blue)

        // Origin reference for "moved since Set Origin" detection (live telemetry).
        bool   originSet = false;
        double originX = 0, originY = 0, originZ = 0, originA = 0;
        double liveX = 0, liveY = 0, liveZ = 0, liveA = 0;
        int    originSettle = 0;   // frames to ignore move-detect after the WCS re-zero
        std::shared_ptr<bool> alive;

        // -- Machine driver: flat reference, then tilt sampling --------
        enum class DriverPhase { None, Reference, Sampling };
        DriverPhase driverPhase = DriverPhase::None;
        double drvFlatZ = 0.0;                            // measured flat-face machine Z
        std::vector<std::pair<double, double>> drvOrder;  // (angleDeg, lateral) per probe
        std::size_t drvIdx = 0;

        static constexpr double kClearance      = 3.0;    // standoff above expected (mm)
        static constexpr double kTravel         = 9.0;    // plunge from standoff (mm)
        static constexpr double kProbeFeed      = 100.0;  // mm/min (slow approach)
        static constexpr double kAxisDepthGuess = 10.0;   // mm (standoff term only; small)

        Dropdown*    artifactDropdown  = nullptr;
        NumberInput* artifactSizeInput = nullptr;
        NumberInput* linearBoundInput  = nullptr;
        NumberInput* angleMinInput     = nullptr;
        NumberInput* angleMaxInput     = nullptr;
        NumberInput* sampleAnglesInput = nullptr;
        NumberInput* samplePointsInput = nullptr;

        Text* planText   = nullptr;
        Text* statusText = nullptr;

        // Injected machine driver: the world-view / Air layer wires this to run the
        // level + tilt-and-probe sequence for `calib`, fill calib.samples, fit, and
        // call calibrationComplete().  Unset => the window explains it isn't wired.
        std::function<void(ProbeCalibrationWindow&)> onRunRequested;
        // Abort an in-progress run (Stop).
        std::function<void(ProbeCalibrationWindow&)> onStopRequested;
        // Fired after a successful Save, so the settings window refreshes its copy.
        std::function<void(double /*radius*/)> onCalibrated;
        std::function<void(Event&)> onClosed;

        ProbeCalibrationWindow(Rev::Window* owner, const std::string& probeToolName)
            : Rev::Window(
                owner,
                {
                    .name = probeToolName + " - Calibrate Probe",
                    .size = { .width = 460, .height = 560 },
                    .minimizeButton = false,
                    .maximizeButton = false
                }
            ) {
            toolName = probeToolName;

            if (owner && owner->shared) { shared->state = owner->shared->state; }

            app = Cam::App::AppState::Get(shared->state);

            style->layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
            style->size = { .width = 100_pct, .height = 100_pct };

            buildUi();

            // Watch live machine position so the button reverts to "Set Origin" the
            // moment the operator jogs after setting it.  Guarded so the listener
            // is inert once this window is gone.
            alive = std::make_shared<bool>(true);
            {
                auto a = alive;
                Carvera::MachineLink::instance().onTelemetryFrame(
                    [this, a](Carvera::MachineLink::TelemetryEvent& e) {
                        if (!*a) { return; }
                        onTelemetry(e);
                    });
                Carvera::MachineLink::instance().onProbe(
                    [this, a](Carvera::MachineLink::ProbeEvent& e) {
                        if (!*a) { return; }
                        onCalibContact(e);
                    });
            }

            setTitle(probeToolName + " - Calibrate Probe");

            if (owner) { setPos(owner->details.x + 60, owner->details.y + 70); }
            else       { setPos(280, 140); }

            show();
            refresh(event);
        }

        Cam::App::Tool* tool() {
            return app ? app->toolLibrary()->find(toolName) : nullptr;
        }

        // Slot of this probe tool (its library position), pushed to Air so the
        // spindle interlock treats that loaded slot as a probe.  No magic number.
        int probeOpSlot() {
            int slot = (app && app->toolLibrary()) ? app->toolLibrary()->indexOf(toolName) : 0;
            if (slot <= 0 && app && app->toolLibrary()) { slot = app->toolLibrary()->probeSlot(); }
            Carvera::MachineLink::instance().setProbeSlot(slot);
            return slot;
        }

        // -- Build -----------------------------------------------------

        void buildUi() {

            Box* root = new Box(
                this,
                Theme::withSettingsDialog({ &ProbeCalibrationLayout::Root }),
                "CalibRoot"
            );

            Box* header = new Box(
                root,
                Theme::layer({ &ProbeCalibrationLayout::Header }, { &Theme::Styles::SettingsHeader }),
                "Header"
            );
            new Text(header, "PROBE CALIBRATION",
                     Theme::layer({}, { &Theme::Styles::SettingsHeaderEyebrow }));
            new Text(header, toolName,
                     Theme::layer({}, { &Theme::Styles::SettingsHeaderTitle }));

            Box* body = new Box(
                root,
                Theme::layer(
                    { &ProbeCalibrationLayout::Body, &Theme::Styles::SettingsBody },
                    { &Theme::Styles::Text }
                ),
                "Body"
            );
            Box* col = Form::column(body, "Content");

            new Text(
                col,
                "Mount a rectangular gauge / stock. Calibration levels the top face "
                "for a true zero, then tilts and probes it across a range of angles "
                "to infer the stylus radius. Done once per probe.",
                Theme::layer({ &ProbeCalibrationLayout::Note }, { &Theme::Styles::MutedText })
            );

            // Artifact
            Form::section(col, "REFERENCE ARTIFACT");
            artifactDropdown = new Dropdown(col, {
                .label = "Artifact",
                .options = {
                    { "Flat gauge block",   "gauge"    },
                    { "Reference cylinder", "cylinder" }
                },
                .placeholder = "Select artifact",
                .value = Cam::App::ProbeCalibration::artifactToString(calib.artifact)
            });
            artifactDropdown->onChange = [this](Event& e) { updatePlan(e); };
            artifactSizeInput = Form::numberField(Form::row(col, "SizeRow"), "Artifact size (mm)", "10");

            // Linear sampling
            Form::section(col, "SAMPLING");
            Box* sampRow = Form::row(col, "SampRow");
            linearBoundInput  = Form::numberField(sampRow, "Linear bound (mm)", "20");
            samplePointsInput = Form::numberField(sampRow, "Sample points", "3");

            // Angular sampling
            Form::section(col, "ANGLES");
            Box* angRow = Form::row(col, "AngRow");
            angleMinInput = Form::numberField(angRow, "Min angle (deg)", "5");
            angleMaxInput = Form::numberField(angRow, "Max angle (deg)", "30");
            sampleAnglesInput = Form::numberField(Form::row(col, "AngCountRow"), "Sample angles", "5");

            // Plan + result
            Form::section(col, "PLAN");
            planText = new Text(col, "", Theme::layer({ &ProbeCalibrationLayout::Note }, { &Theme::Styles::MutedText }));
            Form::section(col, "STATUS");
            statusText = new Text(col, "", Theme::layer({ &ProbeCalibrationLayout::Note }, { &Theme::Styles::MutedText }));

            // Seed inputs from the spec, then live-update the plan.
            artifactSizeInput->setValue(calib.artifactSize);
            linearBoundInput->setValue(calib.linearBound);
            samplePointsInput->setValue(static_cast<double>(calib.samplePoints));
            angleMinInput->setValue(calib.angleMin);
            angleMaxInput->setValue(calib.angleMax);
            sampleAnglesInput->setValue(static_cast<double>(calib.sampleAngles));

            bindLive(artifactSizeInput);
            bindLive(linearBoundInput);
            bindLive(samplePointsInput);
            bindLive(angleMinInput);
            bindLive(angleMaxInput);
            bindLive(sampleAnglesInput);

            // Footer
            Box* footer = new Box(
                root,
                Theme::layer({ &ProbeCalibrationLayout::Footer }, { &Theme::Styles::SettingsFooter }),
                "Footer"
            );

            Button* closeButton = new Button(footer, Button::Params::Secondary("Close"), { &ProbeCalibrationLayout::FooterButton });
            closeButton->onClick([this](Event& e) { requestClose(&e); e.propagate = false; });

            Button* redoButton = new Button(footer, Button::Params::Secondary("Redo"), { &ProbeCalibrationLayout::FooterButton });
            redoButton->onClick([this](Event& e) { redo(e); e.propagate = false; });

            Button* saveButton = new Button(footer, Button::Params::Secondary("Save"), { &ProbeCalibrationLayout::FooterButton });
            saveButton->onClick([this](Event& e) { saveResult(e); e.propagate = false; });

            // The stateful action button.  Built as the rich primary (border/shape/
            // label styling), then a background-only override (`runBg`, added last so
            // it wins) recolours it green/red per state without touching the border.
            runButton = new Button(footer, Button::Params::Primary("Set Origin"), { &ProbeCalibrationLayout::FooterButton });
            runButton->styles.add(&runBg);
            runButton->onClick([this](Event& e) { onRunButton(e); e.propagate = false; });

            updatePlan(event);
            updateStatus();
            updateRunButton();
        }

        void bindLive(NumberInput* input) {
            if (!input) { return; }
            input->onTextInput([this](Event& e) { updatePlan(e); });
            input->onValueChange = [this](Event& e, std::optional<double>) { updatePlan(e); };
        }

        // -- Spec / plan / status -------------------------------------

        void captureSpec() {
            if (artifactDropdown)  { calib.artifact = Cam::App::ProbeCalibration::artifactFromString(artifactDropdown->params.value); }
            if (artifactSizeInput) { calib.artifactSize = artifactSizeInput->valueOr(calib.artifactSize); }
            if (linearBoundInput)  { calib.linearBound  = linearBoundInput->valueOr(calib.linearBound); }
            if (angleMinInput)     { calib.angleMin     = angleMinInput->valueOr(calib.angleMin); }
            if (angleMaxInput)     { calib.angleMax     = angleMaxInput->valueOr(calib.angleMax); }
            if (sampleAnglesInput) { calib.sampleAngles = static_cast<int>(std::lround(sampleAnglesInput->valueOr(calib.sampleAngles))); }
            if (samplePointsInput) { calib.samplePoints = static_cast<int>(std::lround(samplePointsInput->valueOr(calib.samplePoints))); }
            calib.clampSpec();
        }

        void updatePlan(Event&) {
            captureSpec();
            if (!planText) { return; }
            char buf[220];
            const int orientations = static_cast<int>(calib.angleSchedule().size());
            std::snprintf(
                buf, sizeof(buf),
                "%d orientations x %d points = %d contacts; %.0f-%.0f deg over %.0f mm.",
                orientations, calib.samplePoints, calib.plannedContacts(),
                calib.angleMin, calib.angleMax, calib.linearBound
            );
            planText->content = buf;
        }

        void updateStatus() {
            if (!statusText) { return; }

            Cam::App::Tool* t = tool();
            char cur[96] = "";
            if (t) {
                std::snprintf(cur, sizeof(cur), "Stored: %.3f mm (%s).",
                    t->probe.stylusRadius, t->probe.calibrated ? "calibrated" : "assumed");
            }

            if (calib.haveResult) {
                char buf[200];
                std::snprintf(buf, sizeof(buf),
                    "Inferred stylus radius %.3f +/- %.3f mm (residual %.4f). Save to store. %s",
                    calib.resultRadius, calib.resultRadiusSigma, calib.resultResidual, cur);
                statusText->content = buf;
            }
            else {
                statusText->content =
                    Cam::App::ProbeCalibration::phaseLabel(calib.phase) + std::string("  ") + cur;
            }
        }

        // Called by the injected driver once samples are collected + fit.
        void calibrationComplete() {
            calib.phase = calib.haveResult ? Cam::App::ProbeCalibration::Phase::Review
                                           : Cam::App::ProbeCalibration::Phase::Failed;
            runState = RunState::Ready;   // probing moved the machine; telemetry will
            updateRunButton();            // revert to Set Origin if it's off-origin
            updateStatus();
            refresh(event);
        }

        // -- Run-button state machine ----------------------------------

        // Recolour (background only) + relabel the action button for the state.
        void updateRunButton() {
            if (!runButton) { return; }
            const char* label = "Set Origin";
            switch (runState) {
                case RunState::NeedsOrigin:
                    runBg.background.color = sColor::Null();        // -> primary blue
                    label = "Set Origin";
                    break;
                case RunState::Ready:
                    runBg.background.color = rgba(40, 170, 90, 1.0);   // green
                    label = "Start";
                    break;
                case RunState::Running:
                    runBg.background.color = rgba(214, 64, 64, 1.0);   // red
                    label = "Stop";
                    break;
            }
            if (runButton->labelText) { runButton->labelText->content = label; }
            refresh(event);
        }

        void onRunButton(Event&) {
            switch (runState) {
                case RunState::NeedsOrigin: doSetOrigin(); break;
                case RunState::Ready:       startRun();    break;
                case RunState::Running:     stopRun();     break;
            }
        }

        // Set Origin: the canonical capture + WCS zero (so G90 == machine absolute,
        // matching the validated cut/probe path).  Read the captured machine origin
        // back for the driver waypoints + move detection.
        void doSetOrigin() {
            auto& link = Carvera::MachineLink::instance();
            link.setWorkOrigin();

            float mx, my, mz, ma;
            if (link.machineOrigin(mx, my, mz, ma)) {
                originX = mx; originY = my; originZ = mz; originA = ma;
                originSet = true;
                originSettle = 12;   // let telemetry settle past the WCS re-zero jump
                runState = RunState::Ready;
                statusText->content = "Origin set. Press Start to run the calibration.";
            }
            else {
                statusText->content = "Set Origin failed -- connect and wait for a position.";
            }
            updateRunButton();
            refresh(event);
        }

        void startRun() {
            captureSpec();
            calib.reset();
            runState = RunState::Running;
            updateRunButton();
            runDriver();
            refresh(event);
        }

        void stopRun() {
            Carvera::MachineLink::instance().stop();
            driverPhase = DriverPhase::None;
            if (onStopRequested) { onStopRequested(*this); }
            calib.phase = Cam::App::ProbeCalibration::Phase::Idle;
            runState = RunState::Ready;
            updateRunButton();
            updateStatus();
            refresh(event);
        }

        // Live position; revert to "Set Origin" if jogged after it was set.
        void onTelemetry(Carvera::MachineLink::TelemetryEvent& e) {
            liveX = e.x; liveY = e.y; liveZ = e.z; liveA = e.a;
            if (originSettle > 0) {
                // Re-sync the origin to live while the WCS re-zero settles, so the
                // one-time coordinate jump isn't read as a jog.
                originX = liveX; originY = liveY; originZ = liveZ; originA = liveA;
                originSettle--;
                return;
            }
            if (runState == RunState::Ready && originSet) {
                if (std::fabs(liveX - originX) > 0.05 ||
                    std::fabs(liveY - originY) > 0.05 ||
                    std::fabs(liveZ - originZ) > 0.05 ||
                    std::fabs(liveA - originA) > 0.10) {
                    runState = RunState::NeedsOrigin;
                    originSet = false;
                    updateRunButton();
                }
            }
        }

        // -- The driving sequence (machine coords; G90 == machine) ------

        // Append one probe to an op: rapid to the standoff, G38.2 down to `through`,
        // retract back to the standoff (so travel BETWEEN points stays high/safe).
        void addProbe(Carvera::MachineLink::Operation& op,
                      double offsetY, double angleDeg, double standoffZ, double throughZ) {
            using Pth = Carvera::MachineLink::Path;
            using Wp  = Carvera::MachineLink::Waypoint;
            const double ax = originA + angleDeg;
            const double yy = originY + offsetY;

            Pth tr = Pth::travel();
            tr.points.push_back(Wp{ originX, yy, standoffZ, ax });
            op.paths.push_back(tr);

            Pth in = Pth::intersect(kProbeFeed);
            in.points.push_back(Wp{ originX, yy, throughZ, ax });
            op.paths.push_back(in);

            Pth rt = Pth::travel();
            rt.points.push_back(Wp{ originX, yy, standoffZ, ax });
            op.paths.push_back(rt);
        }

        // Phase 1: one probe at the centre, flat, to measure the true reference Z.
        void runDriver() {
            auto& link = Carvera::MachineLink::instance();
            if (!link.isArmed()) {
                statusText->content = "Machine not armed -- connect and arm before calibrating.";
                runState = RunState::Ready;
                updateRunButton();
                return;
            }

            calib.phase = Cam::App::ProbeCalibration::Phase::Leveling;
            driverPhase = DriverPhase::Reference;
            updateStatus();

            using Op = Carvera::MachineLink::Operation;
            Op op = Op::probe(probeOpSlot(), "Calibrate: reference");
            addProbe(op, /*offsetY*/ 0.0, /*angle*/ 0.0,
                     /*standoff*/ originZ + kClearance,
                     /*through*/  originZ - kTravel);
            link.enqueueOperations({ op });
        }

        // Phase 2: probe every (angle x lateral) with an `expectedRise`-safe standoff.
        void startSampling() {
            driverPhase = DriverPhase::Sampling;
            drvOrder.clear();
            drvIdx = 0;
            calib.phase = Cam::App::ProbeCalibration::Phase::Sampling;
            updateStatus();
            refresh(event);

            using Op = Carvera::MachineLink::Operation;
            Op op = Op::probe(probeOpSlot(), "Calibrate: samples");

            for (double ang : calib.angleSchedule()) {
                for (double lat : calib.lateralSchedule()) {
                    if (std::fabs(ang) < 0.5 && std::fabs(lat) < 1e-6) { continue; }  // centre done
                    const double rise = Cam::App::ProbeCalibration::expectedRise(ang, lat, kAxisDepthGuess);
                    const double standoff = drvFlatZ + rise + kClearance;
                    addProbe(op, lat, ang, standoff, standoff - kTravel);
                    drvOrder.push_back({ ang, lat });
                }
            }
            Carvera::MachineLink::instance().enqueueOperations({ op });
        }

        // Contacts stream back in enqueue order; route by phase, fit when done.
        void onCalibContact(Carvera::MachineLink::ProbeEvent& e) {
            if (driverPhase == DriverPhase::Reference) {
                // No contact => the Z origin is too high; abort rather than sample
                // against a bogus reference (every standoff would be wrong).
                if (!e.triggered) {
                    driverPhase = DriverPhase::None;
                    calib.phase = Cam::App::ProbeCalibration::Phase::Failed;
                    runState = RunState::Ready;
                    statusText->content =
                        "Reference probe didn't contact -- set the origin at/just above "
                        "the face, then retry.";
                    updateRunButton();
                    updateStatus();
                    refresh(event);
                    return;
                }
                drvFlatZ = e.z;
                calib.samples.push_back({ 0.0, 0.0, double(e.z) });
                startSampling();
            }
            else if (driverPhase == DriverPhase::Sampling) {
                if (drvIdx < drvOrder.size()) {
                    if (e.triggered) {
                        calib.samples.push_back(
                            { drvOrder[drvIdx].first, drvOrder[drvIdx].second, double(e.z) });
                    }
                    drvIdx++;
                }
                if (drvIdx >= drvOrder.size()) {
                    driverPhase = DriverPhase::None;
                    calib.phase = Cam::App::ProbeCalibration::Phase::Fitting;
                    calib.fit();
                    calibrationComplete();
                }
                else {
                    updateStatus();
                    refresh(event);
                }
            }
        }

        void saveResult(Event&) {
            if (!calib.haveResult) {
                statusText->content = "Nothing to save yet -- run a calibration first.";
                refresh(event);
                return;
            }

            Cam::App::Tool* t = tool();
            if (t) {
                t->probe.stylusRadius        = calib.resultRadius;
                t->probe.calibrated          = true;
                t->probe.calibrationResidual = calib.resultResidual;
                t->probe.stylusRadiusSigma   = calib.resultRadiusSigma;
                if (!t->filePath.empty()) {
                    Cam::App::ToolLibrary::saveToolFileAtPath(t->filePath, *t);
                }
                if (onCalibrated) { onCalibrated(calib.resultRadius); }
            }
            updateStatus();
            refresh(event);
        }

        void redo(Event&) {
            calib.reset();
            updateStatus();
            refresh(event);
        }

        // -- Close handling (no unsaved state to guard) -----------------

        void close(Event* event = nullptr) {
            if (alive) { *alive = false; }   // stop the telemetry listener
            shouldClose = true;
            if (event && onClosed) { onClosed(*event); }
        }

        void requestClose(Event* event = nullptr) {
            close(event ? event : &this->event);
        }

        void onClose(bool& rejectClose) override {
            if (alive) { *alive = false; }   // stop the telemetry listener
            rejectClose = false;
            if (onClosed) { onClosed(this->event); }
        }
    };
}
