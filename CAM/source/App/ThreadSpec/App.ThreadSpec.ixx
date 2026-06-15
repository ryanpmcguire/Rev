module;

#include <cmath>
#include <string>
#include <vector>

export module Cam.App.ThreadSpec;

export namespace Cam::App {

    // One row of the common ISO metric coarse thread series.  A thread is fully
    // described by its callout, so this is all the thread mill needs: the major
    // diameter and pitch it must achieve, plus the RECOMMENDED PRE-MILL BORE --
    // the plain hole a prior drilling step should leave before the thread is cut
    // (≈ the standard tap-drill size for ~75% engagement).
    struct ThreadSpec {
        const char* label;     // dropdown label AND stored identity, e.g. "M2.5 x 0.45"
        double major;          // nominal major diameter (mm)
        double pitch;          // pitch (mm/rev)
        double preBore;        // recommended internal pre-mill bore (mm)
    };

    // The dropdown's sentinel for a hand-dialed callout that matches no preset.
    inline constexpr const char* threadCustomValue = "custom";
    inline constexpr const char* threadCustomLabel = "Custom";

    // ISO metric coarse series with standard tap-drill bores.  Kept small and
    // human-checkable; the user dials in anything exotic via the Custom entry.
    inline const std::vector<ThreadSpec>& threadPresets() {
        static const std::vector<ThreadSpec> table = {
            { "M1.6 x 0.35",  1.6,  0.35,  1.25 },
            { "M2 x 0.4",     2.0,  0.40,  1.60 },
            { "M2.5 x 0.45",  2.5,  0.45,  2.05 },
            { "M3 x 0.5",     3.0,  0.50,  2.50 },
            { "M4 x 0.7",     4.0,  0.70,  3.30 },
            { "M5 x 0.8",     5.0,  0.80,  4.20 },
            { "M6 x 1.0",     6.0,  1.00,  5.00 },
            { "M8 x 1.25",    8.0,  1.25,  6.80 },
            { "M10 x 1.5",   10.0,  1.50,  8.50 },
            { "M12 x 1.75",  12.0,  1.75, 10.20 },
            { "M16 x 2.0",   16.0,  2.00, 14.00 },
            { "M20 x 2.5",   20.0,  2.50, 17.50 },
        };
        return table;
    }

    // Recommended internal pre-mill bore for an arbitrary callout, when no preset
    // applies: the standard ~75%-engagement approximation (major - pitch).
    inline double recommendedPreBore(double major, double pitch) {
        return major - pitch;
    }

    // The LARGEST callout whose major diameter fits within a selected hole -- the
    // sensible default when starting a thread mill on a hole: thread it as big as
    // the hole allows.  Nullptr when the hole is smaller than the smallest preset.
    inline const ThreadSpec* largestThreadFitting(double holeDiameter) {
        const ThreadSpec* best = nullptr;
        for (const ThreadSpec& s : threadPresets()) {   // ascending by major
            if (s.major <= holeDiameter + 1e-6) { best = &s; }
        }
        return best;
    }

    // The preset nearest a measured major diameter (a starting guess only -- the
    // user may then pick a different thread for an intentionally oversized hole).
    inline const ThreadSpec* nearestThreadPreset(double major) {
        const ThreadSpec* best = nullptr;
        double bestErr = 1e30;
        for (const ThreadSpec& s : threadPresets()) {
            const double err = std::fabs(s.major - major);
            if (err < bestErr) { bestErr = err; best = &s; }
        }
        return best;
    }

    // The preset by its label (dropdown value), or nullptr for Custom/unknown.
    inline const ThreadSpec* threadPresetByLabel(const std::string& label) {
        for (const ThreadSpec& s : threadPresets()) {
            if (label == s.label) { return &s; }
        }
        return nullptr;
    }

    // The dropdown value that matches a callout (major + pitch) within tolerance,
    // else the Custom sentinel -- so editing a field flips the menu to Custom.
    inline std::string threadPresetValueFor(double major, double pitch) {
        for (const ThreadSpec& s : threadPresets()) {
            if (std::fabs(s.major - major) < 1e-4 && std::fabs(s.pitch - pitch) < 1e-4) {
                return s.label;
            }
        }
        return threadCustomValue;
    }
}
