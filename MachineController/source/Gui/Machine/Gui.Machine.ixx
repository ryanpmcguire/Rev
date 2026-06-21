module;

export module Gui.Machine;

import Rev.Element;
import Rev.Appearance;
import Rev.Element.Box;

import Machine.Base;
import Gui.Machine.Connect;
import Gui.Machine.Controls;
import Gui.Machine.Jog;
import Gui.Machine.Telemetry;
import Gui.Machine.Info;

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
            .layout   = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size     = { .width = 320_px, .height = 100_pct },
            .padding  = { 16_px, 16_px, 16_px, 16_px },
            .overflow = Overflow::Hide,        // the column outgrows the window;
            .scroll   = Scroll::Vertical       // let the operator scroll it
        };

        ConnectSection*   connect   = nullptr;
        ControlsSection*  controls  = nullptr;
        JogSection*       jog       = nullptr;
        TelemetrySection* telemetry = nullptr;
        InfoSection*      info      = nullptr;

        // Create
        //--------------------------------------------------

        MachinePanel(Element* parent, Machine::MachineBase& machine) : Box(parent, { &Panel }, "MachinePanel") {

            connect   = new ConnectSection(this, machine);
            controls  = new ControlsSection(this, machine);
            jog       = new JogSection(this);   // structure only -- no machine wiring yet
            telemetry = new TelemetrySection(this, machine);
            info      = new InfoSection(this, machine);
        }

        // Destroy
        //--------------------------------------------------

        ~MachinePanel() {}
    };
}
