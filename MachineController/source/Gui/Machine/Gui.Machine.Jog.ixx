module;

#include <string>

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

    // The Jog section: a directional pad + step selector. Structure only for now
    // -- no machine wiring (the jog keys carry no handlers yet); the step selector
    // is pure GUI state. A clean, click-per-step take on the CAM jog grid.
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

        static inline Style Body    = { .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False } };
        static inline Style Col     = { .layout = { Axis::Vertical,   Align::Start, Align::Start, Wrap::False } };
        static inline Style PadRow  = { .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False } };
        static inline Style ColGap  = { .layout = { Axis::Vertical,   Align::Start, Align::Start, Wrap::False }, .margin = { .left = 10_px } };

        // A square jog key.
        static inline Style JogBtn = {
            .layout     = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size       = { .width = 46_px, .height = 34_px },
            .margin     = { 3_px, 3_px, 3_px, 3_px },
            .background = { .color = rgba(255, 255, 255, 0.06), .transition = 120_ms },
            .border     = { .color = rgba(255, 255, 255, 0.10), .radius = 6_px, .width = 1_px, .transition = 120_ms },
            .cursor     = Cursor::Hand
        };

        // An empty cell that holds the pad's cross shape.
        static inline Style JogSpacer = {
            .size   = { .width = 46_px, .height = 34_px },
            .margin = { 3_px, 3_px, 3_px, 3_px }
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

        static inline Style StepBtn = {
            .layout     = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .size       = { .width = 44_px, .height = 28_px },
            .margin     = { 3_px, 3_px, 3_px, 3_px },
            .background = { .color = rgba(255, 255, 255, 0.06), .transition = 120_ms },
            .border     = { .color = rgba(255, 255, 255, 0.10), .radius = 5_px, .width = 1_px, .transition = 120_ms },
            .cursor     = Cursor::Hand
        };

        // The selected step's highlight.
        static inline Style StepActive = {
            .background = { .color = rgba(79, 99, 255, 0.45) },
            .border     = { .color = rgba(79, 99, 255, 1.0), .radius = 5_px, .width = 1_px }
        };

        // Elements + state
        //--------------------------------------------------

        Box* body  = nullptr;
            Box* xyPad = nullptr;
                Button* yPlus  = nullptr; Button* yMinus = nullptr;
                Button* xPlus  = nullptr; Button* xMinus = nullptr;
            Box* zCol  = nullptr;
                Button* zPlus  = nullptr; Button* zMinus = nullptr;
            Box* aCol  = nullptr;
                Button* aPlus  = nullptr; Button* aMinus = nullptr;

        Box* stepRow = nullptr;
            Button* step01 = nullptr; Button* step1 = nullptr; Button* step10 = nullptr;

        float stepMm = 1.0f;   // selected "step": the jog quantization + speed dial

        Machine::MachineBase& machine;

        // The step dial sets both quantization and feed; Control drops each by an
        // order of magnitude (finer + slower).
        float jogQuantum(bool ctrl) const { return ctrl ? stepMm * 0.1f : stepMm; }
        int   jogSpeed  (bool ctrl) const { return static_cast<int>(stepMm * 600.0f * (ctrl ? 0.1f : 1.0f)); }

        // Create
        //--------------------------------------------------

        JogSection(Element* parent, Machine::MachineBase& machine)
            : Box(parent, { &Section }, "JogSection"), machine(machine) {

            new Text(this, "Jog", { &Heading });

            body = new Box(this, { &Body }, "JogBody");

                // XY pad: +Y top, -X / +X middle, -Y bottom (cross shape).
                xyPad = new Box(body, { &Col }, "XYPad");

                    Box* row0 = new Box(xyPad, { &PadRow }, "Row0");
                        spacer(row0);  yPlus  = key(row0, "+Y");  spacer(row0);

                    Box* row1 = new Box(xyPad, { &PadRow }, "Row1");
                        xMinus = key(row1, "-X");  spacer(row1);  xPlus = key(row1, "+X");

                    Box* row2 = new Box(xyPad, { &PadRow }, "Row2");
                        spacer(row2);  yMinus = key(row2, "-Y");  spacer(row2);

                // Z column.
                zCol = new Box(body, { &ColGap }, "ZCol");
                    zPlus  = key(zCol, "+Z");
                    zMinus = key(zCol, "-Z");

                // A (rotary) column.
                aCol = new Box(body, { &ColGap }, "ACol");
                    aPlus  = key(aCol, "A+");
                    aMinus = key(aCol, "A-");

            // Step size.
            stepRow = new Box(this, { &StepRow }, "StepRow");
                new Text(stepRow, "step", { &StepLabel });
                step01 = stepKey(stepRow, "0.1");
                step1  = stepKey(stepRow, "1");
                step10 = stepKey(stepRow, "10");

            // Hold a key to jog: press starts a continuous jog along that axis;
            // release (or dragging off the key) stops it.
            holdToJog(yPlus,  0, +1,  0,  0);  holdToJog(yMinus, 0, -1,  0,  0);
            holdToJog(xPlus, +1,  0,  0,  0);  holdToJog(xMinus, -1, 0,  0,  0);
            holdToJog(zPlus,  0,  0, +1,  0);  holdToJog(zMinus, 0,  0, -1,  0);
            holdToJog(aPlus,  0,  0,  0, +1);  holdToJog(aMinus, 0,  0,  0, -1);

            // Step selection (pure GUI state).
            step01->onClick([this](Event&) { setStep(0.1f, step01); });
            step1->onClick ([this](Event&) { setStep(1.0f, step1);  });
            step10->onClick([this](Event&) { setStep(10.0f, step10); });

            setStep(1.0f, step1);   // default
        }

        // Destroy
        //--------------------------------------------------

        // Never leave the machine jogging if the section dies mid-hold.
        ~JogSection() { machine.pauseJog(); }

        // Builders + behaviour
        //--------------------------------------------------

        Button* key(Element* parent, const std::string& label) {
            return new Button(parent,
                { .label = label, .labelStyles = { &ConnectSection::BtnLabel } },
                { &JogBtn, &ConnectSection::BtnHover, &ConnectSection::BtnPress });
        }

        Button* stepKey(Element* parent, const std::string& label) {
            return new Button(parent,
                { .label = label, .labelStyles = { &ConnectSection::BtnLabel } },
                { &StepBtn, &ConnectSection::BtnHover, &ConnectSection::BtnPress });
        }

        void spacer(Element* parent) {
            new Box(parent, { &JogSpacer }, "Spacer");
        }

        // Press-and-hold a key to jog its axis; release / drag-off pauses. (Mouse
        // jog uses the plain step/speed -- no modifiers.)
        void holdToJog(Button* key, float dx, float dy, float dz, float da) {
            key->onMouseDown ([this, dx, dy, dz, da](Event&) { machine.holdJog(dx, dy, dz, da, jogQuantum(false), jogSpeed(false)); });
            key->onMouseUp   ([this](Event&) { machine.pauseJog(); });
            key->onMouseLeave([this](Event&) { machine.pauseJog(); });
        }

        // Keyboard jog
        //--------------------------------------------------
        // Arrows jog X/Y. Alt ("alternate") remaps Up/Down -> Z and Left/Right -> A.
        // Shift makes it a continuous hold (moves while held); without Shift each
        // press is a single step. Control gives a finer + slower jog. We only act
        // while focused, so click the jog pad to "arm" the arrows. The Event's
        // keyboard already tracks every key's held state, so we read it directly --
        // no parallel bookkeeping.

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

        // Select the step and highlight its key.
        void setStep(float mm, Button* active) {
            stepMm = mm;
            step01->styles.remove(&StepActive);
            step1->styles.remove(&StepActive);
            step10->styles.remove(&StepActive);
            active->styles.add(&StepActive);
        }
    };
}
