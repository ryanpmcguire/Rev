module;

#include <functional>
#include <string>
#include <vector>

export module Machine.Base;

import Rev.Core.Dispatcher;
import Rev.Core.Process;

import Machine.Events;
import Machine.Command;
import Machine.Operation;
import Machine.Adapter;

export namespace Machine {

    // A no-payload channel ping ("something here changed, go read it"). Carries
    // nothing; it is the payload type of the SignalChannel notify channels below.
    struct Signal {};

    // A no-payload notification channel: "something here changed, go read it".
    // Subscribers attach AS an owner so they can drop their subscription on death.
    // (One dispatcher per channel, so a single fixed key groups all its listeners.)
    struct SignalChannel {

        using Key = Rev::Core::Dispatcher<Signal>::ListenerKey;
        Rev::Core::Dispatcher<Signal> dispatcher;

        void on(void* owner, std::function<void()> f) {
            dispatcher.listen(Key{}, owner, [f = std::move(f)](Signal&) { f(); });
        }
        void notify() { Signal s; dispatcher.tell(Key{}, s); }
        void unsubscribe(void* owner) { dispatcher.unsubscribe(owner); }
    };

    // The base machine: owns an adapter, binds it, and routes decoded events into
    // its sub-structs -- each a self-contained observable that caches its own data
    // and owns the channels announcing its changes. (MachineBase, not Machine, to
    // not clash with the namespace.)
    struct MachineBase {

        // Definitions
        //--------------------------------------------------

        // A coordinate / offset (linear X/Y/Z + rotary A,B)
        struct Coord { float x = 0, y = 0, z = 0, a = 0, b = 0; };

        // Live telemetry, grouped by subsystem. Folds a status frame in via
        // apply(), then announces on its own channels.
        struct Telemetry {

            // Spindle (MPos + drive)
            struct Spindle {
                Coord pos;
                float rpm    = 0;
                float target = 0;
                float scale  = 100;
                float load   = 0;
                float temp   = 0;
            };

            // Tool (WPos + feed)
            struct Tool {
                Coord pos;
                float speed       = 0;
                float speedTarget = 0;
                float scale       = 100;
                int   number      = 0;
                float offset      = 0;
            };

            struct Laser { float power = 0; float scale = 100; };
            struct Probe { float voltage = 0; };

            Spindle spindle;
            Tool    tool;
            Probe   probe;
            Laser   laser;

            // Channels
            SignalChannel updateChannel;       // any telemetry change
            SignalChannel machinePosChannel;   // spindle (MPos) moved
            SignalChannel workPosChannel;      // tool (WPos) moved

            void onUpdate    (void* owner, std::function<void()> f) { updateChannel.on(owner, std::move(f)); }
            void onMachinePos(void* owner, std::function<void()> f) { machinePosChannel.on(owner, std::move(f)); }
            void onWorkPos   (void* owner, std::function<void()> f) { workPosChannel.on(owner, std::move(f)); }
            void unsubscribe (void* owner) { updateChannel.unsubscribe(owner); machinePosChannel.unsubscribe(owner); workPosChannel.unsubscribe(owner); }

            // Fold a decoded status frame in, then announce.
            void apply(const Event::Telemetry& e) {

                spindle.pos    = { e.mx, e.my, e.mz, e.ma, e.mb };
                spindle.rpm    = e.spindleRpm;
                spindle.target = e.spindleTarget;
                spindle.scale  = e.spindleScale;
                spindle.load   = e.spindleLoad;
                spindle.temp   = e.spindleTemp;

                tool.pos         = { e.wx, e.wy, e.wz, e.wa, e.wb };
                tool.speed       = e.feed;
                tool.speedTarget = e.feedTarget;
                tool.scale       = e.feedScale;
                tool.number      = e.tool;
                tool.offset      = e.toolOffset;

                laser.power = e.laserPower;
                laser.scale = e.laserScale;

                machinePosChannel.notify();
                workPosChannel.notify();
                updateChannel.notify();
            }

            Telemetry() {}
        };

        // Request/response facts -- fetched on demand, cached. (Open-ended queries
        // like config-get / file lists are answered ad hoc, not modelled here.)
        struct Info {

            // Identity (version / model / network name)
            struct Identity {
                std::string firmware;
                std::string model;
                std::string name;
            };

            // Network -- connection target + live connection status. Owns both the
            // machine's notion of connection status (its own type, distinct from
            // the adapter's Event::Connection::Status) and the connection channels
            // (the GUI's connect section listens here).
            struct Network {

                // The MACHINE's connection status -- adapted from the adapter's at
                // the boundary (see apply()).
                enum class ConnectionStatus {
                    Disconnected,
                    Connecting,
                    Connected,
                    Error
                };

                std::string      ip;
                std::string      mac;
                int              port   = 0;
                ConnectionStatus status = ConnectionStatus::Disconnected;

                SignalChannel updateChannel;       // status changed (any)
                SignalChannel connectChannel;      // came up
                SignalChannel disconnectChannel;   // went down / errored

                void onUpdate    (void* owner, std::function<void()> f) { updateChannel.on(owner, std::move(f)); }
                void onConnect   (void* owner, std::function<void()> f) { connectChannel.on(owner, std::move(f)); }
                void onDisconnect(void* owner, std::function<void()> f) { disconnectChannel.on(owner, std::move(f)); }
                void unsubscribe (void* owner) { updateChannel.unsubscribe(owner); connectChannel.unsubscribe(owner); disconnectChannel.unsubscribe(owner); }

                bool isConnected() const { return status == ConnectionStatus::Connected; }

                const char* statusName() const {
                    switch (status) {
                        case ConnectionStatus::Disconnected: return "Disconnected";
                        case ConnectionStatus::Connecting:   return "Connecting";
                        case ConnectionStatus::Connected:    return "Connected";
                        case ConnectionStatus::Error:        return "Error";
                    }
                    return "?";
                }

                // Adapt the adapter's transport status into the machine's own.
                static ConnectionStatus adapt(Event::Connection::Status s) {
                    switch (s) {
                        case Event::Connection::Status::Disconnected: return ConnectionStatus::Disconnected;
                        case Event::Connection::Status::Connecting:   return ConnectionStatus::Connecting;
                        case Event::Connection::Status::Connected:    return ConnectionStatus::Connected;
                        case Event::Connection::Status::Error:        return ConnectionStatus::Error;
                    }
                    return ConnectionStatus::Disconnected;
                }

                void apply(const Event::Connection& e) {
                    status = adapt(e.status);
                    updateChannel.notify();
                    if (status == ConnectionStatus::Connected) {
                        connectChannel.notify();
                    } else if (status == ConnectionStatus::Disconnected || status == ConnectionStatus::Error) {
                        disconnectChannel.notify();
                    }
                }
            };

            // Coordinate systems & stored offsets (from "$#")
            struct Frames {
                Coord g54, g55, g56, g57, g58, g59;
                Coord g28, g30;
                Coord g92;
                float toolLengthOffset = 0;
                float spindleOffset    = 0;
            };

            // Last probe (from "[PRB:...]")
            struct Probe {
                Coord position;
                bool  triggered = false;
            };

            // One tool slot
            struct ToolSlot {
                int   number  = 0;
                float offset  = 0;
                bool  present = false;
            };

            Identity              identity;
            Network               network;
            Frames                frames;
            Probe                 probe;
            std::vector<ToolSlot> tools;

            // Channel (the facts are read from this struct on update)
            SignalChannel updateChannel;
            void onUpdate(void* owner, std::function<void()> f) { updateChannel.on(owner, std::move(f)); }
            void unsubscribe(void* owner) { updateChannel.unsubscribe(owner); }

            // Fold one decoded "$#" fact in, then announce.
            void apply(const Event::Info& e) {

                Coord c{ e.x, e.y, e.z, e.a, e.b };

                switch (e.field) {
                    case Event::Info::Field::FrameG54:         frames.g54 = c; break;
                    case Event::Info::Field::FrameG55:         frames.g55 = c; break;
                    case Event::Info::Field::FrameG56:         frames.g56 = c; break;
                    case Event::Info::Field::FrameG57:         frames.g57 = c; break;
                    case Event::Info::Field::FrameG58:         frames.g58 = c; break;
                    case Event::Info::Field::FrameG59:         frames.g59 = c; break;
                    case Event::Info::Field::FrameG28:         frames.g28 = c; break;
                    case Event::Info::Field::FrameG30:         frames.g30 = c; break;
                    case Event::Info::Field::FrameG92:         frames.g92 = c; break;
                    case Event::Info::Field::ToolLengthOffset: frames.toolLengthOffset = e.tlo; break;
                    case Event::Info::Field::Probe:            probe.position = c; probe.triggered = e.probeTriggered; break;
                }

                updateChannel.notify();
            }

            Info() {}
        };

        // The machine's work queue (FIFO; front runs). Owns the adapter and its tick.
        struct Operations {

            std::vector<Operation::OperationBase*> queue;   // pending + running (owned, FIFO)

            Adapter* adapter = nullptr;
            static constexpr uint64_t TickMs = 5;   // ~200 Hz

            SignalChannel updateChannel;   // queue / current / progress changed
            void onUpdate(void* owner, std::function<void()> f) { updateChannel.on(owner, std::move(f)); }
            void unsubscribe(void* owner) { updateChannel.unsubscribe(owner); }

            ~Operations() { stop(); for (auto* op : queue) { delete op; } }

            // The running operation (front of the queue), or null.
            Operation::OperationBase* current() const { return queue.empty() ? nullptr : queue.front(); }

            // Queue a new operation; it runs once it reaches the front.
            void enqueue(Operation::OperationBase* op) {
                queue.push_back(op);
                updateChannel.notify();
            }

            // Run the queue on our own tick.
            void start() { Rev::Core::Process::instance().schedule(this, TickMs, [this](uint64_t now) { tick(now); }); }
            void stop()  { Rev::Core::Process::instance().unschedule(this); }

            // Tick the current operation, then retire it once finished.
            void tick(uint64_t now) {

                if (!adapter) { return; }

                Operation::OperationBase* op = current();
                if (!op) { return; }

                op->tick(*adapter, now);

                if (op->finished()) { advance(); }
            }

            // Retire the finished front; the next operation becomes current.
            void advance() {
                if (queue.empty()) { return; }
                delete queue.front();
                queue.erase(queue.begin());
                updateChannel.notify();
            }

            // Drop everything (e.g. on disconnect / abort).
            void clear() {
                for (auto* op : queue) { delete op; }
                queue.clear();
                updateChannel.notify();
            }
        };

        // Data
        //--------------------------------------------------

        // The link to the physical machine (null until a concrete machine binds one)
        Adapter*  adapter = nullptr;

        // Cached, self-observable state
        Telemetry  telemetry;
        Info       info;
        Operations operations;

        bool connected() const { return info.network.isConnected(); }

        // Construct / destruct
        //--------------------------------------------------

        // Telemetry + motion tick, alive only while connected (started/stopped by
        // the network's own connect channels).
        MachineBase() {
            info.network.onConnect   (this, [this]() { startTicking(); });
            info.network.onDisconnect(this, [this]() { stopTicking(); operations.clear(); });
        }
        virtual ~MachineBase() { stopTicking(); }

        // ============================================================
        // Adapter & channels
        // ============================================================
        // Telemetry / Info / connection channels live on the sub-structs above;
        // only state + log -- which have no cached home -- remain machine-level.

        virtual void stateKey(Event::State&) {}
        virtual void logKey  (Event::Log&)   {}

        Rev::Core::Dispatcher<Event::State> stateDispatcher;
        Rev::Core::Dispatcher<Event::Log>   logDispatcher;

        void onState(void* owner, const std::function<void(Event::State&)>& f) { stateDispatcher.listen(&MachineBase::stateKey, owner, f); }
        void onLog  (void* owner, const std::function<void(Event::Log&)>&   f) { logDispatcher.listen(&MachineBase::logKey, owner, f); }

        // Adopt an adapter and route its channels into our sub-structs
        void bindAdapter(Adapter* a) {

            adapter = a;
            if (!adapter) { return; }

            operations.adapter = a;   // the queue pumps through this link

            adapter->onConnection([this](Event::Connection& e) { info.network.apply(e); });
            adapter->onTelemetry ([this](Event::Telemetry&  e) { telemetry.apply(e); });
            adapter->onInfo      ([this](Event::Info&       e) { info.apply(e); });
            adapter->onState     ([this](Event::State&      e) { stateDispatcher.tell(&MachineBase::stateKey, e); });
            adapter->onLog       ([this](Event::Log&        e) { logDispatcher.tell(&MachineBase::logKey, e); });
        }

        // Drop all of an owner's subscriptions, wherever they live
        void unsubscribe(void* owner) {
            telemetry.unsubscribe(owner);
            info.unsubscribe(owner);
            info.network.unsubscribe(owner);
            operations.unsubscribe(owner);
            stateDispatcher.unsubscribe(owner);
            logDispatcher.unsubscribe(owner);
        }

        // ============================================================
        // Service tick (telemetry; operations run their own tick)
        // ============================================================

        static constexpr uint64_t TelemetryTickMs = 50;   // ~20 Hz

        void startTicking() {
            Rev::Core::Process::instance().schedule(this, TelemetryTickMs, [this](uint64_t) { queryStatus(); });
            operations.start();
        }
        void stopTicking() {
            Rev::Core::Process::instance().unschedule(this);
            operations.stop();
        }

        // ============================================================
        // Commands
        // ============================================================

        // Status commands
        //--------------------------------------------------

        virtual void connect()    { if (adapter) { adapter->connect(); } }   // lifecycle (adapter)
        virtual void disconnect() { if (adapter) { adapter->disconnect(); } }
        virtual void unlock()     {}                                          // clear alarm
        virtual void reset()      {}                                          // soft reset

        // Query commands
        //--------------------------------------------------

        // Fire every query (each reply is decoded into Info / Telemetry)
        void queryAll() {
            queryVersion();
            queryStatus();
            queryOffsets();
            queryState();
            querySwitches();
        }

        virtual void queryStatus()   {}
        virtual void queryOffsets()  {}
        virtual void queryState()    {}
        virtual void querySwitches() {}
        virtual void queryVersion()  {}

        // Tool and calibration commands
        //--------------------------------------------------

        virtual void home() {}
        virtual void changeTool(int) {}

        // Motion commands
        //--------------------------------------------------

        // Influence the current jog: go in direction (per-axis sign) at `speed`,
        // auto-cancelling after `autoCancelMm` if not renewed. Driven repeatedly by
        // the GUI; the machine never continues a jog on its own.
        virtual void jog(float /*dx*/, float /*dy*/, float /*dz*/, float /*da*/, float /*autoCancelMm*/, int /*speed*/) {}
    };
}
