module;

#include <string>

export module Machine.App.Events;

export namespace App {

    // The machine's outward vocabulary: status + the structs it emits.

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

    // -- Event payloads ------------------------------------------------

    struct ConnectionEvent { ConnectionStatus status = ConnectionStatus::Disconnected; std::string message; };

    // Live machine position (linear X/Y/Z + rotary A).
    struct TelemetryEvent  { float x = 0, y = 0, z = 0, a = 0; };

    // Coarse run-state ("Idle", "Run", "Alarm", ...).
    struct StateEvent      { std::string state; };

    // A raw line of machine back-talk, for the operator log.
    struct LogEvent        { std::string line; };
}
