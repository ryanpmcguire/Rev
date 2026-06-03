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
