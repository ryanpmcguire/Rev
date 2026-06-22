module;

export module Machine.Machines.CarveraAir;

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

        // Jog -- build/update a streaming jog operation; the queue does the rest
        //--------------------------------------------------

        // One step now: a non-held jog emits a single segment, then finishes.
        void stepJog(float dx, float dy, float dz, float da, float quantum, int speed) override {
            Carvera::Operation::Jog& jog = jogDirective(dx, dy, dz, da, quantum, speed);
            jog.holding = false;
            if (adapter) { jog.emitSegment(*adapter); }
        }

        // Keep moving until pauseJog().
        void holdJog(float dx, float dy, float dz, float da, float quantum, int speed) override {
            jogDirective(dx, dy, dz, da, quantum, speed).holding = true;
        }

        // Release: the operation stops refilling and finishes once it has drained.
        void pauseJog() override {
            if (Carvera::Operation::Jog* jog = currentJog()) { jog->holding = false; }
        }

        // The running operation, if it is a jog.
        Carvera::Operation::Jog* currentJog() {
            Operation::OperationBase* op = operations.current();
            if (op && op->type == Operation::OperationBase::Type::Jog) {
                return static_cast<Carvera::Operation::Jog*>(op);
            }
            return nullptr;
        }

        // Reuse the running jog (or open one anchored at the live position), and set
        // its directive.
        Carvera::Operation::Jog& jogDirective(float dx, float dy, float dz, float da, float quantum, int speed) {

            Carvera::Operation::Jog* jog = currentJog();

            if (!jog) {
                jog = new Carvera::Operation::Jog();
                const Coord& p = telemetry.spindle.pos;   // anchor at the known machine position
                jog->frontierX = p.x; jog->frontierY = p.y; jog->frontierZ = p.z; jog->frontierA = p.a;
                operations.enqueue(jog);
            }

            jog->dirX = dx; jog->dirY = dy; jog->dirZ = dz; jog->dirA = da;
            jog->segmentMm = quantum;
            jog->speed     = speed;
            return *jog;
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
