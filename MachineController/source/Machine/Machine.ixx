module;

#include <functional>
#include <string>
#include <vector>

export module App.Machine;

import Rev.Core.Dispatcher;
import Rev.Core.Observable;

import App.Events;
import App.Command;
import App.Adapter;

export namespace App {

    // A machine: what the system listens to. Owns its dispatchers, binds one
    // adapter, and re-broadcasts the adapter's channels as its own.
    struct Machine {

        // Definitions
        //--------------------------------------------------

        // The machine's own telemetry: its current readings, grouped by subsystem
        // and held as the single source of truth. report(TelemetryEvent) folds in
        // decoded pieces; the telemetry channel then announces the change.
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

        // The machine's INFO: the slower-changing, request/response facts about
        // the machine -- identity, network, coordinate offsets, the tool table,
        // the last probe. Unlike telemetry (streamed via "?"), these are fetched
        // on demand (version, $#, ...) and cached here as the single source of
        // truth. (Open-ended queries -- config-get, file listings -- are NOT
        // modelled here; they are answered ad hoc, not cached as fixed fields.)
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

        Machine() {}
        virtual ~Machine() {}

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

        // Drop every subscription a given owner registered, across all channels.
        // A subscriber calls this with its own `this` as it is destroyed.
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

        virtual void connect()     { if (adapter) { adapter->connect(); } }
        virtual void disconnect()  { if (adapter) { adapter->disconnect(); } }

        // Actions
        virtual void unlock()      { if (adapter) { adapter->sendCommand(Command{ Command::Type::Unlock }); } }
        virtual void reset()       { if (adapter) { adapter->sendCommand(Command{ Command::Type::Reset }); } }
        virtual void changeTool(int n) { if (adapter) { adapter->sendCommand(Command{ Command::Type::ChangeTool, n }); } }

        // Queries -- ask the machine to tell us something; the reply lands back
        // in telemetry / info via the adapter's decode.
        virtual void queryStatus()   { if (adapter) { adapter->sendCommand(Command{ Command::Type::QueryStatus }); } }
        virtual void queryOffsets()  { if (adapter) { adapter->sendCommand(Command{ Command::Type::QueryOffsets }); } }
        virtual void queryState()    { if (adapter) { adapter->sendCommand(Command{ Command::Type::QueryState }); } }
        virtual void querySwitches() { if (adapter) { adapter->sendCommand(Command{ Command::Type::QuerySwitches }); } }
        virtual void queryVersion()  { if (adapter) { adapter->sendCommand(Command{ Command::Type::QueryVersion }); } }

        // Subscribe
        //--------------------------------------------------

        // `owner` is the subscriber's `this`, recorded so it can later drop ALL
        // of its subscriptions in one call (see unsubscribe).
        //
        // Payload channels carry data that has no cached home (a log line, a
        // status message). Signal channels carry NOTHING -- the data already
        // lives on the machine (status / telemetry), so the channel just says
        // "it changed, go read it." That keeps one source of truth.
        void onConnection(void* owner, const std::function<void(ConnectionEvent&)>& f) { connectionDispatcher.listen(&Machine::connectionKey, owner, f); }
        void onState     (void* owner, const std::function<void(StateEvent&)>&      f) { stateDispatcher.listen(&Machine::stateKey, owner, f); }
        void onLog       (void* owner, const std::function<void(LogEvent&)>&        f) { logDispatcher.listen(&Machine::logKey, owner, f); }

        // Signal channels (no payload): "<thing> updated -- go check the machine".
        void onTelemetry (void* owner, std::function<void()> f) { telemetryDispatcher.listen (&Machine::telemetryKey,  owner, [f = std::move(f)](Signal&) { f(); }); }
        void onConnect   (void* owner, std::function<void()> f) { connectDispatcher.listen   (&Machine::connectKey,    owner, [f = std::move(f)](Signal&) { f(); }); }
        void onDisconnect(void* owner, std::function<void()> f) { disconnectDispatcher.listen(&Machine::disconnectKey, owner, [f = std::move(f)](Signal&) { f(); }); }

        // Report -- fan-in from the bound adapter: cache what is worth holding,
        // then announce on the matching channel.
        //--------------------------------------------------

        void report(ConnectionEvent e) {
            status = e.status;
            connectionDispatcher.tell(&Machine::connectionKey, e);

            Signal signal;
            if (e.status == ConnectionStatus::Connected) {
                connectDispatcher.tell(&Machine::connectKey, signal);
            } else if (e.status == ConnectionStatus::Disconnected || e.status == ConnectionStatus::Error) {
                disconnectDispatcher.tell(&Machine::disconnectKey, signal);
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

            Signal s; telemetryDispatcher.tell(&Machine::telemetryKey, s);
        }
        void report(StateEvent e)      { stateDispatcher.tell(&Machine::stateKey, e); }
        void report(LogEvent e)        { logDispatcher.tell(&Machine::logKey, e); }
    };
}
