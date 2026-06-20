module;

export module Gui.Interface;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;

import App.Machine;
import Gui.Machine;

export namespace Gui {

    using namespace Rev;
    using namespace Rev::Element;
    using namespace Rev::Appearance;

    // The content root: a full-window Box that owns the dark surface every other
    // GUI element sits on. The window hosts exactly one of these; the per-system
    // reflection widgets (Gui/Machine/...) compose in as its children.
    struct Interface : public Box {

        MachinePanel* machinePanel = nullptr;

        // Create
        //--------------------------------------------------

        Interface(Element* parent, App::Machine& machine) : Box(parent, {}, "Interface") {

            this->style->layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
            this->style->size = { .width = 100_pct, .height = Grow() };

            // Dark theme surface.
            this->style->background.color = rgba(24, 24, 28, 1.0);

            machinePanel = new MachinePanel(this, machine);
        }

        // Destroy
        //--------------------------------------------------

        ~Interface() {}
    };
}
