module;

export module Gui.Machine;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;

import App.Machine;
import Gui.Machine.Connect;
import Gui.Machine.Controls;
import Gui.Machine.Telemetry;

export namespace Gui {

    using namespace Rev;
    using namespace Rev::Element;
    using namespace Rev::Appearance;

    // The on-screen reflection of the Machine system. It composes the machine's
    // sections (connection, and later jog / origin / tool) as a column. It owns
    // no machine logic -- it mirrors the system tree (source/Machine/...), one
    // reflection per concern.
    struct MachinePanel : public Box {

        static inline Style Panel = {
            .layout  = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size    = { .width = 320_px },
            .padding = { 16_px, 16_px, 16_px, 16_px }
        };

        ConnectSection*   connect   = nullptr;
        ControlsSection*  controls  = nullptr;
        TelemetrySection* telemetry = nullptr;

        // Create
        //--------------------------------------------------

        MachinePanel(Element* parent, App::Machine& machine) : Box(parent, { &Panel }, "MachinePanel") {

            connect   = new ConnectSection(this, machine);
            controls  = new ControlsSection(this, machine);
            telemetry = new TelemetrySection(this, machine);
        }

        // Destroy
        //--------------------------------------------------

        ~MachinePanel() {}
    };
}
