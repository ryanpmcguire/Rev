module;

export module Machine.Machines.CarveraAir;

import Machine.Base;
import Machine.Machines.Carvera.Adapter;
import Machine.Machines.Carvera.Commands;

export namespace Machine {

    // A concrete machine: owns the Carvera adapter and creates the Carvera
    // commands it emits.
    struct CarveraAir : public MachineBase {

        // Adapter
        Carvera::Adapter* carveraAdapter = nullptr;

        // Construct/destruct
        //--------------------------------------------------

        CarveraAir() {

            carveraAdapter = new Carvera::Adapter();
            bindAdapter(carveraAdapter);

            // Known properties the controller never reports over the wire.
            info.frames.spindleOffset = 29.00f;    // calibration-tool datum below the gauge line
            info.identity.model       = "Carvera Air";
        }

        ~CarveraAir() {

            // Adapter first -- joins its worker before the base dispatchers go.
            delete carveraAdapter;
        }

        // Actions
        //--------------------------------------------------

        void unlock()          override { if (adapter) { adapter->sendCommand(Carvera::Unlock{}); } }
        void reset()           override { if (adapter) { adapter->sendCommand(Carvera::Reset{}); } }
        void home()            override { if (adapter) { adapter->sendCommand(Carvera::Home{}); } }
        void changeTool(int n) override { if (adapter) { adapter->sendCommand(Carvera::ChangeTool{ n }); } }

        void jog(float x, float y, float z, float a, int feed) override {
            if (adapter) { adapter->sendCommand(Carvera::Jog{ x, y, z, a, feed }); }
        }

        // Queries (queryAll() lives on MachineBase and calls these)
        //--------------------------------------------------

        void queryStatus()     override { if (adapter) { adapter->sendCommand(Carvera::QueryStatus{}); } }
        void queryOffsets()    override { if (adapter) { adapter->sendCommand(Carvera::QueryOffsets{}); } }
        void queryState()      override { if (adapter) { adapter->sendCommand(Carvera::QueryState{}); } }
        void querySwitches()   override { if (adapter) { adapter->sendCommand(Carvera::QuerySwitches{}); } }
        void queryVersion()    override { if (adapter) { adapter->sendCommand(Carvera::QueryVersion{}); } }
    };
}
