module;

#include <string>

export module Gui.Machine.Controls;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;

import Machine.Base;
import Gui.Machine.Connect;   // reuse its button styles

export namespace Gui {

    using namespace Rev::Element;
    using namespace Rev::Appearance;

    // Pin Text to the element (there is also a Rev::Primitive::Text in scope).
    using Text = Rev::Element::Text;

    // TEMPORARY test controls: cycle the loaded tool to watch the tool-length
    // offset (and WPos) move. Reuses the Connect section's button styles.
    struct ControlsSection : public Box {

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

        static inline Style Row = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False, CrossAlign::True },
            .size   = { .width = 100_pct }
        };

        // Elements + state
        //--------------------------------------------------

        Box*    toolRow  = nullptr;
            Button* toolDown = nullptr;
            Button* toolUp   = nullptr;

        Machine::MachineBase& machine;
        int           tool = 0;   // last commanded tool; cycles 1..6

        // Create
        //--------------------------------------------------

        ControlsSection(Element* parent, Machine::MachineBase& machine)
            : Box(parent, { &Section }, "ControlsSection"), machine(machine) {

            new Text(this, "Controls (test)", { &Heading });

            toolRow = new Box(this, { &Row }, "ToolRow");
                toolDown = new Button(toolRow, { .label = "Tool -", .labelStyles = { &ConnectSection::BtnLabel, &ConnectSection::BtnLabelDisabled } }, { &ConnectSection::Btn, &ConnectSection::BtnHover, &ConnectSection::BtnPress, &ConnectSection::BtnDisabled });
                toolUp   = new Button(toolRow, { .label = "Tool +", .labelStyles = { &ConnectSection::BtnLabel, &ConnectSection::BtnLabelDisabled } }, { &ConnectSection::Btn, &ConnectSection::BtnHover, &ConnectSection::BtnPress, &ConnectSection::BtnDisabled });

            // Cycle the target tool 1..6 and command the change.
            toolUp->onClick  ([this](Event&) { tool = (tool % 6) + 1;       this->machine.changeTool(tool); });
            toolDown->onClick([this](Event&) { tool = ((tool + 4) % 6) + 1; this->machine.changeTool(tool); });
        }

        // Destroy
        //--------------------------------------------------

        ~ControlsSection() {}
    };
}
