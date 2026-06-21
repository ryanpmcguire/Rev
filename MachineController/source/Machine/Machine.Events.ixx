module;

#include <string>

export module Machine.Events;

export namespace Machine {

    // The machine's outward vocabulary: status + the structs it emits.

    // Connection status
    enum class ConnectionStatus {

        Disconnected,
        Connecting,
        Connected,
        Error
    };

    // Status -> name
    inline const char* connectionStatusName(ConnectionStatus s) {

        switch (s) {
            case ConnectionStatus::Disconnected: return "Disconnected";
            case ConnectionStatus::Connecting:   return "Connecting";
            case ConnectionStatus::Connected:    return "Connected";
            case ConnectionStatus::Error:        return "Error";
        }

        return "?";
    }

    // Event payloads
    //--------------------------------------------------

    // Connection came up / changed / failed
    struct ConnectionEvent { ConnectionStatus status = ConnectionStatus::Disconnected; std::string message; };

    // A decoded status frame: MPos / WPos (X/Y/Z + rotary A,B) plus feed, spindle,
    // tool, and laser pieces. The machine folds these into its telemetry.
    struct TelemetryEvent {

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
    };

    // Coarse run-state ("Idle", "Run", "Alarm", ...)
    struct StateEvent { std::string state; };

    // A raw line of machine back-talk, for the operator log
    struct LogEvent { std::string line; };

    // A no-payload edge ("it changed, go look")
    struct Signal {};
}
