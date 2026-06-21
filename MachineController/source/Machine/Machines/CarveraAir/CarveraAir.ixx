module;

export module Machine.Machines.CarveraAir;

import Rev.Core.Process;

import Machine.Base;
import Machine.Operation;
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

        void jog(float x, float y, float z, float a, int feed) override {
            if (adapter) { adapter->sendCommand(Carvera::Command::Jog{ x, y, z, a, feed }); }
        }

        // Continuous jog (goto under the hood)
        //--------------------------------------------------

        static constexpr float JogLookaheadTicks = 3.0f;    // queue ~this many motion ticks ahead (anti-starvation)
        static constexpr float JogMaxQueuedMm    = 10.0f;   // ...but never more queued than this, any speed

        // Move one step now.
        void stepJog(float dx, float dy, float dz, float da, float quantum, int speed) override {
            Operation::Jog& jog = beginJog(dx, dy, dz, da, quantum, speed);
            jog.holding = false;
            emitSegment(jog);   // one segment; serviceMotion drops the jog next tick
        }

        // Keep moving until pauseJog().
        void holdJog(float dx, float dy, float dz, float da, float quantum, int speed) override {
            Operation::Jog& jog = beginJog(dx, dy, dz, da, quantum, speed);
            jog.holding = true;   // serviceMotion refills while held
        }

        void pauseJog() override {
            if (Operation::Jog* jog = currentJog()) { jog->holding = false; }
        }

        // Run every motion tick: consume the motion expected this tick, then refill
        // just enough lookahead that the controller never starves. Stopping drops the
        // rest of the jog outright -- the queued intent is no longer wanted, so a key
        // release coasts only the small lookahead, not the whole buffer.
        void serviceMotion() override {

            Operation::Jog* jog = currentJog();
            if (!jog || !adapter) { return; }

            if (!jog->holding) { operations.clear(); return; }

            const float perTick = jog->speed / 60.0f * (MotionTickMs / 1000.0f);   // mm covered this tick
            jog->queuedMm -= perTick;                                               // consume one tick of motion

            float target = perTick * JogLookaheadTicks;
            if (target > JogMaxQueuedMm) { target = JogMaxQueuedMm; }   // hard ceiling

            while (jog->queuedMm < target) { emitSegment(*jog); }
        }

        // The current operation, if it is a jog directive.
        Operation::Jog* currentJog() {
            Operation::OperationBase* op = operations.current;
            if (op && op->type == Operation::OperationBase::Type::Jog) {
                return static_cast<Operation::Jog*>(op);
            }
            return nullptr;
        }

        // Get the running jog (or open a fresh one anchored at the machine position),
        // and update its directive.
        Operation::Jog& beginJog(float dx, float dy, float dz, float da, float quantum, int speed) {

            Operation::Jog* jog = currentJog();
            
            if (!jog) {
                operations.clear();
                jog = new Operation::Jog();
                const Coord& p = telemetry.spindle.pos;   // anchor at the known machine position
                jog->frontierX = p.x; jog->frontierY = p.y; jog->frontierZ = p.z; jog->frontierA = p.a;
                operations.enqueue(jog);
            }

            jog->dirX = dx; jog->dirY = dy; jog->dirZ = dz; jog->dirA = da;
            jog->segmentMm = quantum;
            jog->speed     = speed;
            return *jog;
        }

        // Advance the frontier by one segment and send the absolute goto for it.
        void emitSegment(Operation::Jog& jog) {

            jog.frontierX += jog.dirX * jog.segmentMm;
            jog.frontierY += jog.dirY * jog.segmentMm;
            jog.frontierZ += jog.dirZ * jog.segmentMm;
            jog.frontierA += jog.dirA * jog.segmentMm;

            Carvera::Command::GoTo move(jog.speed);
            if (jog.dirX != 0.0f) { move.x = { true, jog.frontierX }; }
            if (jog.dirY != 0.0f) { move.y = { true, jog.frontierY }; }
            if (jog.dirZ != 0.0f) { move.z = { true, jog.frontierZ }; }
            if (jog.dirA != 0.0f) { move.a = { true, jog.frontierA }; }
            adapter->sendCommand(move);   // stack temp -- sent now, nothing retained

            jog.queuedMm += jog.segmentMm;
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
