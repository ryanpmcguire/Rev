module;

#include <string>
#include <format>
#include <vector>
#include <array>

#include <dbg.hpp>

export module Gui.Machine.Jog;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;

import Rev.Core.Animator;

import Machine.Base;
import Gui.Machine.Connect;   // reuse hover / press / label button styles

export namespace Gui {

    using namespace Rev::Element;
    using namespace Rev::Appearance;

    // Pin Text to the element (there is also a Rev::Primitive::Text in scope).
    using Text = Rev::Element::Text;

    // The Jog section: a 3x3 directional grid laid out like the original Carvera
    // control window --
    //
    //     A-   +Y   A+
    //     -X  +Z/-Z  +X
    //     -    -Y    +
    //
    // -- with the Z axis stacked in the centre and the step -/+ in the bottom
    // corners, over a "step" readout. Mouse: press-and-hold a key to jog that axis.
    // Keyboard (while focused): arrows jog X/Y, Alt remaps to Z/A, Shift holds,
    // Ctrl is fine+slow.
    struct JogSection : public Box {

        // Styles
        //--------------------------------------------------

        static inline Style Section = {
            .layout     = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size       = { .width = 100_pct },
            .margin     = { .top = 12_px },
            .padding    = { 14_px, 14_px, 14_px, 14_px },
            .background = { .color = rgba(255, 255, 255, 0.03) },
            .border     = { .color = rgba(255, 255, 255, 0.08), .radius = 8_px, .width = 1_px }
        };

        static inline Style Heading = {
            .margin = { .bottom = 12_px },
            .text   = { .color = rgba(236, 238, 242, 1.0), .size = 15_px }
        };

        static inline Style Grid    = { .layout = { Axis::Vertical,   Align::Start, Align::Start, Wrap::False } };
        static inline Style GridRow = { .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False } };

        // A square jog key.
        static inline Style JogBtn = {
            .layout     = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size       = { .width = 46_px, .height = 34_px },
            .margin     = { 3_px, 3_px, 3_px, 3_px },
            .background = { .color = rgba(255, 255, 255, 0.06), .transition = 120_ms },
            .border     = { .color = rgba(255, 255, 255, 0.10), .radius = 4_px, .width = 1_px, .transition = 120_ms },
            .cursor     = Cursor::Hand
        };

        // The centre cell, holding +Z over -Z as two half-height keys.
        static inline Style ZCell = {
            .layout = { Axis::Vertical, Align::Center, Align::Center, Wrap::False },
            .size   = { .width = 46_px, .height = 34_px },
            .margin = { 3_px, 3_px, 3_px, 3_px }
        };

        static inline Style ZHalfBtn = {
            .layout     = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size       = { .width = 46_px, .height = 15_px },
            .background = { .color = rgba(255, 255, 255, 0.06), .transition = 120_ms },
            .border     = { .color = rgba(255, 255, 255, 0.10), .radius = 3_px, .width = 1_px, .transition = 120_ms },
            .cursor     = Cursor::Hand
        };

        static inline Style ZHalfGap = { .size = { .height = 4_px } };

        // Highlight applied to a key while its direction is part of the current
        // jog intent. Added last so it wins over the base / hover / press fills.
        static inline Style JogActive = {
            .background = { .color = rgba(128, 206, 230, 0.28), .transition = 120_ms },
            .border     = { .color = rgba(128, 206, 230, 0.90), .width = 1_px, .transition = 120_ms }
        };

        static inline Style StepRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False, CrossAlign::True },
            .size   = { .width = 100_pct },
            .margin = { .top = 10_px }
        };

        static inline Style StepLabel = {
            .margin = { .right = 8_px },
            .text   = { .color = rgba(120, 126, 136, 1.0), .size = 12_px }
        };

        static inline Style StepValue = {
            .text = { .color = rgba(214, 218, 224, 1.0), .size = 13_px }
        };

        // Elements
        //--------------------------------------------------

        Box* grid = nullptr;
            Button* aMinus = nullptr;  Button* yPlus  = nullptr;  Button* aPlus = nullptr;
            Button* xMinus = nullptr;                             Button* xPlus = nullptr;
                Box*    zCell = nullptr;
                    Button* zPlus = nullptr;  Button* zMinus = nullptr;
            Button* stepDown = nullptr; Button* yMinus = nullptr; Button* stepUp = nullptr;

        Box*  stepRow   = nullptr;
            Text* stepValue = nullptr;

        // State
        //--------------------------------------------------

        Machine::MachineBase& machine;

        // Step presets, paired so the linear (mm) and rotary (deg) steps move
        // together. Kept sorted by mm -- adjustStep walks to the neighbouring entry.
        struct StepPreset { float mm, deg; };
        static inline const std::vector<StepPreset> StepPresets = {
            { 0.01f, 0.1f }, { 0.1f, 0.5f }, { 0.5f, 1.0f }, { 1.0f, 5.0f }, { 5.0f, 15.0f }, { 10.0f, 30.0f }
        };

        // Track jog intent: the intended directions plus the step magnitude of the
        // move. The selected step (jog quantization + speed dial) lives here too, in
        // intent.stepMm / intent.stepDeg -- there is no separate copy on the element.
        struct Intent {

            // Bool map (seems stupid but is necessary)
            bool  xPos = false, xNeg = false;
            bool  yPos = false, yNeg = false;
            bool  zPos = false, zNeg = false;
            bool  aPos = false, aNeg = false;

            float stepMm = 1.0f, stepDeg = 5.0f;

            // Hold the jog: repeat it while the input is held (Shift). When false the
            // jog is a single one-shot move -- sent once, not animated.
            bool hold = false;

            bool rotary() const { return aPos || aNeg; }
            bool moving() const { return xPos || xNeg || yPos || yNeg || zPos || zNeg || aPos || aNeg; }

            // A direction held now that was not held in `prev` -- i.e. a new keypress.
            // Drives the immediate (unheld) dispatch so pressing a key while another
            // is held still sends the new combined intent.
            bool newPress(const Intent& prev) const {
                return (xPos && !prev.xPos) || (xNeg && !prev.xNeg)
                    || (yPos && !prev.yPos) || (yNeg && !prev.yNeg)
                    || (zPos && !prev.zPos) || (zNeg && !prev.zNeg)
                    || (aPos && !prev.aPos) || (aNeg && !prev.aNeg);
            }

            // The intended direction as per-axis signs (-1 / 0 / +1); the step
            // magnitudes travel alongside in stepMm / stepDeg.
            Machine::Coord direction() const {
                return {
                    .x = float(xPos - xNeg),
                    .y = float(yPos - yNeg),
                    .z = float(zPos - zNeg),
                    .a = float(aPos - aNeg),
                };
            }
        };

        Intent intent;   // the unified intent currently reflected onto the keys

        std::string stepText;   // the step readout currently shown (so it only re-sets on change)

        // The jog is pushed to the machine on this cadence, but only while there is
        // motion to drive: the animator plays on the leading edge of intent and stops
        // when it clears, so nothing ticks at rest.
        static constexpr uint64_t JogIntervalMs = 100;
        Rev::Core::Animator jogAnimator{ JogIntervalMs };

        // The direction each jog key drives. update() (reads press flags) and
        // reflect() (highlights) both walk this, so the mapping lives in one place.
        struct Bind { Button* btn; bool Intent::* dir; };
        std::array<Bind, 8> binds{};

        // Create
        //--------------------------------------------------

        JogSection(Element* parent, Machine::MachineBase& machine)
            : Box(parent, { &Section }, "JogSection"), machine(machine) {

            new Text(this, "Jog", { &Heading });

            grid = new Box(this, { &Grid }, "JogGrid");

                Box* row0 = new Box(grid, { &GridRow }, "Row0");
                    aMinus = JogButton(row0, "A-");  yPlus = JogButton(row0, "+Y");  aPlus = JogButton(row0, "A+");

                Box* row1 = new Box(grid, { &GridRow }, "Row1");
                    xMinus = JogButton(row1, "-X");

                    zCell = new Box(row1, { &ZCell }, "ZCell");
                        zPlus = JogButton(zCell, "+Z", &ZHalfBtn);
                        new Box(zCell, { &ZHalfGap }, "ZGap");
                        zMinus = JogButton(zCell, "-Z", &ZHalfBtn);

                    xPlus = JogButton(row1, "+X");

                Box* row2 = new Box(grid, { &GridRow }, "Row2");
                    stepDown = key(row2, "-");  yMinus = JogButton(row2, "-Y");  stepUp = key(row2, "+");

            binds = {{
                { xPlus, &Intent::xPos }, { xMinus, &Intent::xNeg },
                { yPlus, &Intent::yPos }, { yMinus, &Intent::yNeg },
                { zPlus, &Intent::zPos }, { zMinus, &Intent::zNeg },
                { aPlus, &Intent::aPos }, { aMinus, &Intent::aNeg },
            }};

            stepRow = new Box(this, { &StepRow }, "StepRow");
                new Text(stepRow, "step", { &StepLabel });
                stepText  = formatStep(intent);
                stepValue = new Text(stepRow, stepText, { &StepValue });

            // Arrows jog while the section is focused.
            tabStop = true;

            // Re-derive the intent whenever the held inputs could have changed.
            onKeyDown  ([this](Event& e) { update(e); });
            onKeyUp    ([this](Event& e) { update(e); });
            onLoseFocus([this](Event& e) { update(e); });

            // Step corners cycle the preset, then refresh the readout.
            stepDown->onClick([this](Event& e) { adjustStep(-1); update(e); });
            stepUp  ->onClick([this](Event& e) { adjustStep(+1); update(e); });

            // While playing, the animator re-sends the held jog each frame; update()
            // plays / stops it as the intent gains / loses motion.
            jogAnimator.onFrame([this](Rev::Core::AnimationEvent&) { sendJog(intent); });
        }

        // Destroy
        //--------------------------------------------------

        ~JogSection() { jogAnimator.stop(); }

        // Builders
        //--------------------------------------------------

        // A standard square jog key (reused eight times -- worth the builder).
        Button* key(Element* parent, const std::string& label) {
            return new Button(parent,
                { .label = label, .labelStyles = { &ConnectSection::BtnLabel } },
                { &JogBtn, &ConnectSection::BtnHover, &ConnectSection::BtnPress });
        }

        // Step
        //--------------------------------------------------

        // Step to the neighbouring preset by mm: up -> the next greater entry,
        // down -> the previous lesser. A rare action, so a linear scan is fine.
        void adjustStep(int delta) {

            const StepPreset* best = nullptr;
            for (const StepPreset& p : StepPresets) {
                if (delta > 0 && p.mm > intent.stepMm && (!best || p.mm < best->mm)) { best = &p; }
                if (delta < 0 && p.mm < intent.stepMm && (!best || p.mm > best->mm)) { best = &p; }
            }

            if (best) { intent.stepMm = best->mm; intent.stepDeg = best->deg; }
            // The readout follows from the intent; the next sendJog() uses the new step.
        }

        // The step readout: the linear step always, plus the rotary step when a
        // rotary axis is part of the intended move.
        std::string formatStep(const Intent& it) const {
            std::string s = std::format("{:g} mm", it.stepMm);
            if (it.rotary()) { s += std::format("  {:g} deg", it.stepDeg); }
            return s;
        }

        // Input
        //--------------------------------------------------

        // A jog key: builds the button (square by default, or a half-height Z key).
        // Press/release just re-derive the intent -- the held state is read back off
        // the button's captured `press` flag, so a release off the key still clears.
        Button* JogButton(Element* parent, const std::string& label, Style* box = &JogBtn) {

            Button* b = new Button(parent,
                { .label = label, .labelStyles = { &ConnectSection::BtnLabel } },
                { box, &ConnectSection::BtnHover, &ConnectSection::BtnPress });

            b->onMouseDown([this](Event& e) { update(e); });
            b->onMouseUp  ([this](Event& e) { update(e); });

            return b;
        }

        // Keyboard: arrows jog X/Y; Alt remaps them to Z (up/down) and A (left/right),
        // matching the grid. Folded into the intent only while the section holds focus.
        void addKeyboard(Intent& it, Event& e) const {

            if (!targetFlags.focus) { return; }

            auto& a = e.keyboard.arrows;

            if (e.keyboard.alt) {
                it.zPos |= a.up;   it.zNeg |= a.down;
                it.aNeg |= a.left; it.aPos |= a.right;
            } else {
                it.yPos |= a.up;   it.yNeg |= a.down;
                it.xNeg |= a.left; it.xPos |= a.right;
            }
        }

        // Recompute the unified intent from the live held inputs -- each key's
        // captured `press` flag plus the keyboard -- and reflect it onto the keys.
        void update(Event& e) {

            Intent next;
            for (const Bind& b : binds) { next.*(b.dir) = b.btn->targetFlags.press; }
            addKeyboard(next, e);
            next.stepMm = intent.stepMm;  next.stepDeg = intent.stepDeg;
            next.hold = e.keyboard.shift;   // Shift == hold (repeat) the jog

            reflect(next);

            // 1. Only a held jog runs the animator; otherwise it never ticks.
            if (next.hold && next.moving()) { jogAnimator.play(); }
            else { jogAnimator.stop(); }

            // 2. Unheld: each new keypress immediately dispatches a one-shot jog built
            //    from the new intent (a synthetic Coord). Releases don't dispatch.
            if (!next.hold && next.newPress(intent)) { sendJog(next); }

            // Commit the new intent -- the animator reads it while a hold is playing.
            intent = next;
        }

        // One jog submission for the given intent: the held jog the animator pumps
        // each frame, or a synthetic one-shot for an immediate (unheld) keypress.
        void sendJog(const Intent& it) {

            dbg("sending jog");

            machine.jog(it.direction(), it.stepMm, it.stepDeg, it.hold);
        }

        // Reflect the intent: highlight each key whose direction just turned on,
        // un-highlight each that turned off. Diffed against the current intent so
        // only real edges touch the style list.
        void reflect(const Intent& next) {

            for (const Bind& b : binds) { toggle(b.btn, next.*(b.dir), intent.*(b.dir)); }

            // Step readout follows the intent (gains the rotary step while jogging A).
            std::string s = formatStep(next);
            if (s != stepText) { stepValue->setContent(s); stepText = s; }
        }

        void toggle(Button* key, bool on, bool was) {
            if (on == was) { return; }
            if (on) { key->styles.add(&JogActive); }
            else    { key->styles.remove(&JogActive); }
        }
    };
}
