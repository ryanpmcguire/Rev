module;

#include <functional>

export module Machine.App.Machine;

import Rev.Core.Dispatcher;
import Rev.Core.Observable;

import Machine.App.Events;
import Machine.App.Command;
import Machine.App.Adapter;

export namespace App {

    using namespace Rev;

    // A machine: what the system listens to. Owns its dispatchers, binds one
    // adapter, and re-broadcasts the adapter's channels as its own.
    struct Machine {

        // The link to the physical machine. Null until a concrete machine binds one.
        Adapter* adapter = nullptr;

        // Latest connection status, for a late subscriber.
        Core::Observable<ConnectionStatus> status;

        bool connected() const { return status.value == ConnectionStatus::Connected; }

        // Outward channels -- consumers subscribe via on*().
        //--------------------------------------------------

        virtual void connectionKey(ConnectionEvent&) {}
        virtual void telemetryKey (TelemetryEvent&)  {}
        virtual void stateKey     (StateEvent&)      {}
        virtual void logKey       (LogEvent&)        {}

        Core::Dispatcher<ConnectionEvent> connectionDispatcher;
        Core::Dispatcher<TelemetryEvent>  telemetryDispatcher;
        Core::Dispatcher<StateEvent>      stateDispatcher;
        Core::Dispatcher<LogEvent>        logDispatcher;

        void onConnection(const std::function<void(ConnectionEvent&)>& f) { connectionDispatcher.listen(&Machine::connectionKey, f); }
        void onTelemetry (const std::function<void(TelemetryEvent&)>&  f) { telemetryDispatcher.listen(&Machine::telemetryKey, f); }
        void onState     (const std::function<void(StateEvent&)>&      f) { stateDispatcher.listen(&Machine::stateKey, f); }
        void onLog       (const std::function<void(LogEvent&)>&        f) { logDispatcher.listen(&Machine::logKey, f); }

        // Cache what is worth holding, then fan out. Fed by the bound adapter.
        void report(ConnectionEvent e) { status = e.status; connectionDispatcher.tell(&Machine::connectionKey, e); }
        void report(TelemetryEvent e)  { telemetryDispatcher.tell(&Machine::telemetryKey, e); }
        void report(StateEvent e)      { stateDispatcher.tell(&Machine::stateKey, e); }
        void report(LogEvent e)        { logDispatcher.tell(&Machine::logKey, e); }

        // Bind -- adopt an adapter and forward its channels into ours.
        //--------------------------------------------------

        void bindAdapter(Adapter* a) {

            adapter = a;
            if (!adapter) { return; }

            adapter->onConnection([this](ConnectionEvent& e) { report(e); });
            adapter->onTelemetry ([this](TelemetryEvent&  e) { report(e); });
            adapter->onState     ([this](StateEvent&      e) { report(e); });
            adapter->onLog       ([this](LogEvent&        e) { report(e); });
        }

        // Verbs -- forward through the adapter (a machine with none does nothing).
        //--------------------------------------------------

        virtual void connect()     { if (adapter) { adapter->connect(); } }
        virtual void disconnect()  { if (adapter) { adapter->disconnect(); } }
        virtual void unlock()      { if (adapter) { adapter->sendCommand(Command{ Command::Type::Unlock }); } }
        virtual void reset()       { if (adapter) { adapter->sendCommand(Command{ Command::Type::Reset }); } }
        virtual void queryStatus() { if (adapter) { adapter->sendCommand(Command{ Command::Type::QueryStatus }); } }

        Machine() {}
        virtual ~Machine() {}
    };
}
