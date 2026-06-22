module;

#include <string>
#include <format>

export module Gui.Machine.Jog;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;

import Rev.Core.Process;

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

        // The step sets the feed; Ctrl drops it an order of magnitude (finer + slower).
        int jogSpeed(bool ctrl) const { return static_cast<int>(stepMm * 600.0f * (ctrl ? 0.1f : 1.0f)); }

        // Auto-cancel distance: ~this much motion time, scaled to the feed.
        static constexpr float AutoCancelMs = 150.0f;
        float autoCancel(int speed) const { return speed / 60.0f * (AutoCancelMs / 1000.0f); }

        // Held inputs. Mouse holds one key's direction; the keyboard tracks the arrows.
        float mDirX = 0, mDirY = 0, mDirZ = 0, mDirA = 0;
        bool  kLeft = false, kRight = false, kUp = false, kDown = false, kAlt = false, kCtrl = false;

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

            // Press-and-hold a key to set the mouse jog direction.
            mouseHold(xPlus, +1,  0,  0,  0);  mouseHold(xMinus, -1,  0,  0,  0);
            mouseHold(yPlus,  0, +1,  0,  0);  mouseHold(yMinus,  0, -1,  0,  0);
            mouseHold(zPlus,  0,  0, +1,  0);  mouseHold(zMinus,  0,  0, -1,  0);
            mouseHold(aPlus,  0,  0,  0, +1);  mouseHold(aMinus,  0,  0,  0, -1);

            // Step corners cycle the preset.
            stepDown->onClick([this](Event&) { adjustStep(-1); });
            stepUp  ->onClick([this](Event&) { adjustStep(+1); });

            // Drive the machine from the live input state every frame.
            Rev::Core::Process::instance().schedule(this, 16, [this](uint64_t) { animate(); });
        }

        // Destroy
        //--------------------------------------------------

        ~JogSection() { Rev::Core::Process::instance().unschedule(this); }

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

        // Input
        //--------------------------------------------------

        // Mouse: hold a key to drive that direction; release / leave clears it.
        void mouseHold(Button* key, float dx, float dy, float dz, float da) {
            key->onMouseDown ([this, dx, dy, dz, da](Event&) { mDirX = dx; mDirY = dy; mDirZ = dz; mDirA = da; });
            key->onMouseUp   ([this](Event&) { mDirX = mDirY = mDirZ = mDirA = 0; });
            key->onMouseLeave([this](Event&) { mDirX = mDirY = mDirZ = mDirA = 0; });
        }

        // Keyboard (while focused): arrows, Alt remaps to Z/A, Ctrl is fine + slow.
        void keyDown(Event& e) override { Box::keyDown(e); readKeys(e); }
        void keyUp  (Event& e) override { Box::keyUp(e);   readKeys(e); }
        void loseFocus(Event& e) override { Box::loseFocus(e); kLeft = kRight = kUp = kDown = false; }

        void readKeys(Event& e) {
            kLeft = e.keyboard.arrows.left; kRight = e.keyboard.arrows.right;
            kUp   = e.keyboard.arrows.up;   kDown  = e.keyboard.arrows.down;
            kAlt  = e.keyboard.alt;         kCtrl  = e.keyboard.ctrl;
        }

        // Per-frame: sum the held inputs and influence the jog.
        void animate() {

            if (!machine.connected()) { return; }

            float dx = mDirX, dy = mDirY, dz = mDirZ, da = mDirA;
            if (!kAlt) { dx += axis(kRight, kLeft); dy += axis(kUp, kDown); }
            else       { dz += axis(kUp, kDown);    da += axis(kRight, kLeft); }

            dx = clamp(dx); dy = clamp(dy); dz = clamp(dz); da = clamp(da);
            if (dx == 0 && dy == 0 && dz == 0 && da == 0) { return; }

            const int speed = jogSpeed(kCtrl);
            machine.jog(dx, dy, dz, da, autoCancel(speed), speed);
        }

        static float axis(bool pos, bool neg) { return (pos ? 1.0f : 0.0f) - (neg ? 1.0f : 0.0f); }
        static float clamp(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
    };
}
