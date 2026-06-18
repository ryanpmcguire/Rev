module;

#include <cstddef>
#include <string>

export module Cam.App.MachineProfile;

import Cam.App.Model;

export namespace Cam::App {

    // User-facing machine definition: capabilities plus STEP asset references.
    // Serialized as `<name>.machine.json` inside a machine-named subfolder.
    struct MachineProfile {

        std::string name = "New Machine";
        std::string filePath = "";

        double spindleMinRpm = 0.0;
        double spindleMaxRpm = 24000.0;

        bool axisX = true;
        bool axisY = true;
        bool axisZ = true;

        bool rotaryX = false;
        bool rotaryY = false;
        bool rotaryZ = false;

        // The rotary axis LOCATION in machine coordinates -- persistent machine
        // geometry, established by machine calibration (or typed by hand).  The axis
        // is a line; for a rotary-about-X machine, X is just a reference point along
        // it (arbitrary), while Y and Z are the meaningful, calibrated location of
        // the centreline.  Because the work frame IS the machine frame, this is the
        // single source of truth for where the part pivots.
        double rotaryAxisX = 0.0;
        double rotaryAxisY = 0.0;
        double rotaryAxisZ = 0.0;
        bool   rotaryAxisCalibrated = false;
        double rotaryAxisSigma = 0.0;   // 1-sigma confidence on Y/Z (mm)

        // Filenames relative to the machine folder (empty = none).
        std::string spindleStep;
        std::string bedStep;
        std::string workpieceStep;
        std::string rotaryStep;

        // Runtime STEP geometry (not serialized).
        Model spindleModel;
        std::size_t spindleMeshRevision = 0;
    };

    // Absolute paths chosen in the UI before save copies them into the folder.
    struct MachineStepSources {
        std::string spindle;
        std::string bed;
        std::string workpiece;
        std::string rotary;
    };
}
