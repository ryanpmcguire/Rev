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

        // Queries (queryAll() lives on MachineBase and calls these)
        //--------------------------------------------------

        void queryStatus()     override { if (adapter) { adapter->sendCommand(Carvera::Command::QueryStatus{}); } }
        void queryOffsets()    override { if (adapter) { adapter->sendCommand(Carvera::Command::QueryOffsets{}); } }
        void queryState()      override { if (adapter) { adapter->sendCommand(Carvera::Command::QueryState{}); } }
        void querySwitches()   override { if (adapter) { adapter->sendCommand(Carvera::Command::QuerySwitches{}); } }
        void queryVersion()    override { if (adapter) { adapter->sendCommand(Carvera::Command::QueryVersion{}); } }

        // Actions
        //--------------------------------------------------

        void unlock()          override { if (adapter) { adapter->sendCommand(Carvera::Command::Unlock{}); } requestImpliedResync(); }
        void reset()           override { if (adapter) { adapter->sendCommand(Carvera::Command::Reset{}); }  requestImpliedResync(); }
        void home()            override { if (adapter) { adapter->sendCommand(Carvera::Command::Home{}); } }
        void changeTool(int n) override { if (adapter) { adapter->sendCommand(Carvera::Command::ChangeTool{ n }); } }

        // Jog -- thin: influence the running jog, or open one if nothing is running
        //--------------------------------------------------

        void jog(Coord direction, float stepMm, float stepDeg, int feed, bool hold) override {

            // A non-jog operation owns the machine -- don't interrupt it.
            Operation::OperationBase* op = operations.current;
            if (op && op->type != Operation::Type::Jog) { return; }

            // No jog running -- open one (skip a zero-motion command, nothing to stop).
            if (!op) {
                if (!direction.x && !direction.y && !direction.z && !direction.a) { return; }
                op = new Carvera::Operation::Jog();
                operations.enqueue(op);
            }

            static_cast<Carvera::Operation::Jog*>(op)->setDir(direction, stepMm, stepDeg, feed, hold);
        }
    };
}
