module;

#include <string>
#include <cstdio>
#include <cmath>
#include <memory>
#include <functional>

export module Cam.Gui.MeasureStockWindow;

import Rev.Window;
import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.NumberInput;
import Rev.Element.Button;

import Cam.App;
import Cam.App.Tool;
import Cam.App.Project;
import Cam.App.MachineProfile;
import Cam.App.StockMeasurement;
import Cam.Gui.Theme;
import Cam.Gui.Form;

import CarveraAir;

// ------------------------------------------------------------------
// Cam::Gui::MeasureStockWindow
//
// Child window for the Measure Stock step.  With the probe, machine axis, machine
// position and tool tip all already known, this probes the raw stock against the
// true rotary axis to resolve its residual unknowns -- angular offset, cross
// section size, and centre offset -- then feeds them to the stock definition.
//
// Driver (mirrors the calibration windows' Set Origin -> Start -> Stop):
//   1. Park above the known axis Y/Z (the first travel is already high + safe).
//   2. Set Origin -- captures the user-jogged X (Y/Z come from the measured axis).
//   3. LEVEL the top face (A=0, lateral spread) -> angular offset.
//   4. FLIP through A = 90/180/270, probe each face -> its axis distance.
//   Commit writes W/H (or radius) + centre offset + angle into project->stock.
// ------------------------------------------------------------------

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;
    using namespace Rev::Appearance;

    namespace MeasureStockLayout {

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

    struct MeasureStockWindow : public Rev::Window {

        Cam::App::AppState* app = nullptr;
        std::string probeToolName;

        Cam::App::StockMeasurement meas;

        enum class RunState { NeedsOrigin, Ready, Running };
        RunState   runState  = RunState::NeedsOrigin;
        Button*    runButton = nullptr;
        Style      runBg{};

        bool   originSet = false;
        double originX = 0, originY = 0, originZ = 0, originA = 0;
        double liveX = 0, liveY = 0, liveZ = 0, liveA = 0;
        int    originSettle = 0;
        std::shared_ptr<bool> alive;

        enum class DriverPhase { None, Sampling };
        DriverPhase driverPhase = DriverPhase::None;
        std::vector<std::pair<int, double>> drvOrder;   // (face, lateral) per probe
        std::size_t drvIdx = 0;

        static constexpr double kClearance      = 3.0;
        static constexpr double kTravel         = 9.0;
        static constexpr double kProbeFeed      = 100.0;
        static constexpr double kStockClearance = 5.0;   // min tip-to-stock gap (mm)

        NumberInput* widthInput  = nullptr;
        NumberInput* heightInput = nullptr;
        NumberInput* boundInput  = nullptr;
        NumberInput* pointsInput = nullptr;

        Text* planText   = nullptr;
        Text* statusText = nullptr;

        std::function<void(MeasureStockWindow&)> onStopRequested;
        std::function<void(Cam::App::StockMeasurement&)> onMeasured;
        std::function<void(Event&)> onClosed;

        MeasureStockWindow(Rev::Window* owner, const std::string& probeTool = "")
            : Rev::Window(
                owner,
                {
                    .name = "Measure Stock",
                    .size = { .width = 460, .height = 560 },
                    .minimizeButton = false,
                    .maximizeButton = false
                }
            ) {
            probeToolName = probeTool;

            if (owner && owner->shared) { shared->state = owner->shared->state; }
            app = Cam::App::AppState::Get(shared->state);

            resolveProbe();
            seedFromProject();

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
                        onContact(e);
                    });
            }

            setTitle("Measure Stock");
            if (owner) { setPos(owner->details.x + 60, owner->details.y + 70); }
            else       { setPos(280, 140); }

            show();
            refresh(event);
        }

        Cam::App::Tool* tool() {
            return app ? app->toolLibrary()->find(probeToolName) : nullptr;
        }

        Cam::App::Project* project() {
            return app ? app->activeProject : nullptr;
        }

        // Slot of the resolved probe tool (library position), pushed to Air so the
        // spindle interlock treats that loaded slot as a probe.  No magic number.
        int probeOpSlot() {
            int slot = (app && app->toolLibrary()) ? app->toolLibrary()->indexOf(probeToolName) : 0;
            if (slot <= 0 && app && app->toolLibrary()) { slot = app->toolLibrary()->probeSlot(); }
            Carvera::MachineLink::instance().setProbeSlot(slot);
            return slot;
        }

        // Adopt a probe tool (prefer a calibrated one) so its radius is available.
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

        // The calibrated rotary axis lives on the machine definition (machine coords),
        // which is exactly the frame measure-stock probes in.
        Cam::App::MachineProfile* machine() {
            return app ? app->selectedMachine() : nullptr;
        }
        bool machineAxisCalibrated() {
            Cam::App::MachineProfile* m = machine();
            return m && m->rotaryAxisCalibrated;
        }

        // Seed known inputs (axis, probe) + the suggested geometry (from the stock).
        void seedFromProject() {
            if (Cam::App::Tool* t = tool()) { meas.probeRadius = t->probe.stylusRadius; }
            if (Cam::App::MachineProfile* m = machine()) {
                meas.axisY = m->rotaryAxisY;
                meas.axisZ = m->rotaryAxisZ;
            }
            if (Cam::App::Project* p = project()) {
                meas.cylinder = (p->stock.type == Cam::App::StockType::Cylinder);
                meas.suggestedWidth  = p->stock.width  > 0 ? p->stock.width  : 10.0;
                meas.suggestedHeight = p->stock.height > 0 ? p->stock.height : 10.0;
                meas.suggestedRadius = p->stock.radius > 0 ? p->stock.radius : 5.0;
                meas.suggestedLength = p->stock.length;
            }
        }

        // -- Build -----------------------------------------------------

        void buildUi() {

            Box* root = new Box(this, Theme::withSettingsDialog({ &MeasureStockLayout::Root }), "MeasureStockRoot");

            Box* header = new Box(root,
                Theme::layer({ &MeasureStockLayout::Header }, { &Theme::Styles::SettingsHeader }), "Header");
            new Text(header, "MEASURE STOCK",
                     Theme::layer({}, { &Theme::Styles::SettingsHeaderEyebrow }));
            new Text(header, "Locate the raw stock",
                     Theme::layer({}, { &Theme::Styles::SettingsHeaderTitle }));

            Box* body = new Box(root,
                Theme::layer({ &MeasureStockLayout::Body, &Theme::Styles::SettingsBody }, { &Theme::Styles::Text }), "Body");
            Box* col = Form::column(body, "Content");

            new Text(col,
                "Mount the raw stock in the chuck. With the probe, axis, position and "
                "tip already calibrated, this levels the top face, then rotates through "
                "the four faces, probing each against the known rotary axis to measure "
                "the stock's size, centre offset and angular offset.",
                Theme::layer({ &MeasureStockLayout::Note }, { &Theme::Styles::MutedText }));

            Form::section(col, "SUGGESTED STOCK");
            Box* dimRow = Form::row(col, "DimRow");
            widthInput  = Form::numberField(dimRow, "Width (mm)", "10");
            heightInput = Form::numberField(dimRow, "Height (mm)", "10");

            Form::section(col, "SAMPLING");
            Box* sampRow = Form::row(col, "SampRow");
            boundInput  = Form::numberField(sampRow, "Linear bound (mm)", "10");
            pointsInput = Form::numberField(sampRow, "Sample points", "3");

            Form::section(col, "PLAN");
            planText = new Text(col, "", Theme::layer({ &MeasureStockLayout::Note }, { &Theme::Styles::MutedText }));
            Form::section(col, "STATUS");
            statusText = new Text(col, "", Theme::layer({ &MeasureStockLayout::Note }, { &Theme::Styles::MutedText }));

            widthInput->setValue(meas.suggestedWidth);
            heightInput->setValue(meas.suggestedHeight);
            boundInput->setValue(meas.linearBound);
            pointsInput->setValue(static_cast<double>(meas.samplePoints));

            bindLive(widthInput); bindLive(heightInput);
            bindLive(boundInput); bindLive(pointsInput);

            Box* footer = new Box(root,
                Theme::layer({ &MeasureStockLayout::Footer }, { &Theme::Styles::SettingsFooter }), "Footer");

            Button* closeButton = new Button(footer, Button::Params::Secondary("Close"), { &MeasureStockLayout::FooterButton });
            closeButton->onClick([this](Event& e) { requestClose(&e); e.propagate = false; });

            Button* redoButton = new Button(footer, Button::Params::Secondary("Redo"), { &MeasureStockLayout::FooterButton });
            redoButton->onClick([this](Event& e) { redo(e); e.propagate = false; });

            Button* commitButton = new Button(footer, Button::Params::Secondary("Commit"), { &MeasureStockLayout::FooterButton });
            commitButton->onClick([this](Event& e) { commitResult(e); e.propagate = false; });

            runButton = new Button(footer, Button::Params::Primary("Set Origin"), { &MeasureStockLayout::FooterButton });
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
            if (widthInput)  { meas.suggestedWidth  = widthInput->valueOr(meas.suggestedWidth); }
            if (heightInput) { meas.suggestedHeight = heightInput->valueOr(meas.suggestedHeight); }
            if (boundInput)  { meas.linearBound  = boundInput->valueOr(meas.linearBound); }
            if (pointsInput) { meas.samplePoints = static_cast<int>(std::lround(pointsInput->valueOr(meas.samplePoints))); }
            meas.clampSpec();
        }

        void updatePlan(Event&) {
            captureSpec();
            if (!planText) { return; }
            char buf[260];
            const bool haveAxis = machineAxisCalibrated();
            std::snprintf(buf, sizeof(buf),
                "4 faces x %d points = %d contacts over %.0f mm.\n"
                "Axis Y=%.3f Z=%.3f%s  probe r=%.3f mm.",
                meas.samplePoints, meas.plannedContacts(), meas.linearBound,
                meas.axisY, meas.axisZ,
                haveAxis ? "" : " -- NOT machine-calibrated; calibrate the axis first.",
                meas.probeRadius);
            planText->content = buf;
        }

        void updateStatus() {
            if (!statusText) { return; }
            if (meas.haveResult) {
                char buf[240];
                std::snprintf(buf, sizeof(buf),
                    "W %.3f  H %.3f mm; centre offset (%.3f, %.3f); angle %.2f deg "
                    "(residual %.4f). Commit to store.",
                    meas.resultWidth, meas.resultHeight,
                    meas.resultCenterY, meas.resultCenterZ,
                    meas.resultAngleDeg, meas.resultResidual);
                statusText->content = buf;
            }
            else {
                statusText->content = Cam::App::StockMeasurement::phaseLabel(meas.phase);
            }
        }

        void measurementComplete() {
            meas.phase = meas.haveResult ? Cam::App::StockMeasurement::Phase::Review
                                         : Cam::App::StockMeasurement::Phase::Failed;
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
                case RunState::NeedsOrigin: runBg.background.color = sColor::Null();        label = "Set Origin"; break;
                case RunState::Ready:       runBg.background.color = rgba(40, 170, 90, 1.0); label = "Start";      break;
                case RunState::Running:     runBg.background.color = rgba(214, 64, 64, 1.0); label = "Stop";       break;
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

        void doSetOrigin() {
            auto& link = Carvera::MachineLink::instance();
            link.setWorkOrigin();
            float mx, my, mz, ma;
            if (link.machineOrigin(mx, my, mz, ma)) {
                originX = mx; originY = my; originZ = mz; originA = ma;
                originSet = true;
                originSettle = 12;
                runState = RunState::Ready;
                statusText->content = "Origin set (X). Press Start to measure the stock.";
            }
            else {
                statusText->content = "Set Origin failed -- connect and wait for a position.";
            }
            updateRunButton();
            refresh(event);
        }

        void startRun() {
            captureSpec();
            meas.reset();
            seedFromProject();   // refresh axis/probe in case calibration changed
            captureSpec();
            runState = RunState::Running;
            updateRunButton();
            runDriver();
            refresh(event);
        }

        void stopRun() {
            Carvera::MachineLink::instance().stop();
            driverPhase = DriverPhase::None;
            if (onStopRequested) { onStopRequested(*this); }
            meas.phase = Cam::App::StockMeasurement::Phase::Idle;
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
                    std::fabs(liveA - originA) > 0.10) {   // X + A define this op's origin
                    runState = RunState::NeedsOrigin;
                    originSet = false;
                    updateRunButton();
                }
            }
        }

        // -- The driving sequence --------------------------------------
        // The probe approaches each face from ABOVE: the face is rotated up by
        // A = originA + faceAngle, and we probe down onto it at X=originX,
        // Y=axisY+lateral.  The contact height minus axisZ is the axis->face
        // distance (the probe always touches the up surface, so it's along +Z).

        void addProbe(Carvera::MachineLink::Operation& op,
                      double lateral, double faceAngle, double standoffZ, double throughZ) {
            using Pth = Carvera::MachineLink::Path;
            using Wp  = Carvera::MachineLink::Waypoint;
            const double ax = originA + faceAngle;
            const double yy = meas.axisY + lateral;

            // Descend straight onto the up-facing surface -- A does NOT change here
            // (it was already rotated to `ax` at the safe radius), so this never
            // sweeps the stock.
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

        // A single travel waypoint at an ABSOLUTE A (used for the stock-clearing
        // rotation maneuver, which is pure translation then pure rotation).
        void addTravel(Carvera::MachineLink::Operation& op,
                       double y, double z, double aAbs) {
            using Pth = Carvera::MachineLink::Path;
            using Wp  = Carvera::MachineLink::Waypoint;
            Pth tr = Pth::travel();
            tr.points.push_back(Carvera::MachineLink::Waypoint{ originX, y, z, aAbs });
            op.paths.push_back(tr);
        }

        // The radius from the rotary axis at which the stock can spin without the
        // tip ever coming within kStockClearance of it.  Assumes the stock is
        // roughly centred and roughly its suggested size: the worst case is the
        // cross-section's farthest point from the axis (a prism corner = the
        // half-diagonal; a cylinder = its radius), plus the 5 mm safety gap.
        double safeRotationRadius() const {
            double reach;
            if (meas.cylinder) {
                reach = meas.suggestedRadius;
            } else {
                const double hw = meas.suggestedWidth  * 0.5;
                const double hh = meas.suggestedHeight * 0.5;
                reach = std::sqrt(hw * hw + hh * hh);
            }
            return reach + kStockClearance;
        }

        // Rotate to an absolute A, but NEVER while near the stock: first lift the tip
        // straight up to the stock-clearing radius at the CURRENT A (translation),
        // THEN rotate at that safe height (rotation).  This is the radial,
        // stock-aware move -- the analogue of how machining stages are linked.
        void rotateClearOfStock(Carvera::MachineLink::Operation& op,
                                double fromAabs, double toAabs, double safeZ) {
            addTravel(op, meas.axisY, safeZ, fromAabs);   // 1) translate up, no rotation
            if (std::fabs(toAabs - fromAabs) > 1e-6) {
                addTravel(op, meas.axisY, safeZ, toAabs); // 2) rotate at the safe radius
            }
        }

        void runDriver() {
            auto& link = Carvera::MachineLink::instance();
            if (!link.isArmed()) {
                statusText->content = "Machine not armed -- connect and arm before measuring.";
                runState = RunState::Ready;
                updateRunButton();
                return;
            }
            if (!machineAxisCalibrated()) {
                statusText->content = "No calibrated rotary axis -- run machine calibration first.";
                runState = RunState::Ready;
                updateRunButton();
                return;
            }

            driverPhase = DriverPhase::Sampling;
            drvOrder.clear();
            drvIdx = 0;
            meas.phase = Cam::App::StockMeasurement::Phase::Leveling;
            updateStatus();

            using Op = Carvera::MachineLink::Operation;
            Op op = Op::probe(probeOpSlot(), "Measure stock");

            // Flat stock can sweep into the probe as it turns, so we NEVER blend
            // rotation with translation: before each face we lift clear of the stock
            // and rotate at a safe radius, then descend straight onto the up face.
            const double safeZ = meas.axisZ + safeRotationRadius();
            double lastA = originA;

            for (int f = 0; f < Cam::App::StockMeasurement::kFaceCount; f++) {
                const double angle    = Cam::App::StockMeasurement::kFaceAngle[f];
                const double absA     = originA + angle;
                const double standoff = meas.axisZ + meas.expectedDistance(f) + kClearance;

                rotateClearOfStock(op, lastA, absA, safeZ);   // safe radial reposition
                lastA = absA;

                for (double lat : meas.lateralSchedule()) {
                    addProbe(op, lat, angle, standoff, standoff - kTravel);
                    drvOrder.push_back({ f, lat });
                }
            }
            // Finish clear of the stock so the part can be removed safely.
            rotateClearOfStock(op, lastA, lastA, safeZ);
            link.enqueueOperations({ op });
        }

        void onContact(Carvera::MachineLink::ProbeEvent& e) {
            if (driverPhase != DriverPhase::Sampling) { return; }
            if (drvIdx < drvOrder.size()) {
                if (e.triggered) {
                    meas.samples.push_back({ drvOrder[drvIdx].first, drvOrder[drvIdx].second, double(e.z) });
                }
                drvIdx++;
            }
            if (drvIdx >= drvOrder.size()) {
                driverPhase = DriverPhase::None;
                meas.phase = Cam::App::StockMeasurement::Phase::Fitting;
                meas.fit();
                measurementComplete();
            }
            else {
                if (drvIdx == 1) { meas.phase = Cam::App::StockMeasurement::Phase::Sampling; }
                updateStatus();
                refresh(event);
            }
        }

        // Commit the measured stock into the project + regenerate the four
        // material-state (Stock +/-Y, +/-Z) steps from the real geometry.
        void commitResult(Event&) {
            if (!meas.haveResult) {
                statusText->content = "Nothing to commit yet -- run a measurement first.";
                refresh(event);
                return;
            }
            if (Cam::App::Project* p = project()) {
                if (meas.cylinder) {
                    p->stock.radius = meas.resultRadius;
                }
                else {
                    p->stock.width  = meas.resultWidth;
                    p->stock.height = meas.resultHeight;
                }
                p->stock.measured            = true;
                p->stock.angularOffsetDeg    = meas.resultAngleDeg;
                p->stock.centerOffsetY       = meas.resultCenterY;
                p->stock.centerOffsetZ       = meas.resultCenterZ;
                p->stock.measurementResidual = meas.resultResidual;
                p->stock.defined             = true;
                p->regenerateStockStages();
            }
            if (onMeasured) { onMeasured(meas); }
            statusText->content = "Stock measurement committed.";
            refresh(event);
        }

        void redo(Event&) {
            meas.reset();
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
