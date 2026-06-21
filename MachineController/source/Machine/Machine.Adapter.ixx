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

        // Channel keys (dispatcher identity)
        virtual void connectionKey(Event::Connection&) {}
        virtual void telemetryKey (Event::Telemetry&)  {}
        virtual void infoKey      (Event::Info&)       {}
        virtual void stateKey     (Event::State&)      {}
        virtual void logKey       (Event::Log&)        {}

        // Channels
        Rev::Core::Dispatcher<Event::Connection> connectionDispatcher;
        Rev::Core::Dispatcher<Event::Telemetry>  telemetryDispatcher;
        Rev::Core::Dispatcher<Event::Info>       infoDispatcher;
        Rev::Core::Dispatcher<Event::State>      stateDispatcher;
        Rev::Core::Dispatcher<Event::Log>        logDispatcher;

        // Subscribe
        void onConnection(const std::function<void(Event::Connection&)>& f) { connectionDispatcher.listen(&Adapter::connectionKey, f); }
        void onTelemetry (const std::function<void(Event::Telemetry&)>&  f) { telemetryDispatcher.listen(&Adapter::telemetryKey, f); }
        void onInfo      (const std::function<void(Event::Info&)>&       f) { infoDispatcher.listen(&Adapter::infoKey, f); }
        void onState     (const std::function<void(Event::State&)>&      f) { stateDispatcher.listen(&Adapter::stateKey, f); }
        void onLog       (const std::function<void(Event::Log&)>&        f) { logDispatcher.listen(&Adapter::logKey, f); }

        // Emit (concrete adapters fire these)
        void emit(Event::Connection e) { connectionDispatcher.tell(&Adapter::connectionKey, e); }
        void emit(Event::Telemetry e)  { telemetryDispatcher.tell(&Adapter::telemetryKey, e); }
        void emit(Event::Info e)       { infoDispatcher.tell(&Adapter::infoKey, e); }
        void emit(Event::State e)      { stateDispatcher.tell(&Adapter::stateKey, e); }
        void emit(Event::Log e)        { logDispatcher.tell(&Adapter::logKey, e); }

        // Inward (overridden by concrete adapters)
        virtual void connect()    {}
        virtual void disconnect() {}
        virtual void sendCommand(const Command::CommandBase&) {}

        // Construct/destruct
        Adapter() {}
        virtual ~Adapter() {}
    };
}
