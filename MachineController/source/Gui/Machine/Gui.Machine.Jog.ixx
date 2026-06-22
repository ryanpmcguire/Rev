module;

#include <string>
#include <format>

export module Gui.Machine.Jog;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;

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
            .border     = { .color = rgba(255, 255, 255, 0.08), .radius = 6_px, .width = 1_px }
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

        static constexpr float StepPresets[] = { 0.01f, 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
        static constexpr int   StepCount     = 6;

        int   stepIndex = 3;             // -> 1.0 mm
        float stepMm    = 1.0f;          // the selected step: jog quantization + speed dial

        // The step sets both quantization and feed; Control drops each by an order
        // of magnitude (finer + slower).
        float jogQuantum(bool ctrl) const { return ctrl ? stepMm * 0.1f : stepMm; }
        int   jogSpeed  (bool ctrl) const { return static_cast<int>(stepMm * 600.0f * (ctrl ? 0.1f : 1.0f)); }

        // Create
        //--------------------------------------------------

        JogSection(Element* parent, Machine::MachineBase& machine)
            : Box(parent, { &Section }, "JogSection"), machine(machine) {

            new Text(this, "Jog", { &Heading });

            grid = new Box(this, { &Grid }, "JogGrid");

                Box* row0 = new Box(grid, { &GridRow }, "Row0");
                    aMinus = key(row0, "A-");  yPlus = key(row0, "+Y");  aPlus = key(row0, "A+");

                Box* row1 = new Box(grid, { &GridRow }, "Row1");
                    xMinus = key(row1, "-X");

                    zCell = new Box(row1, { &ZCell }, "ZCell");
                        zPlus = zKey(zCell, "+Z");
                        new Box(zCell, { &ZHalfGap }, "ZGap");
                        zMinus = zKey(zCell, "-Z");

                    xPlus = key(row1, "+X");

                Box* row2 = new Box(grid, { &GridRow }, "Row2");
                    stepDown = key(row2, "-");  yMinus = key(row2, "-Y");  stepUp = key(row2, "+");

            stepRow = new Box(this, { &StepRow }, "StepRow");
                new Text(stepRow, "step", { &StepLabel });
                stepValue = new Text(stepRow, formatStep(), { &StepValue });

            // Axis keys jog on press-and-hold.
            holdToJog(xPlus, +1,  0,  0,  0);  holdToJog(xMinus, -1,  0,  0,  0);
            holdToJog(yPlus,  0, +1,  0,  0);  holdToJog(yMinus,  0, -1,  0,  0);
            holdToJog(zPlus,  0,  0, +1,  0);  holdToJog(zMinus,  0,  0, -1,  0);
            holdToJog(aPlus,  0,  0,  0, +1);  holdToJog(aMinus,  0,  0,  0, -1);

            // Step corners cycle the preset.
            stepDown->onClick([this](Event&) { adjustStep(-1); });
            stepUp  ->onClick([this](Event&) { adjustStep(+1); });
        }

        // Destroy
        //--------------------------------------------------

        // Never leave the machine jogging if the section dies mid-hold.
        ~JogSection() { machine.pauseJog(); }

        // Builders
        //--------------------------------------------------

        // A standard square jog key (reused eight times -- worth the builder).
        Button* key(Element* parent, const std::string& label) {
            return new Button(parent,
                { .label = label, .labelStyles = { &ConnectSection::BtnLabel } },
                { &JogBtn, &ConnectSection::BtnHover, &ConnectSection::BtnPress });
        }

        // A half-height key for the stacked Z cell.
        Button* zKey(Element* parent, const std::string& label) {
            return new Button(parent,
                { .label = label, .labelStyles = { &ConnectSection::BtnLabel } },
                { &ZHalfBtn, &ConnectSection::BtnHover, &ConnectSection::BtnPress });
        }

        // Wire a key to press-and-hold jogging of one axis (release / drag-off
        // pauses). Mouse jog uses the plain step/speed -- no modifiers.
        void holdToJog(Button* key, float dx, float dy, float dz, float da) {
            key->onMouseDown ([this, dx, dy, dz, da](Event&) { machine.holdJog(dx, dy, dz, da, jogQuantum(false), jogSpeed(false)); });
            key->onMouseUp   ([this](Event&) { machine.pauseJog(); });
            key->onMouseLeave([this](Event&) { machine.pauseJog(); });
        }

        // Step
        //--------------------------------------------------

        void adjustStep(int delta) {
            stepIndex += delta;
            if (stepIndex < 0)          { stepIndex = 0; }
            if (stepIndex >= StepCount) { stepIndex = StepCount - 1; }
            stepMm = StepPresets[stepIndex];
            stepValue->setContent(formatStep());
        }

        std::string formatStep() const { return std::format("{:g} mm", stepMm); }

        // Keyboard jog
        //--------------------------------------------------
        // Arrows jog X/Y. Alt ("alternate") remaps Up/Down -> Z and Left/Right -> A.
        // Shift makes it a continuous hold; without Shift each press is a single
        // step. Control gives a finer + slower jog. We act only while focused, so
        // click the jog pad to "arm" the arrows.

        // The jog the keyboard is asking for. Comparing successive intents lets us
        // act only on real transitions -- a press, a release, a direction or mode
        // change -- and ignore OS auto-repeat, which re-sends the same intent.
        struct Intent {
            float dx = 0, dy = 0, dz = 0, da = 0;
            bool  hold = false;   // Shift -> continuous

            bool moving() const { return dx != 0.0f || dy != 0.0f || dz != 0.0f || da != 0.0f; }
            bool operator==(const Intent& o) const {
                return dx == o.dx && dy == o.dy && dz == o.dz && da == o.da && hold == o.hold;
            }
        };

        Intent lastIntent;   // the intent we last acted on

        void keyDown(Event& e) override { Box::keyDown(e); applyIntent(e); }
        void keyUp  (Event& e) override { Box::keyUp(e);   applyIntent(e); }

        // Losing focus mid-hold can't leave the machine moving with no key-up coming.
        void loseFocus(Event& e) override {
            Box::loseFocus(e);
            machine.pauseJog();
            lastIntent = {};
        }

        // Act on the keyboard's jog intent, but only when it differs from last time.
        void applyIntent(Event& e) {

            Intent now = targetFlags.focus ? readIntent(e) : Intent{};   // unfocused -> no jog
            if (now == lastIntent) { return; }                           // unchanged (e.g. auto-repeat)
            lastIntent = now;

            if      (!now.moving()) { machine.pauseJog(); }
            else if (now.hold)      { machine.holdJog(now.dx, now.dy, now.dz, now.da, jogQuantum(e.keyboard.ctrl), jogSpeed(e.keyboard.ctrl)); }
            else                    { machine.stepJog(now.dx, now.dy, now.dz, now.da, jogQuantum(e.keyboard.ctrl), jogSpeed(e.keyboard.ctrl)); }
        }

        // Read the current intent from the keyboard. Alt ("alternate") remaps
        // Up/Down -> Z and Left/Right -> A; opposite keys cancel; Shift = hold.
        Intent readIntent(Event& e) {

            Event::Keyboard::Arrows& a = e.keyboard.arrows;
            Intent i;
            i.hold = e.keyboard.shift;

            if (!e.keyboard.alt) {
                if (a.left)  { i.dx -= 1.0f; }
                if (a.right) { i.dx += 1.0f; }
                if (a.up)    { i.dy += 1.0f; }
                if (a.down)  { i.dy -= 1.0f; }
            } else {
                if (a.up)    { i.dz += 1.0f; }
                if (a.down)  { i.dz -= 1.0f; }
                if (a.left)  { i.da -= 1.0f; }
                if (a.right) { i.da += 1.0f; }
            }

            return i;
        }
    };
}
