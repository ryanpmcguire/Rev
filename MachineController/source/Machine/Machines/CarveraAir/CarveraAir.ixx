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
        }

        ~CarveraAir() {

            // Adapter first -- joins its worker before the base dispatchers go.
            delete carveraAdapter;
        }

        // Actions
        //--------------------------------------------------

        void unlock()          override { if (adapter) { adapter->sendCommand(Carvera::Unlock{}); } }
        void reset()           override { if (adapter) { adapter->sendCommand(Carvera::Reset{}); } }
        void changeTool(int n) override { if (adapter) { adapter->sendCommand(Carvera::ChangeTool{ n }); } }

        // Queries
        //--------------------------------------------------

        // Fire every query
        void queryAll() {

            queryVersion();
            queryStatus();
            queryOffsets();
            queryState();
            querySwitches();
        }

        void queryStatus()     override { if (adapter) { adapter->sendCommand(Carvera::QueryStatus{}); } }
        void queryOffsets()    override { if (adapter) { adapter->sendCommand(Carvera::QueryOffsets{}); } }
        void queryState()      override { if (adapter) { adapter->sendCommand(Carvera::QueryState{}); } }
        void querySwitches()   override { if (adapter) { adapter->sendCommand(Carvera::QuerySwitches{}); } }
        void queryVersion()    override { if (adapter) { adapter->sendCommand(Carvera::QueryVersion{}); } }
    };
}
