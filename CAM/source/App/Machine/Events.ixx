module;

#include <string>

export module Cam.Machine.Events;

// ------------------------------------------------------------------
// The abstract MACHINE's outward vocabulary: the status it reports and the
// discrete events it dispatches as it connects, moves, probes, and changes
// tools.  These are what any UI listens to -- independent of the concrete
// controller, which just fills and emits them.
// ------------------------------------------------------------------

export namespace Cam::Machine {

    enum class ConnectionStatus { Disconnected, Connecting, Connected, Error };

    // What the machine is doing RIGHT NOW, at the granularity of the typed paths
    // it executes.  It never knows what the material looks like (the software's
    // job), but it always knows whether it is travelling, cutting, probing, or
    // changing a tool.
    enum class Activity { Idle, Traveling, Cutting, Probing, ToolChanging };

    inline const char* activityName(Activity a) {
        switch (a) {
            case Activity::Idle:         return "Idle";
            case Activity::Traveling:    return "Traveling";
            case Activity::Cutting:      return "Cutting";
            case Activity::Probing:      return "Probing";
            case Activity::ToolChanging: return "Changing tool";
        }
        return "?";
    }

    // The PUBLIC tool-change phase (a controller may track a finer internal
    // state machine and map onto this).
    enum class ToolChangePhase { None, Seeking, Standby, Confirming };

    // -- Event payloads ------------------------------------------------

    struct TelemetryEvent  { float x = 0, y = 0, z = 0, a = 0; };
    struct StateEvent      { std::string state; };
    struct ConnectionEvent { ConnectionStatus status = ConnectionStatus::Disconnected; std::string message; };
    struct LogEvent        { std::string line; };
    struct ActivityEvent   { Activity activity = Activity::Idle; };
    struct ArmEvent        { bool armed = false; bool spindleArmed = false; };
    struct StartEvent      {};

    // Result of a G38.x probe move.  `triggered` is the controller's success
    // flag: 1 = the probe made contact, 0 = the move reached its target without
    // ever triggering (a probe fail).  x/y/z are the MACHINE position at contact.
    struct ProbeEvent      { float x = 0, y = 0, z = 0; bool triggered = false; };

    // Raised when a spindle-start was REFUSED because a probe / spindle-inhibited
    // tool is loaded.  `blocked` is the suppressed line; `reason` is operator-facing.
    struct SafetyEvent     { std::string reason; std::string blocked; };

    // Shared by the tool-change lifecycle channels; `phase` says which transition
    // the machine just entered.  `aborted` (on Complete) marks a failure; `reason`
    // explains it.
    struct ToolChangeEvent {
        int             slot = 0;
        ToolChangePhase phase = ToolChangePhase::None;
        bool            aborted = false;
        std::string     reason;
    };
}
