module;

export module Machine.Machines.CarveraAir;

import Rev.GlobalTime;

import Machine.Base;
import Machine.Operation;
import Machine.Machines.Carvera.Adapter;
import Machine.Machines.Carvera.Commands;
import Machine.Machines.Carvera.Operations;

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

            // Stop the service ticks before tearing down.
            stopTicking();

            // Adapter first -- joins its worker before the base dispatchers go.
            delete carveraAdapter;
        }

        // Actions
        //--------------------------------------------------

        void unlock()          override { if (adapter) { adapter->sendCommand(Carvera::Command::Unlock{}); } }
        void reset()           override { if (adapter) { adapter->sendCommand(Carvera::Command::Reset{}); } }
        void home()            override { if (adapter) { adapter->sendCommand(Carvera::Command::Home{}); } }
        void changeTool(int n) override { if (adapter) { adapter->sendCommand(Carvera::Command::ChangeTool{ n }); } }

        // Jog -- thin: influence the running jog, or open one if nothing is running
        //--------------------------------------------------

        void jog(float dx, float dy, float dz, float da, float autoCancelMm, int speed) override {

            
        }

        // Queries (queryAll() lives on MachineBase and calls these)
        //--------------------------------------------------

        void queryStatus()     override { if (adapter) { adapter->sendCommand(Carvera::Command::QueryStatus{}); } }
        void queryOffsets()    override { if (adapter) { adapter->sendCommand(Carvera::Command::QueryOffsets{}); } }
        void queryState()      override { if (adapter) { adapter->sendCommand(Carvera::Command::QueryState{}); } }
        void querySwitches()   override { if (adapter) { adapter->sendCommand(Carvera::Command::QuerySwitches{}); } }
        void queryVersion()    override { if (adapter) { adapter->sendCommand(Carvera::Command::QueryVersion{}); } }
    };
}
