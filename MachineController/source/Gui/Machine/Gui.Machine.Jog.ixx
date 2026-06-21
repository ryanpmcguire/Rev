module;

#include <string>

export module Gui.Machine.Jog;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;

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

        float stepMm = 1.0f;   // selected step (pure GUI state for now)

        // Create
        //--------------------------------------------------

        JogSection(Element* parent) : Box(parent, { &Section }, "JogSection") {

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

            // (Jog keys carry no handlers yet -- machine wiring stripped for a
            //  fresh start. They are structural placeholders.)

            // Step selection (pure GUI state).
            step01->onClick([this](Event&) { setStep(0.1f, step01); });
            step1->onClick ([this](Event&) { setStep(1.0f, step1);  });
            step10->onClick([this](Event&) { setStep(10.0f, step10); });

            setStep(1.0f, step1);   // default
        }

        // Destroy
        //--------------------------------------------------

        ~JogSection() {}

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
