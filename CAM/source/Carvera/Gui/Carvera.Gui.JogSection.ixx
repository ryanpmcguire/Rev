module;

#include <string>
#include <format>
#include <algorithm>
#include <cmath>

#include <managed.hpp>

export module Carvera.Gui.JogSection;

import Rev.Core.Animator;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;
import Rev.Element.Text;

import CarveraAir;

import Cam.Gui.Theme;
import Carvera.Gui.Style;

export namespace Carvera::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace Theme = Cam::Gui::Theme;

    // Jog section — live position readout + ±X/±Y/±Z/±A jog grid + step size.
    //
    // Keyboard plumbing (called by the Interface root via handleKeyDown /
    // handleKeyUp):
    //
    //   ←/→ : −X/+X            shift   : continuous-hold mode
    //   ↑/↓ : +Y/−Y            ctrl    : fine (×0.1) increment / feed
    //   q/e : +Z/−Z
    //   z/x : −A/+A
    //
    // Without shift  a key press emits ONE step jog (autorepeat is ignored —
    //                you must release and press again to step again).
    // With shift     the section enters continuous-hold mode and a small
    //                animator trickle-sends step-sized $J jogs in the current
    //                key direction.  Opposing keys cancel; multi-axis combines.
    //                On release we simply stop emitting — the in-flight step
    //                jogs naturally complete on a `jogStepMm` grid boundary
    //                (no jog-cancel byte needed, so the snap-to-grid guarantee
    //                comes for free).
    struct JogSection : public Box {

        static Carvera::Air& air() { return Carvera::Air::instance(); }

        // -- Step presets (UI-only) -------------------------------------

        static constexpr float kStepPresets[] = { 0.01f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
        static constexpr int   kStepCount     = 6;
        static constexpr float kFineFactor    = 0.1f;   // ctrl modifier

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

        // -- Telemetry display throttle (140 Hz → only when value changes) ---

        float lastShownX_ = 0, lastShownY_ = 0, lastShownZ_ = 0, lastShownA_ = 0;
        bool  lastShownValid_ = false;
        Box*  lastActiveJogBtn_ = nullptr;

        // -- Continuous-hold state -------------------------------------

        // Per-axis pressed state (we track our own because autorepeat / shift
        // changes can't be inferred from a single event).
        bool xPlusDown_ = false, xMinusDown_ = false;
        bool yPlusDown_ = false, yMinusDown_ = false;
        bool zPlusDown_ = false, zMinusDown_ = false;
        bool aPlusDown_ = false, aMinusDown_ = false;

        bool continuousMode_ = false;
        bool fineMode_       = false;   // ctrl was held when continuous started

        // Trickle-sends step jogs while keys are held with shift.  Period is
        // re-computed on entry from jogStepMm + jogFeedRate so the planner
        // stays approximately one step ahead.
        Rev::Core::Animator continuousTicker_ { 60 };

        JogSection(Element* parent)
            : Box(parent, Theme::withPanel({ &Style::Section }), "JogSection")
        {
            build();
            subscribe();

            continuousTicker_.onFrame([this](Rev::Core::AnimationEvent&) { continuousTick(); });
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
            //
            //   [A-]    [+Y]    [A+]
            //   [-X]   [+Z/-Z]  [+X]
            //  [step-]  [-Y]  [step+]

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

        // -- Action forwarders -----------------------------------------

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

            float px = 0, py = 0, pz = 0, pa = 0;
            const bool havePos = a.livePosition(px, py, pz, pa);

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

            // Jog-button highlight — only swap when the active button changed.
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

        // True if the key was a recognised jog key (consume the event).
        bool handleKeyDown(Event& e) {

            bool* state = stateForKey(e.keyboard.key);
            Box*  btn   = buttonForKey (e.keyboard.key);
            if (!state) { return false; }

            const bool wasDown = *state;
            *state = true;

            if (e.keyboard.shift) {
                // Continuous-hold.  Start the ticker if this is the first
                // axis key down; otherwise it's already running and the new
                // key just adds to the direction.
                if (!continuousMode_) {
                    continuousMode_ = true;
                    fineMode_       = (bool)e.keyboard.ctrl;
                    schedulePace();
                    continuousTicker_.play();
                }
                activeJogBtn = btn;
            }
            else if (!wasDown) {
                // Incremental tap (first press only — OS autorepeat is ignored).
                const float fac = e.keyboard.ctrl ? kFineFactor : 1.0f;
                emitTapStep(e.keyboard.key, fac);
                activeJogBtn = btn;
            }
            // else: autorepeat under tap mode — silently ignore.

            return true;
        }

        bool handleKeyUp(Event& e) {

            bool* state = stateForKey(e.keyboard.key);
            if (!state) { return false; }

            *state = false;

            if (!anyAxisKeyDown()) {
                if (continuousMode_) {
                    // Stop emitting.  Buffered step jogs complete naturally
                    // and land on a `jogStepMm` boundary — no jog-cancel byte
                    // sent, so we keep the snap-to-grid guarantee.
                    continuousMode_ = false;
                    continuousTicker_.stop();
                }
                activeJogBtn = nullptr;
            }

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
            if (continuousMode_) {
                continuousMode_ = false;
                continuousTicker_.stop();
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

        // Roughly match the ticker to one step's travel time so the planner
        // stays about one step ahead — short enough to feel continuous, long
        // enough that releasing a key doesn't sit on a deep buffer.
        // Clamped to [40, 200] ms.
        void schedulePace() {
            constexpr float kJogFeedMmMin = 1000.0f;   // matches Air::jog feed
            const float stepTimeMs = jogStepMm * 60000.0f / kJogFeedMmMin;
            uint64_t periodMs = (uint64_t)std::clamp(stepTimeMs, 40.0f, 200.0f);
            continuousTicker_.setPeriod(periodMs);
        }

        // Issue one step-sized jog in the currently-held direction.  Opposing
        // keys cancel out; a zero direction emits nothing.
        void continuousTick() {

            if (!continuousMode_) { continuousTicker_.stop(); return; }

            const int dx = (xPlusDown_ ? 1 : 0) - (xMinusDown_ ? 1 : 0);
            const int dy = (yPlusDown_ ? 1 : 0) - (yMinusDown_ ? 1 : 0);
            const int dz = (zPlusDown_ ? 1 : 0) - (zMinusDown_ ? 1 : 0);
            const int da = (aPlusDown_ ? 1 : 0) - (aMinusDown_ ? 1 : 0);

            if (dx == 0 && dy == 0 && dz == 0 && da == 0) { return; }

            const float fac = fineMode_ ? kFineFactor : 1.0f;
            const float s   = jogStepMm  * fac;
            const float sa  = jogStepDeg * fac;

            if (dx || dy || dz) { jog(dx * s, dy * s, dz * s); }
            if (da)             { jogA(da * sa);               }
        }
    };
}
