module;

#include <string>
#include <vector>

export module Cam.App.MachineSettings;

export namespace Cam::App {

    // A named bookmark of an absolute machine position (MPos X/Y/Z/A).  These
    // are machine settings, not project data — they persist app-wide alongside
    // the session (see Cam.App.Persist).
    struct OriginAlias {
        std::string name = "Origin";
        double x = 0.0, y = 0.0, z = 0.0, a = 0.0;
        bool   valid = false;   // has a captured / typed position
    };

    // App-wide machine settings.  Lives on AppState and is serialised into the
    // same persist.json as the project session.
    struct MachineSettings {

        std::vector<OriginAlias> origins;
        int                      activeOrigin = 0;

        MachineSettings() {
            // First-run defaults; replaced wholesale when a session is loaded.
            origins = { { "Origin 1" }, { "Origin 2" }, { "Origin 3" } };
        }
    };
}
