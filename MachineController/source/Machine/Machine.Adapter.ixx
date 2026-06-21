module;

#include <functional>

export module Machine.Adapter;

import Rev.Core.Dispatcher;

import Machine.Events;
import Machine.Command;

export namespace Machine {

    // Pure translation between the system's vocabulary and one machine's wire
    // dialect. Accepts Commands, emits decoded events; knows no consumer.
    struct Adapter {

        // Outward channels -- consumers subscribe via on*(), concrete adapters fire emit().
        //--------------------------------------------------

        virtual void connectionKey(ConnectionEvent&) {}
        virtual void telemetryKey (TelemetryEvent&)  {}
        virtual void stateKey     (StateEvent&)      {}
        virtual void logKey       (LogEvent&)        {}

        Rev::Core::Dispatcher<ConnectionEvent> connectionDispatcher;
        Rev::Core::Dispatcher<TelemetryEvent>  telemetryDispatcher;
        Rev::Core::Dispatcher<StateEvent>      stateDispatcher;
        Rev::Core::Dispatcher<LogEvent>        logDispatcher;

        void onConnection(const std::function<void(ConnectionEvent&)>& f) { connectionDispatcher.listen(&Adapter::connectionKey, f); }
        void onTelemetry (const std::function<void(TelemetryEvent&)>&  f) { telemetryDispatcher.listen(&Adapter::telemetryKey, f); }
        void onState     (const std::function<void(StateEvent&)>&      f) { stateDispatcher.listen(&Adapter::stateKey, f); }
        void onLog       (const std::function<void(LogEvent&)>&        f) { logDispatcher.listen(&Adapter::logKey, f); }

        void emit(ConnectionEvent e) { connectionDispatcher.tell(&Adapter::connectionKey, e); }
        void emit(TelemetryEvent e)  { telemetryDispatcher.tell(&Adapter::telemetryKey, e); }
        void emit(StateEvent e)      { stateDispatcher.tell(&Adapter::stateKey, e); }
        void emit(LogEvent e)        { logDispatcher.tell(&Adapter::logKey, e); }

        // Inward -- lifecycle + commands, overridden by concrete adapters.
        //--------------------------------------------------

        virtual void connect()    {}
        virtual void disconnect() {}
        virtual void sendCommand(const Command&) {}

        // Create / Destroy
        //--------------------------------------------------

        Adapter() {}
        virtual ~Adapter() {}
    };
}
