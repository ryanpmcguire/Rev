module;

#include <string>
#include <cstdio>
#include <cmath>
#include <memory>
#include <functional>

export module Cam.Gui.MachineCalibrationWindow;

import Rev.Window;
import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;
import Rev.Element.NumberInput;

import Cam.App;
import Cam.App.Tool;
import Cam.App.Project;
import Cam.App.MachineCalibration;
import Cam.Gui.Theme;
import Cam.Gui.Form;

import CarveraAir;

// ------------------------------------------------------------------
// Cam::Gui::MachineCalibrationWindow
//
// Child window that calibrates the MACHINE DEFINITION: with a CALIBRATED probe
// (known stylus radius r + 1-sigma), it probes a mounted flat aluminum plane at
// a spread of tilt angles and lateral positions to locate the ROTARY (A) AXIS in
// Y and Z -- with a stored confidence (sigma).  The flow mirrors probe
// calibration; it differs only in what it solves for (the axis, not the radius)
// and where it commits the result (the work frame's rotary axis, not the tool).
//
//   1. LEVEL  -- probe the flat top at A=0 for a true reference height.
//   2. SAMPLE -- tilt across [angleMin, angleMax] (both signs) and probe
//                `samplePoints` locations across `linearBound` (along Y) at each.
//   3. FIT    -- de-bias by r, least-squares the axis Y/Z + confidences.
//   4. REVIEW -- show the axis; COMMIT it to the work frame, or REDO.
//
// The probe radius + its sigma are sourced from a loaded/selected probe tool.
// ------------------------------------------------------------------

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;
    using namespace Rev::Appearance;   // rgba(), sColor

    namespace MachineCalibrationLayout {

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

    struct MachineCalibrationWindow : public Rev::Window {

        Cam::App::AppState* app = nullptr;
        std::string probeToolName;   // probe whose calibrated r + sigma we use

        Cam::App::MachineCalibration calib;

        // Run-button state machine: Home & Touch Off (blue) -> Set Origin (blue) ->
        // Start (green) -> Stop (red).  Machine calibration ALWAYS homes + touches
        // off first, so the axis Y/Z is measured against the machine's true,
        // repeatable zero (and the probe tip is referenced) before anything else.
        enum class RunState { NeedsHome, NeedsOrigin, Ready, Running };
        RunState   runState  = RunState::NeedsHome;
        Button*    runButton = nullptr;
        Style      runBg{};

        bool   originSet = false;
        double originX = 0, originY = 0, originZ = 0, originA = 0;
        double liveX = 0, liveY = 0, liveZ = 0, liveA = 0;
        int    originSettle = 0;
        std::shared_ptr<bool> alive;

        // -- Machine driver: flat reference, then tilt sampling --------
        enum class DriverPhase { None, Reference, Sampling };
        DriverPhase driverPhase = DriverPhase::None;
        double drvFlatZ = 0.0;
        std::vector<std::pair<double, double>> drvOrder;
        std::size_t drvIdx = 0;

        static constexpr double kClearance      = 3.0;
        static constexpr double kTravel         = 9.0;
        static constexpr double kProbeFeed      = 100.0;
        static constexpr double kAxisDepthGuess = 10.0;

        NumberInput* linearBoundInput  = nullptr;
        NumberInput* angleMinInput     = nullptr;
        NumberInput* angleMaxInput     = nullptr;
        NumberInput* sampleAnglesInput = nullptr;
        NumberInput* samplePointsInput = nullptr;

        Text* planText   = nullptr;
        Text* statusText = nullptr;

        std::function<void(MachineCalibrationWindow&)> onStopRequested;
        // Fired after a successful Commit, so an owner can persist / refresh.
        std::function<void(Cam::App::MachineCalibration&)> onAxisCalibrated;
        std::function<void(Event&)> onClosed;

        MachineCalibrationWindow(Rev::Window* owner, const std::string& probeTool = "")
            : Rev::Window(
                owner,
                {
                    .name = "Calibrate Machine",
                    .size = { .width = 460, .height = 560 },
                    .minimizeButton = false,
                    .maximizeButton = false
                }
            ) {
            probeToolName = probeTool;

            if (owner && owner->shared) { shared->state = owner->shared->state; }
            app = Cam::App::AppState::Get(shared->state);

            // Machine calibration needs a CALIBRATED probe but isn't tied to one, so
            // resolve it ourselves when no specific probe was handed in (the machine
            // settings context has no probe in hand).
            resolveProbe();

            // Seed the known probe radius + its uncertainty from the probe tool.
            if (Cam::App::Tool* t = tool()) {
                calib.probeRadius      = t->probe.stylusRadius;
                calib.probeRadiusSigma = t->probe.stylusRadiusSigma;
            }

            style->layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
            style->size = { .width = 100_pct, .height = 100_pct };

            buildUi();

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

            setTitle("Calibrate Machine");
            if (owner) { setPos(owner->details.x + 60, owner->details.y + 70); }
            else       { setPos(280, 140); }

            show();
            refresh(event);
        }

        Cam::App::Tool* tool() {
            return app ? app->toolLibrary()->find(probeToolName) : nullptr;
        }

        // Slot of the resolved probe tool (by its library position), pushed to Air so
        // the spindle interlock treats that loaded slot as a probe.  No magic number.
        int probeOpSlot() {
            int slot = (app && app->toolLibrary()) ? app->toolLibrary()->indexOf(probeToolName) : 0;
            if (slot <= 0 && app && app->toolLibrary()) { slot = app->toolLibrary()->probeSlot(); }
            Carvera::MachineLink::instance().setProbeSlot(slot);
            return slot;
        }

        // Ensure `probeToolName` names a probe tool.  If the supplied name isn't a
        // probe (or is empty), scan the library and adopt a probe -- preferring a
        // CALIBRATED one (its radius + sigma are what make the fit trustworthy).
        void resolveProbe() {
            if (!app) { return; }
            auto* lib = app->toolLibrary();
            if (!lib) { return; }

            Cam::App::Tool* given = lib->find(probeToolName);
            if (given && given->type == Cam::App::Tool::Type::Probe) { return; }

            std::string firstProbe;
            for (const std::string& name : lib->order) {
                Cam::App::Tool* t = lib->find(name);
                if (!t || t->type != Cam::App::Tool::Type::Probe) { continue; }
                if (firstProbe.empty()) { firstProbe = name; }
                if (t->probe.calibrated) { probeToolName = name; return; }
            }
            if (!firstProbe.empty()) { probeToolName = firstProbe; }
        }

        // -- Build -----------------------------------------------------

        void buildUi() {

            Box* root = new Box(
                this,
                Theme::withSettingsDialog({ &MachineCalibrationLayout::Root }),
                "MachineCalibRoot"
            );

            Box* header = new Box(
                root,
                Theme::layer({ &MachineCalibrationLayout::Header }, { &Theme::Styles::SettingsHeader }),
                "Header"
            );
            new Text(header, "MACHINE CALIBRATION",
                     Theme::layer({}, { &Theme::Styles::SettingsHeaderEyebrow }));
            new Text(header, "Rotary axis location",
                     Theme::layer({}, { &Theme::Styles::SettingsHeaderTitle }));

            Box* body = new Box(
                root,
                Theme::layer(
                    { &MachineCalibrationLayout::Body, &Theme::Styles::SettingsBody },
                    { &Theme::Styles::Text }
                ),
                "Body"
            );
            Box* col = Form::column(body, "Content");

            new Text(
                col,
                "Mount a flat aluminum plane in the chuck. With a CALIBRATED probe "
                "loaded, this levels the face, then tilts and probes it across a range "
                "of angles to locate the rotary axis in Y and Z, with a confidence.",
                Theme::layer({ &MachineCalibrationLayout::Note }, { &Theme::Styles::MutedText })
            );

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

            Form::section(col, "PLAN");
            planText = new Text(col, "", Theme::layer({ &MachineCalibrationLayout::Note }, { &Theme::Styles::MutedText }));
            Form::section(col, "STATUS");
            statusText = new Text(col, "", Theme::layer({ &MachineCalibrationLayout::Note }, { &Theme::Styles::MutedText }));

            linearBoundInput->setValue(calib.linearBound);
            samplePointsInput->setValue(static_cast<double>(calib.samplePoints));
            angleMinInput->setValue(calib.angleMin);
            angleMaxInput->setValue(calib.angleMax);
            sampleAnglesInput->setValue(static_cast<double>(calib.sampleAngles));

            bindLive(linearBoundInput);
            bindLive(samplePointsInput);
            bindLive(angleMinInput);
            bindLive(angleMaxInput);
            bindLive(sampleAnglesInput);

            // Footer
            Box* footer = new Box(
                root,
                Theme::layer({ &MachineCalibrationLayout::Footer }, { &Theme::Styles::SettingsFooter }),
                "Footer"
            );

            Button* closeButton = new Button(footer, Button::Params::Secondary("Close"), { &MachineCalibrationLayout::FooterButton });
            closeButton->onClick([this](Event& e) { requestClose(&e); e.propagate = false; });

            Button* redoButton = new Button(footer, Button::Params::Secondary("Redo"), { &MachineCalibrationLayout::FooterButton });
            redoButton->onClick([this](Event& e) { redo(e); e.propagate = false; });

            Button* commitButton = new Button(footer, Button::Params::Secondary("Commit"), { &MachineCalibrationLayout::FooterButton });
            commitButton->onClick([this](Event& e) { commitResult(e); e.propagate = false; });

            runButton = new Button(footer, Button::Params::Primary("Set Origin"), { &MachineCalibrationLayout::FooterButton });
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
            char buf[320];
            const int orientations = static_cast<int>(calib.angleSchedule().size());
            Cam::App::Tool* t = tool();
            const char* probeName = t ? t->name.c_str() : "(no probe)";
            const bool  cal = t && t->probe.calibrated;
            std::snprintf(
                buf, sizeof(buf),
                "%d orientations x %d points = %d contacts; %.0f-%.0f deg over %.0f mm.\n"
                "Probe \"%s\": r=%.3f +/- %.3f mm%s",
                orientations, calib.samplePoints, calib.plannedContacts(),
                calib.angleMin, calib.angleMax, calib.linearBound,
                probeName, calib.probeRadius, calib.probeRadiusSigma,
                cal ? "." : "  -- NOT calibrated; calibrate the probe first."
            );
            planText->content = buf;
        }

        void updateStatus() {
            if (!statusText) { return; }
            if (calib.haveResult) {
                char buf[220];
                std::snprintf(buf, sizeof(buf),
                    "Rotary axis  Y %.3f +/- %.3f mm,  Z %.3f +/- %.3f mm "
                    "(depth %.3f, residual %.4f). Commit to store.",
                    originY + calib.resultAxisY, calib.resultAxisYSigma,
                    calib.resultAxisZ, calib.resultAxisZSigma,
                    calib.resultDepth, calib.resultResidual);
                statusText->content = buf;
            }
            else {
                statusText->content = Cam::App::MachineCalibration::phaseLabel(calib.phase);
            }
        }

        void calibrationComplete() {
            calib.phase = calib.haveResult ? Cam::App::MachineCalibration::Phase::Review
                                           : Cam::App::MachineCalibration::Phase::Failed;
            runState = RunState::Ready;
            updateRunButton();
            updateStatus();
            refresh(event);
        }

        // -- Run-button state machine ----------------------------------

        void updateRunButton() {
            if (!runButton) { return; }
            const char* label = "Set Origin";
            switch (runState) {
                case RunState::NeedsHome:
                    runBg.background.color = sColor::Null();
                    label = "Home & Touch Off";
                    break;
                case RunState::NeedsOrigin:
                    runBg.background.color = sColor::Null();
                    label = "Set Origin";
                    break;
                case RunState::Ready:
                    runBg.background.color = rgba(40, 170, 90, 1.0);
                    label = "Start";
                    break;
                case RunState::Running:
                    runBg.background.color = rgba(214, 64, 64, 1.0);
                    label = "Stop";
                    break;
            }
            if (runButton->labelText) { runButton->labelText->content = label; }
            refresh(event);
        }

        void onRunButton(Event&) {
            switch (runState) {
                case RunState::NeedsHome:   doHomeTouchOff(); break;
                case RunState::NeedsOrigin: doSetOrigin();    break;
                case RunState::Ready:       startRun();       break;
                case RunState::Running:     stopRun();        break;
            }
        }

        // Home + touch off FIRST: reference the machine to its repeatable zero and
        // the probe tip to the built-in sensor, so the axis Y/Z that calibration
        // measures is expressed against that true zero.  Homing is async; advancing
        // to Set Origin is fine -- the operator jogs to the artifact afterward.
        void doHomeTouchOff() {
            auto& link = Carvera::MachineLink::instance();
            link.home();
            link.touchOffProbe();
            runState = RunState::NeedsOrigin;
            statusText->content =
                "Homing + touch-off issued. When motion settles, jog to the artifact "
                "and press Set Origin.";
            updateRunButton();
            refresh(event);
        }

        void doSetOrigin() {
            auto& link = Carvera::MachineLink::instance();
            link.setWorkOrigin();

            float mx, my, mz, ma;
            if (link.machineOrigin(mx, my, mz, ma)) {
                originX = mx; originY = my; originZ = mz; originA = ma;
                originSet = true;
                originSettle = 12;
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
            if (Cam::App::Tool* t = tool()) {
                calib.probeRadius      = t->probe.stylusRadius;
                calib.probeRadiusSigma = t->probe.stylusRadiusSigma;
            }
            runState = RunState::Running;
            updateRunButton();
            runDriver();
            refresh(event);
        }

        void stopRun() {
            Carvera::MachineLink::instance().stop();
            driverPhase = DriverPhase::None;
            if (onStopRequested) { onStopRequested(*this); }
            calib.phase = Cam::App::MachineCalibration::Phase::Idle;
            runState = RunState::Ready;
            updateRunButton();
            updateStatus();
            refresh(event);
        }

        void onTelemetry(Carvera::MachineLink::TelemetryEvent& e) {
            liveX = e.x; liveY = e.y; liveZ = e.z; liveA = e.a;
            if (originSettle > 0) {
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

        void runDriver() {
            auto& link = Carvera::MachineLink::instance();
            if (!link.isArmed()) {
                statusText->content = "Machine not armed -- connect and arm before calibrating.";
                runState = RunState::Ready;
                updateRunButton();
                return;
            }

            calib.phase = Cam::App::MachineCalibration::Phase::Leveling;
            driverPhase = DriverPhase::Reference;
            updateStatus();

            using Op = Carvera::MachineLink::Operation;
            Op op = Op::probe(probeOpSlot(), "Machine calib: reference");
            addProbe(op, /*offsetY*/ 0.0, /*angle*/ 0.0,
                     /*standoff*/ originZ + kClearance,
                     /*through*/  originZ - kTravel);
            link.enqueueOperations({ op });
        }

        void startSampling() {
            driverPhase = DriverPhase::Sampling;
            drvOrder.clear();
            drvIdx = 0;
            calib.phase = Cam::App::MachineCalibration::Phase::Sampling;
            updateStatus();
            refresh(event);

            using Op = Carvera::MachineLink::Operation;
            Op op = Op::probe(probeOpSlot(), "Machine calib: samples");

            for (double ang : calib.angleSchedule()) {
                for (double lat : calib.lateralSchedule()) {
                    if (std::fabs(ang) < 0.5 && std::fabs(lat) < 1e-6) { continue; }  // centre done
                    const double rise = Cam::App::MachineCalibration::expectedRise(ang, lat, kAxisDepthGuess);
                    const double standoff = drvFlatZ + rise + kClearance;
                    addProbe(op, lat, ang, standoff, standoff - kTravel);
                    drvOrder.push_back({ ang, lat });
                }
            }
            Carvera::MachineLink::instance().enqueueOperations({ op });
        }

        void onCalibContact(Carvera::MachineLink::ProbeEvent& e) {
            if (driverPhase == DriverPhase::Reference) {
                if (!e.triggered) {
                    driverPhase = DriverPhase::None;
                    calib.phase = Cam::App::MachineCalibration::Phase::Failed;
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
                    calib.phase = Cam::App::MachineCalibration::Phase::Fitting;
                    calib.fit();
                    calibrationComplete();
                }
                else {
                    updateStatus();
                    refresh(event);
                }
            }
        }

        // Commit the inferred axis into the work frame.  The fit is in MACHINE
        // coordinates (the absolute frame established by Set Origin): axis Z is
        // machine Z, axis Y is the lateral origin (machine Y at Set Origin) plus the
        // fitted lateral offset.  Stored with measured=true + the 1-sigma confidence.
        void commitResult(Event&) {
            if (!calib.haveResult) {
                statusText->content = "Nothing to commit yet -- run a calibration first.";
                refresh(event);
                return;
            }
            // Resolve the axis Y into absolute machine coords; the owner persists it
            // onto the MACHINE DEFINITION (the single source of truth -- the calibrated
            // axis is machine geometry, used at Set Origin to pin the work frame).
            calib.resultAxisYAbs = originY + calib.resultAxisY;
            if (onAxisCalibrated) { onAxisCalibrated(calib); }
            statusText->content = "Rotary axis committed to the machine definition.";
            refresh(event);
        }

        void redo(Event&) {
            calib.reset();
            updateStatus();
            refresh(event);
        }

        // -- Close handling --------------------------------------------

        void close(Event* event = nullptr) {
            if (alive) { *alive = false; }
            shouldClose = true;
            if (event && onClosed) { onClosed(*event); }
        }

        void requestClose(Event* event = nullptr) {
            close(event ? event : &this->event);
        }

        void onClose(bool& rejectClose) override {
            if (alive) { *alive = false; }
            rejectClose = false;
            if (onClosed) { onClosed(this->event); }
        }
    };
}
