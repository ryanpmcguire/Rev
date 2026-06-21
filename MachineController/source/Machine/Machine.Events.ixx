module;

#include <string>

export module Machine.Events;

export namespace Machine {

    // Connection status -- a value carried by Event::Connection and cached on the
    // network (not itself an event).
    enum class ConnectionStatus {
        Disconnected,
        Connecting,
        Connected,
        Error
    };

    inline const char* connectionStatusName(ConnectionStatus s) {
        switch (s) {
            case ConnectionStatus::Disconnected: return "Disconnected";
            case ConnectionStatus::Connecting:   return "Connecting";
            case ConnectionStatus::Connected:    return "Connected";
            case ConnectionStatus::Error:        return "Error";
        }
        return "?";
    }

    // A no-payload channel ping ("something here changed, go read it"). Drives the
    // SignalChannel notify channels; carries nothing, so it stands apart from the
    // events below.
    struct Signal {};
}

// The machine's event vocabulary lives in its own namespace: one base (EventBase)
// subclassed per kind. Reference them by namespace -- Event::Telemetry,
// Event::Connection, ... (Machine::Event::Telemetry from outside) -- and downcast
// a stored EventBase* via its `type` to read the payload.
export namespace Machine::Event {

    struct EventBase {

        enum class Type {
            Connection,
            Telemetry,
            Info,
            State,
            Log
        };

        Type type;

        EventBase(Type type) : type(type) {}
        virtual ~EventBase() {}
    };

    // Connection came up / changed / failed
    struct Connection : EventBase {

        ConnectionStatus status = ConnectionStatus::Disconnected;
        std::string      message;

        Connection() : EventBase(Type::Connection) {}
        Connection(ConnectionStatus status, std::string message)
            : EventBase(Type::Connection), status(status), message(std::move(message)) {}
    };

    // A decoded status frame: MPos / WPos (X/Y/Z + rotary A,B) plus feed, spindle,
    // tool, and laser pieces. The machine folds these into its telemetry.
    struct Telemetry : EventBase {

        // MPos / WPos
        float mx = 0, my = 0, mz = 0, ma = 0, mb = 0;
        float wx = 0, wy = 0, wz = 0, wa = 0, wb = 0;

        // Feed (current / cap / override%)
        float feed       = 0;
        float feedTarget = 0;
        float feedScale  = 100;

        // Spindle (rpm / target / override% / load% / temp)
        float spindleRpm    = 0;
        float spindleTarget = 0;
        float spindleScale  = 100;
        float spindleLoad   = 0;
        float spindleTemp   = 0;

        // Tool (number / length offset)
        int   tool       = 0;
        float toolOffset = 0;

        // Laser (power / override%)
        float laserPower = 0;
        float laserScale = 100;

        Telemetry() : EventBase(Type::Telemetry) {}
    };

    // One decoded request/response fact (from "$#" etc.). `field` tags which datum
    // the coordinate / scalar carries; the machine folds it into its Info cache.
    struct Info : EventBase {

        enum class Field {
            FrameG54, FrameG55, FrameG56, FrameG57, FrameG58, FrameG59,
            FrameG28, FrameG30, FrameG92,
            ToolLengthOffset,
            Probe
        };

        Field field = Field::FrameG54;

        float x = 0, y = 0, z = 0, a = 0, b = 0;   // frame / probe coordinates
        float tlo = 0;                              // ToolLengthOffset
        bool  probeTriggered = false;               // Probe

        Info() : EventBase(Type::Info) {}
    };

    // Coarse run-state ("Idle", "Run", "Alarm", ...)
    struct State : EventBase {

        std::string state;

        State() : EventBase(Type::State) {}
        State(std::string state) : EventBase(Type::State), state(std::move(state)) {}
    };

    // A raw line of machine back-talk, for the operator log
    struct Log : EventBase {

        std::string line;

        Log() : EventBase(Type::Log) {}
        Log(std::string line) : EventBase(Type::Log), line(std::move(line)) {}
    };
}
