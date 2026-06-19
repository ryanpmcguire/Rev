module;

#include <string>
#include <format>
#include <algorithm>
#include <cmath>
#include <limits>

#include <managed.hpp>

export module Carvera.Gui.JogSection;

import Rev.Core.Animator;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;

import CarveraAir;

import Cam.Gui.Theme;
import Carvera.Gui.Style;

export namespace Carvera::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace Theme = Cam::Gui::Theme;

    // Jog section -- live position readout + +/-X/+/-Y/+/-Z/+/-A jog grid + step size.
    //
    // Keyboard plumbing (called by the Interface root via handleKeyDown /
    // handleKeyUp):
    //
    //   left/right : -X/+X       shift   : continuous-hold mode
    //   up/down    : +Y/-Y       ctrl    : fine (x0.1) increment / feed
    //   q/e        : +Z/-Z
    //   z/x        : -A/+A
    //
    // Without shift  a key press emits ONE step jog (autorepeat is ignored --
    //                you must release and press again to step again).
    //
    // With shift     the section opens a "continuous session": ONE big
    //                `$J=G90` jog is sent toward a far target in the held
    //                direction, so the machine sees a single smooth motion
    //                rather than a flood of independent step jogs.  Adding or
    //                removing axis keys mid-jog issues a real-time jog cancel
    //                (0x85) followed by a fresh $J in the new direction.  On
    //                release of the last key we cancel and emit a FINAL $J
    //                whose absolute target is rounded UP (per axis, in the
    //                direction of motion) to the next jogStepMm multiple -- so
    //                the machine always lands cleanly on the grid even though
    //                the path was a single continuous move.
    struct JogSection : public Box {

        static Carvera::Air& air() { return Carvera::Air::instance(); }

        // -- Step presets (UI-only) -------------------------------------

        static constexpr float kStepPresets[] = { 0.01f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
        static constexpr int   kStepCount     = 6;
        static constexpr float kFineFactor    = 0.1f;   // ctrl modifier

        // Continuous-mode constants.
        static constexpr int   kJogFeedMmMin  = 1000;
        static constexpr int   kJogFeedDegMin = 3000;
        // Per-leg "chunk": how far the in-flight jog targets at any time.
        // Must comfortably fit inside the machine's working envelope from any
        // starting position -- a huge target like 10 m triggers GRBL's soft
        // limit (the "jumping off a cliff" alarm).  We re-extend periodically
        // while keys are still held so the chunk size doesn't cap travel.
        static constexpr float kChunkMm       = 25.0f;
        static constexpr float kChunkDeg      = 90.0f;
        // While the machine is approaching the in-flight target, top up the
        // queue with another chunk so the planner never runs dry.  Threshold
        // is "extend when less than 60% of the chunk remains" -- at F=1000 the
        // 200 ms extend cadence covers 3.3 mm, so 60% x 25 mm = 15 mm gives
        // multiple ticks of margin before the machine could ever decelerate.
        static constexpr int   kExtendTickMs   = 200;
        static constexpr float kExtendFraction = 0.60f;
        // Safety margin past the live position when computing the final
        // grid-aligned target -- covers telemetry lag (~50 ms at F=1000 ~ 0.8 mm)
        // and the brief jog-cancel deceleration distance so the final jog
        // never has to reverse.
        static constexpr float kSafetyMm      = 2.0f;
        static constexpr float kSafetyDeg     = 5.0f;

        int   stepIndex  = 3;
        float jogStepMm  = 1.0f;
        float jogStepDeg = 5.0f;

        // -- UI nodes ---------------------------------------------------

        Text* posXText = nullptr, *posYText = nullptr;
        Text* posZText = nullptr, *posAText = nullptr;
        Text* stepText = nullptr;

        Box* btnPX = nullptr; Box* btnNX = nullptr;
        Box* btnPY = nullptr; Box* btnNY = nullptr;
        Box* btnPZ = nullptr; Box* btnNZ = nullptr;
        Box* btnAP = nullptr; Box* btnAM = nullptr;

        Box* activeJogBtn = nullptr;

        // -- Telemetry display throttle (140 Hz -> only when value changes) ---

        float lastShownX_ = 0, lastShownY_ = 0, lastShownZ_ = 0, lastShownA_ = 0;
        bool  lastShownValid_ = false;
        Box*  lastActiveJogBtn_ = nullptr;

        // -- Per-key pressed state -------------------------------------

        bool xPlusDown_ = false, xMinusDown_ = false;
        bool yPlusDown_ = false, yMinusDown_ = false;
        bool zPlusDown_ = false, zMinusDown_ = false;
        bool aPlusDown_ = false, aMinusDown_ = false;

        // -- Continuous-hold session -----------------------------------
        //
        // A session begins on the first shift+axis keyDown and ends on
        // release of the last axis key.  All motion is expressed as G91
        // relative deltas so we never need to know the WCS state -- a +25
        // chunk always moves the axis +25 from wherever the controller is.
        //
        // We track:
        //   - the most recent direction (so we know how to round on release),
        //   - the machine position at the start of the current leg, and
        //   - the total signed delta we've COMMANDED on this leg so far.
        // The extend ticker compares (commanded delta) against actual delta
        // from livePosition() to decide when the planner needs another chunk.
        bool sessionActive_   = false;
        bool sessionFineMode_ = false;
        int  lastDirX_ = 0, lastDirY_ = 0;
        int  lastDirZ_ = 0, lastDirA_ = 0;

        float legStartX_ = 0, legStartY_ = 0, legStartZ_ = 0, legStartA_ = 0;
        float cmdDeltaX_ = 0, cmdDeltaY_ = 0, cmdDeltaZ_ = 0, cmdDeltaA_ = 0;

        // Ticks every kExtendTickMs while a session is active; tops up the
        // jog queue whenever the machine is approaching the commanded delta.
        Rev::Core::Animator extendTicker_ { kExtendTickMs };

        JogSection(Element* parent)
            : Box(parent, Theme::withPanel({ &Style::Section, &Style::SectionFocus }), "JogSection")
        {
            // The jog section is the keyboard target for axis jogging.  Click
            // on it (or any descendant) to focus it; the focus border style
            // then signals visually that arrow keys will be consumed for jog.
            this->tabStop = true;

            build();
            subscribe();

            // Extend the in-flight jog whenever the machine is approaching its
            // commanded delta -- keeps continuous-hold motion seamless without
            // ever queuing more than ~1 extra jog at a time.
            extendTicker_.onFrame([this](Rev::Core::AnimationEvent&) {
                maybeExtendLeg();
            });
        }

        void build() {

            new Text(this, "Jog", Theme::withText({ &Style::Label }));
            Box* body = new Box(this, { &Style::JogBody }, "JogBody");

            // -- Position panel (left) ----------------------------------

            Box* posPanel = new Box(body, { &Style::PosPanel }, "PosPanel");

            auto makeAxisRow = [&](const std::string& axis, Text*& out, const std::string& unit) {
                Box* row = new Box(posPanel, { &Style::PosRow }, "PosRow");
                new Text(row, axis, Theme::withMutedText({ &Style::PosAxis }));
                out = new Text(row, "---", Theme::withText({ &Style::PosValue }));
                new Text(row, unit, Theme::withMutedText({ &Style::PosUnit }));
            };

            makeAxisRow("X", posXText, "mm");
            makeAxisRow("Y", posYText, "mm");
            makeAxisRow("Z", posZText, "mm");
            makeAxisRow("A", posAText, "deg");

            Box* stepRow = new Box(posPanel, { &Style::StepRow }, "StepRow");
            new Text(stepRow, "step", Theme::withMutedText({ &Style::PosAxis }));
            stepText = new Text(stepRow, formatStep(), Theme::withText({ &Style::StepLabel }));

            // -- Jog grid (right) ---------------------------------------

            Box* grid = new Box(body, { &Style::JogGrid }, "JogGrid");

            Box* r0 = new Box(grid, { &Style::JogGridRow }, "R0");
            btnAM = makeJogBtn(r0, "A-"); btnAM->onClick([this](Event& e) { jogA(-jogStepDeg); e.propagate = false; });
            btnPY = makeJogBtn(r0, "+Y"); btnPY->onClick([this](Event& e) { jog(0,+jogStepMm,0); e.propagate = false; });
            btnAP = makeJogBtn(r0, "A+"); btnAP->onClick([this](Event& e) { jogA(+jogStepDeg); e.propagate = false; });

            Box* r1 = new Box(grid, { &Style::JogGridRow }, "R1");
            btnNX = makeJogBtn(r1, "-X"); btnNX->onClick([this](Event& e) { jog(-jogStepMm,0,0); e.propagate = false; });

            Box* zCell = new Box(r1, { &Style::ZCell }, "ZCell");
            btnPZ = new Box(zCell, Theme::withButton({ &Style::ZHalfBtn, &Style::BtnHover, &Style::BtnPress }), "ZP");
            new Text(btnPZ, "+Z", Theme::withText({ &Style::MutedLabel }));
            btnPZ->onClick([this](Event& e) { jog(0,0,+jogStepMm); e.propagate = false; });
            new Box(zCell, { &Style::ZHalfGap }, "ZG");
            btnNZ = new Box(zCell, Theme::withButton({ &Style::ZHalfBtn, &Style::BtnHover, &Style::BtnPress }), "ZN");
            new Text(btnNZ, "-Z", Theme::withText({ &Style::MutedLabel }));
            btnNZ->onClick([this](Event& e) { jog(0,0,-jogStepMm); e.propagate = false; });

            btnPX = makeJogBtn(r1, "+X"); btnPX->onClick([this](Event& e) { jog(+jogStepMm,0,0); e.propagate = false; });

            Box* r2 = new Box(grid, { &Style::JogGridRow }, "R2");
            makeJogBtn(r2, "-") ->onClick([this](Event& e) { adjustStep(-1); refresh(e); e.propagate = false; });
            btnNY = makeJogBtn(r2, "-Y"); btnNY->onClick([this](Event& e) { jog(0,-jogStepMm,0); e.propagate = false; });
            makeJogBtn(r2, "+") ->onClick([this](Event& e) { adjustStep(+1); refresh(e); e.propagate = false; });

            // TEMPORARY probing test button -- first step toward probe support.
            // Selects the wired probe tool and probes straight down until the
            // probe triggers (G38.2).  Watch the log for the "[PRB:...]" reply /
            // "Probe TRIGGERED" line.  See CarveraREADME.md (Probing section).
            Box* r3 = new Box(grid, { &Style::JogGridRow }, "R3");
            makeJogBtn(r3, "PROBE")->onClick([this](Event& e) { air().probeTest(); e.propagate = false; });
            // Probe the part's top face and immediately set the part coordinate
            // system's Z relative to the work frame (handled by the CAM view, which
            // knows the selected material state's known top).
            makeJogBtn(r3, "SET TOP")->onClick([this](Event& e) {
                if (air().onSetTop) { air().onSetTop(); }
                e.propagate = false;
            });
        }

        void subscribe() {

            // Only refresh when the displayed (3-decimal) readout would change.
            air().onTelemetryFrame([this](Carvera::Air::TelemetryEvent& e) {
                constexpr float kEps = 0.0005f;
                if (lastShownValid_ &&
                    std::abs(e.x - lastShownX_) < kEps &&
                    std::abs(e.y - lastShownY_) < kEps &&
                    std::abs(e.z - lastShownZ_) < kEps &&
                    std::abs(e.a - lastShownA_) < kEps) {
                    return;
                }
                lastShownX_ = e.x; lastShownY_ = e.y;
                lastShownZ_ = e.z; lastShownA_ = e.a;
                lastShownValid_ = true;
                if (shared && shared->event) { refresh(*shared->event); }
            });

            air().onConnection([this](Carvera::Air::ConnectionEvent&) {
                lastShownValid_ = false;
                cancelAllKeys();
                if (shared && shared->event) { refresh(*shared->event); }
            });
        }

        // -- Action forwarders (on-screen buttons) ----------------------

        void jog(float dx, float dy, float dz) { air().jog(dx, dy, dz); }
        void jogA(float degrees)               { air().jogA(degrees);   }

        void adjustStep(int delta) {
            stepIndex = std::clamp(stepIndex + delta, 0, kStepCount - 1);
            jogStepMm = kStepPresets[stepIndex];
            if (stepText) { stepText->content = formatStep(); }
        }

        std::string formatStep() const {
            return std::format("{:.4g} mm", kStepPresets[stepIndex]);
        }

        // -- Compute / render -------------------------------------------

        void computeChildren(Event& e) override {

            Carvera::Air& a = air();
            const bool isConnected = a.connected();

            // Read out the TOOL-TIP position (the controller's tip-referenced
            // WPos, already smoothed by Air).  Fall back to the machine
            // (spindle) position until a tip frame has arrived.
            float px = 0, py = 0, pz = 0, pa = 0;
            const bool havePos = a.tipTelemetry(px, py, pz, pa) ||
                                 a.livePosition(px, py, pz, pa);

            if (!isConnected || !havePos) {
                if (posXText) posXText->content = "---";
                if (posYText) posYText->content = "---";
                if (posZText) posZText->content = "---";
                if (posAText) posAText->content = "---";
            }
            else {
                if (posXText) posXText->content = std::format("{:.3f}", px);
                if (posYText) posYText->content = std::format("{:.3f}", py);
                if (posZText) posZText->content = std::format("{:.3f}", pz);
                if (posAText) posAText->content = std::format("{:.3f}", pa);
            }

            if (activeJogBtn != lastActiveJogBtn_) {
                if (lastActiveJogBtn_) { lastActiveJogBtn_->styles.remove(&Style::JogBtnActive); }
                if (activeJogBtn)      { activeJogBtn     ->styles.add   (&Style::JogBtnActive); }
                lastActiveJogBtn_ = activeJogBtn;
            }

            Box::computeChildren(e);
        }

        // ============================================================
        // Keyboard handling
        // ============================================================

        bool handleKeyDown(Event& e) {

            bool* state = stateForKey(e.keyboard.key);
            Box*  btn   = buttonForKey (e.keyboard.key);
            if (!state) { return false; }

            const bool wasDown = *state;
            *state = true;

            if (e.keyboard.shift) {
                // Continuous-hold session -- direction is recomputed from the
                // full key state inside updateContinuousSession.
                updateContinuousSession(e.keyboard.ctrl);
                activeJogBtn = btn;
            }
            else if (!wasDown) {
                // Incremental tap (first press only -- autorepeat is ignored).
                const float fac = e.keyboard.ctrl ? kFineFactor : 1.0f;
                emitTapStep(e.keyboard.key, fac);
                activeJogBtn = btn;
            }

            return true;
        }

        bool handleKeyUp(Event& e) {

            bool* state = stateForKey(e.keyboard.key);
            if (!state) { return false; }

            *state = false;

            // A release in the middle of a continuous session may change the
            // direction (or end the session entirely).
            if (sessionActive_) {
                updateContinuousSession(sessionFineMode_);
            }

            if (!anyAxisKeyDown()) { activeJogBtn = nullptr; }

            return true;
        }

    private:

        bool* stateForKey(const std::string& k) {
            if (k == "left")  return &xMinusDown_;
            if (k == "right") return &xPlusDown_;
            if (k == "up")    return &yPlusDown_;
            if (k == "down")  return &yMinusDown_;
            if (k == "q")     return &zPlusDown_;
            if (k == "e")     return &zMinusDown_;
            if (k == "z")     return &aMinusDown_;
            if (k == "x")     return &aPlusDown_;
            return nullptr;
        }

        Box* buttonForKey(const std::string& k) {
            if (k == "left")  return btnNX;
            if (k == "right") return btnPX;
            if (k == "up")    return btnPY;
            if (k == "down")  return btnNY;
            if (k == "q")     return btnPZ;
            if (k == "e")     return btnNZ;
            if (k == "z")     return btnAM;
            if (k == "x")     return btnAP;
            return nullptr;
        }

        bool anyAxisKeyDown() const {
            return xPlusDown_ || xMinusDown_ ||
                   yPlusDown_ || yMinusDown_ ||
                   zPlusDown_ || zMinusDown_ ||
                   aPlusDown_ || aMinusDown_;
        }

        void cancelAllKeys() {
            xPlusDown_ = xMinusDown_ = false;
            yPlusDown_ = yMinusDown_ = false;
            zPlusDown_ = zMinusDown_ = false;
            aPlusDown_ = aMinusDown_ = false;
            if (sessionActive_) {
                air().jogCancel();
                sessionActive_ = false;
                extendTicker_.stop();
            }
            activeJogBtn = nullptr;
        }

        void emitTapStep(const std::string& k, float fac) {
            const float s  = jogStepMm  * fac;
            const float sa = jogStepDeg * fac;
            if      (k == "left")  { jog(-s, 0, 0); }
            else if (k == "right") { jog(+s, 0, 0); }
            else if (k == "up")    { jog( 0,+s, 0); }
            else if (k == "down")  { jog( 0,-s, 0); }
            else if (k == "q")     { jog( 0, 0,+s); }
            else if (k == "e")     { jog( 0, 0,-s); }
            else if (k == "z")     { jogA(-sa);     }
            else if (k == "x")     { jogA(+sa);     }
        }

        // The currently-held direction (per axis, signed -1/0/+1).
        struct Dir { int x, y, z, a; bool zero() const { return !x && !y && !z && !a; } };
        Dir currentDirection() const {
            return {
                (xPlusDown_ ? 1 : 0) - (xMinusDown_ ? 1 : 0),
                (yPlusDown_ ? 1 : 0) - (yMinusDown_ ? 1 : 0),
                (zPlusDown_ ? 1 : 0) - (zMinusDown_ ? 1 : 0),
                (aPlusDown_ ? 1 : 0) - (aMinusDown_ ? 1 : 0)
            };
        }

        // Decide what to do given the current key state.  Called on every
        // shift-key transition (and on release while a session is active).
        //
        //   no session, no dir  -> nothing
        //   no session, dir     -> start a session, emit one big $J toward
        //                         a far target in the held direction
        //   session,    dir same-> nothing (jog already in flight)
        //   session,    dir new -> jog cancel + new big $J toward new dir
        //   session,    no dir  -> jog cancel + final $J snapped to grid;
        //                         end session
        void updateContinuousSession(bool ctrlHeld) {

            const Dir dir = currentDirection();

            if (dir.zero()) {
                if (sessionActive_) { endSessionAtGrid(); }
                return;
            }

            if (!sessionActive_) {
                if (!startLeg(dir, ctrlHeld)) { return; }
                emitChunk();
                extendTicker_.play();
                return;
            }

            const bool dirChanged =
                dir.x != lastDirX_ || dir.y != lastDirY_ ||
                dir.z != lastDirZ_ || dir.a != lastDirA_;

            if (dirChanged) {
                // Cancel the in-flight jog, then start a fresh leg from
                // wherever the machine ends up after deceleration.
                air().jogCancel();
                if (!startLeg(dir, sessionFineMode_)) { return; }
                emitChunk();
            }
        }

        // Snapshot the machine position as the start of a new leg in `dir`.
        // Returns false if we don't have telemetry yet (caller bails out).
        bool startLeg(const Dir& dir, bool fineMode) {
            float fx, fy, fz, fa;
            if (!air().livePosition(fx, fy, fz, fa)) { return false; }

            sessionActive_   = true;
            sessionFineMode_ = fineMode;
            legStartX_ = fx; legStartY_ = fy; legStartZ_ = fz; legStartA_ = fa;
            cmdDeltaX_ = cmdDeltaY_ = cmdDeltaZ_ = cmdDeltaA_ = 0.0f;
            lastDirX_  = dir.x; lastDirY_ = dir.y;
            lastDirZ_  = dir.z; lastDirA_ = dir.a;
            return true;
        }

        // Emit ONE $J=G91 chunk in the leg's direction.  Small enough that the
        // soft-limit checker never sees a runaway target; the extend ticker
        // queues another chunk before the machine catches up so motion stays
        // continuous.  Relative (G91) so no WCS arithmetic is needed.
        void emitChunk() {
            const float dx = lastDirX_ * kChunkMm;
            const float dy = lastDirY_ * kChunkMm;
            const float dz = lastDirZ_ * kChunkMm;
            const float da = lastDirA_ * kChunkDeg;

            cmdDeltaX_ += dx; cmdDeltaY_ += dy;
            cmdDeltaZ_ += dz; cmdDeltaA_ += da;

            air().jogRel(dx, dy, dz, da, kJogFeedMmMin);
        }

        // While a session is active, top up the queue when the machine is
        // approaching the end of what we've already commanded.  GRBL queues
        // the next $J=G91 after the current one and the planner combines
        // consecutive colinear jogs into a single uninterrupted motion.
        void maybeExtendLeg() {

            if (!sessionActive_) { extendTicker_.stop(); return; }

            float fx, fy, fz, fa;
            if (!air().livePosition(fx, fy, fz, fa)) { return; }

            // Per-axis "remaining" = commanded delta ? actual delta, signed in
            // the direction of motion (so positive means "still going").
            auto remaining = [](float start, float current, float cmd, int dir) {
                if (dir == 0) { return std::numeric_limits<float>::infinity(); }
                const float actual = (current - start) * (float)dir;
                const float commanded = cmd * (float)dir;
                return commanded - actual;
            };

            const float remX = remaining(legStartX_, fx, cmdDeltaX_, lastDirX_);
            const float remY = remaining(legStartY_, fy, cmdDeltaY_, lastDirY_);
            const float remZ = remaining(legStartZ_, fz, cmdDeltaZ_, lastDirZ_);
            const float remA = remaining(legStartA_, fa, cmdDeltaA_, lastDirA_);

            const float threshMm  = kChunkMm  * kExtendFraction;
            const float threshDeg = kChunkDeg * kExtendFraction;

            const bool nearTarget =
                remX < threshMm  || remY < threshMm ||
                remZ < threshMm  || remA < threshDeg;

            if (nearTarget) { emitChunk(); }
        }

        // End the session: jog-cancel any in-flight motion, then emit ONE
        // final $J=G91 whose delta snaps each moving axis from its current
        // TOOL-TIP position (WPos) UP to the next jogStepMm multiple (in the
        // direction of motion) -- so the WPos readout lands on the grid, matching
        // every other position in the app.  The delta is relative ($J=G91), so it is
        // frame-independent; only the grid TARGET is taken in the tip frame.  The
        // safety margin keeps the snap forward of the cancel deceleration so the
        // final move is never a reversal.  Axes not moving this leg get a 0 delta.
        void endSessionAtGrid() {

            sessionActive_ = false;
            extendTicker_.stop();
            air().jogCancel();

            float fx, fy, fz, fa;
            if (!air().tipTelemetry(fx, fy, fz, fa) &&
                !air().livePosition(fx, fy, fz, fa)) { return; }

            const float step  = jogStepMm  * (sessionFineMode_ ? kFineFactor : 1.0f);
            const float stepA = jogStepDeg * (sessionFineMode_ ? kFineFactor : 1.0f);

            auto snapDelta = [](float current, int dir, float stepSize, float safety) -> float {
                if (dir == 0)         { return 0.0f; }
                if (stepSize <= 0.0f) { return 0.0f; }
                const float pushed = current + dir * safety;
                const float target = dir > 0
                    ? std::ceil (pushed / stepSize) * stepSize
                    : std::floor(pushed / stepSize) * stepSize;
                return target - current;
            };

            const float dx = snapDelta(fx, lastDirX_, step,  kSafetyMm);
            const float dy = snapDelta(fy, lastDirY_, step,  kSafetyMm);
            const float dz = snapDelta(fz, lastDirZ_, step,  kSafetyMm);
            const float da = snapDelta(fa, lastDirA_, stepA, kSafetyDeg);

            air().jogRel(dx, dy, dz, da, kJogFeedMmMin);
        }
    };
}
