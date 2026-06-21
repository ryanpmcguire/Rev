module;

#include <string>

export module Machine.Events;

export namespace Machine {

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

    // A decoded status-frame reading: the pieces a single frame delivered. The
    // machine folds these into its own telemetry. MPos (machine/spindle) and
    // WPos (work/tool-tip) carry up to five axes: linear X/Y/Z + rotary A and B.
    struct TelemetryEvent {
        float mx = 0, my = 0, mz = 0, ma = 0, mb = 0;   // MPos
        float wx = 0, wy = 0, wz = 0, wa = 0, wb = 0;   // WPos

        float feed       = 0;     // F[0] current feed (mm/min)
        float feedTarget = 0;     // F[1] feed cap
        float feedScale  = 100;   // F[2] feed override (%)

        float spindleRpm    = 0;     // S[0] current rpm
        float spindleTarget = 0;     // S[1] commanded rpm
        float spindleScale  = 100;   // S[2] spindle override (%)
        float spindleLoad   = 0;     // S[3] load (%)
        float spindleTemp   = 0;     // S[4] temperature (deg C)

        int   tool       = 0;     // T[0] loaded tool number
        float toolOffset = 0;     // T[1] tool-length offset

        float laserPower = 0;     // L[3] laser power
        float laserScale = 100;   // L[4] laser override (%)
    };

    // Coarse run-state ("Idle", "Run", "Alarm", ...).
    struct StateEvent      { std::string state; };

    // A raw line of machine back-talk, for the operator log.
    struct LogEvent        { std::string line; };

    // A no-payload edge event (connection came up / went down). Carries nothing;
    // it exists so the edge channels can be dispatchers like everything else.
    struct Signal {};
}
