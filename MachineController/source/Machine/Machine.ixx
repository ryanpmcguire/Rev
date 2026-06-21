module;

#include <functional>
#include <string>
#include <vector>

export module Machine.Base;

import Rev.Core.Dispatcher;
import Rev.Core.Observable;

import Machine.Events;
import Machine.Adapter;

export namespace Machine {

    // The base machine: owns the dispatchers, binds an adapter, re-broadcasts its
    // channels. (MachineBase, not Machine, to not clash with the namespace.)
    struct MachineBase {

        // Definitions
        //--------------------------------------------------

        // The machine's live telemetry, grouped by subsystem.
        struct Telemetry {

            // A position: linear X/Y/Z + rotary A and B.
            struct Pos { float x = 0, y = 0, z = 0, a = 0, b = 0; };

            struct Spindle {
                Pos   pos;            // MPos -- the spindle in the machine frame
                float rpm    = 0;
                float target = 0;     // commanded rpm
                float scale  = 100;   // override (%)
                float load   = 0;     // percent
                float temp   = 0;     // degrees C
            };

            struct Tool {
                Pos   pos;            // WPos -- the tool tip in the work frame
                float speed       = 0;    // current feed (mm/min)
                float speedTarget = 0;    // feed cap (mm/min)
                float scale       = 100;  // feed override (%)
                int   number      = 0;    // loaded tool number (0 = none)
                float offset      = 0;    // tool-length offset
            };

            struct Laser {
                float power = 0;
                float scale = 100;    // override (%)
            };

            struct Probe {
                float voltage = 0;
            };

            Spindle spindle;
            Tool    tool;
            Probe   probe;
            Laser   laser;

            Telemetry() {}
        };

        // The machine's request/response facts -- fetched on demand, cached.
        // (Open-ended queries like config-get / file lists are answered ad hoc,
        // not modelled here.)
        struct Info {

            // A coordinate / offset: linear X/Y/Z + rotary A and B.
            struct Coord { float x = 0, y = 0, z = 0, a = 0, b = 0; };

            // Who the machine is (version, model, network name).
            struct Identity {
                std::string firmware;   // firmware version / build
                std::string model;      // e.g. "Carvera" / "Carvera Air"
                std::string name;       // network name (e.g. "CARVERA_01001")
            };

            // How we reach it.
            struct Network {
                std::string ip;
                std::string mac;
                int         port = 0;
            };

            // Coordinate systems & stored offsets -- from "$#".
            struct Frames {
                Coord g54, g55, g56, g57, g58, g59;   // work coordinate systems
                Coord g28, g30;                        // stored (home / park) positions
                Coord g92;                             // temporary offset
                float toolLengthOffset = 0;            // [TLO]
            };

            // Result of the last probe -- from "[PRB:...]".
            struct Probe {
                Coord position;
                bool  triggered = false;
            };

            // One automatic-tool-change slot.
            struct ToolSlot {
                int   number  = 0;
                float offset  = 0;       // tool-length offset for this slot
                bool  present = false;   // a tool occupies this slot
            };

            Identity              identity;
            Network               network;
            Frames                frames;
            Probe                 probe;
            std::vector<ToolSlot> tools;

            Info() {}
        };

        // Data
        //--------------------------------------------------

        // The link to the physical machine. Null until a concrete machine binds one.
        Adapter* adapter = nullptr;

        // Latest connection status, for a late subscriber.
        Rev::Core::Observable<ConnectionStatus> status;

        // Latest telemetry readings.
        Telemetry telemetry;

        // Latest requested machine info.
        Info info;

        // Outward channels: each is a key (the virtual) + its dispatcher.
        // Consumers attach via on*(); the machine fans out via report().
        virtual void connectionKey(ConnectionEvent&) {}
        virtual void stateKey     (StateEvent&)      {}
        virtual void logKey       (LogEvent&)        {}
        virtual void telemetryKey (Signal&)          {}
        virtual void connectKey   (Signal&)          {}
        virtual void disconnectKey(Signal&)          {}

        Rev::Core::Dispatcher<ConnectionEvent> connectionDispatcher;
        Rev::Core::Dispatcher<StateEvent>      stateDispatcher;
        Rev::Core::Dispatcher<LogEvent>        logDispatcher;
        Rev::Core::Dispatcher<Signal>          telemetryDispatcher;
        Rev::Core::Dispatcher<Signal>          connectDispatcher;
        Rev::Core::Dispatcher<Signal>          disconnectDispatcher;

        // Create / Destroy
        //--------------------------------------------------

        MachineBase() {}
        virtual ~MachineBase() {}

        // Bind / Unbind
        //--------------------------------------------------

        // Bind -- adopt an adapter and forward its channels into ours.
        void bindAdapter(Adapter* a) {

            adapter = a;
            if (!adapter) { return; }

            adapter->onConnection([this](ConnectionEvent& e) { report(e); });
            adapter->onTelemetry ([this](TelemetryEvent&  e) { report(e); });
            adapter->onState     ([this](StateEvent&      e) { report(e); });
            adapter->onLog       ([this](LogEvent&        e) { report(e); });
        }

        // Drop all of an owner's subscriptions.
        void unsubscribe(void* owner) {
            connectionDispatcher.unsubscribe(owner);
            telemetryDispatcher.unsubscribe(owner);
            stateDispatcher.unsubscribe(owner);
            logDispatcher.unsubscribe(owner);
            connectDispatcher.unsubscribe(owner);
            disconnectDispatcher.unsubscribe(owner);
        }

        // Query
        //--------------------------------------------------

        bool connected() const { return status.value == ConnectionStatus::Connected; }

        // Verbs -- forward through the adapter (a machine with none does nothing).
        //--------------------------------------------------

        // Lifecycle -- forwarded to the adapter (not commands).
        virtual void connect()     { if (adapter) { adapter->connect(); } }
        virtual void disconnect()  { if (adapter) { adapter->disconnect(); } }

        // Verb interface -- a concrete machine implements these with its own commands.
        //
        // Actions
        virtual void unlock()        {}
        virtual void reset()         {}
        virtual void changeTool(int) {}

        // Queries
        virtual void queryStatus()   {}
        virtual void queryOffsets()  {}
        virtual void queryState()    {}
        virtual void querySwitches() {}
        virtual void queryVersion()  {}

        // Subscribe
        //--------------------------------------------------

        // `owner` is the subscriber's `this`, so it can unsubscribe all at once.
        // Signal channels carry no payload -- "it changed, go read the machine".
        void onConnection(void* owner, const std::function<void(ConnectionEvent&)>& f) { connectionDispatcher.listen(&MachineBase::connectionKey, owner, f); }
        void onState     (void* owner, const std::function<void(StateEvent&)>&      f) { stateDispatcher.listen(&MachineBase::stateKey, owner, f); }
        void onLog       (void* owner, const std::function<void(LogEvent&)>&        f) { logDispatcher.listen(&MachineBase::logKey, owner, f); }

        // Signal channels (no payload): "<thing> updated -- go check the machine".
        void onTelemetry (void* owner, std::function<void()> f) { telemetryDispatcher.listen (&MachineBase::telemetryKey,  owner, [f = std::move(f)](Signal&) { f(); }); }
        void onConnect   (void* owner, std::function<void()> f) { connectDispatcher.listen   (&MachineBase::connectKey,    owner, [f = std::move(f)](Signal&) { f(); }); }
        void onDisconnect(void* owner, std::function<void()> f) { disconnectDispatcher.listen(&MachineBase::disconnectKey, owner, [f = std::move(f)](Signal&) { f(); }); }

        // Report -- cache, then announce. Fed by the bound adapter.
        //--------------------------------------------------

        void report(ConnectionEvent e) {
            status = e.status;
            connectionDispatcher.tell(&MachineBase::connectionKey, e);

            Signal signal;
            if (e.status == ConnectionStatus::Connected) {
                connectDispatcher.tell(&MachineBase::connectKey, signal);
            } else if (e.status == ConnectionStatus::Disconnected || e.status == ConnectionStatus::Error) {
                disconnectDispatcher.tell(&MachineBase::disconnectKey, signal);
            }
        }
        void report(TelemetryEvent e)  {
            telemetry.spindle.pos    = { e.mx, e.my, e.mz, e.ma, e.mb };
            telemetry.spindle.rpm    = e.spindleRpm;
            telemetry.spindle.target = e.spindleTarget;
            telemetry.spindle.scale  = e.spindleScale;
            telemetry.spindle.load   = e.spindleLoad;
            telemetry.spindle.temp   = e.spindleTemp;

            telemetry.tool.pos         = { e.wx, e.wy, e.wz, e.wa, e.wb };
            telemetry.tool.speed       = e.feed;
            telemetry.tool.speedTarget = e.feedTarget;
            telemetry.tool.scale       = e.feedScale;
            telemetry.tool.number      = e.tool;
            telemetry.tool.offset      = e.toolOffset;

            telemetry.laser.power = e.laserPower;
            telemetry.laser.scale = e.laserScale;

            Signal s; telemetryDispatcher.tell(&MachineBase::telemetryKey, s);
        }
        void report(StateEvent e)      { stateDispatcher.tell(&MachineBase::stateKey, e); }
        void report(LogEvent e)        { logDispatcher.tell(&MachineBase::logKey, e); }
    };
}
