module;

#include <cstddef>
#include <cmath>
#include <string>

export module Cam.App.MachineProfile;

import Cam.App.Model;
import Rev.Core.Pos3;

export namespace Cam::App {

    using Rev::Core::Pos3;

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

        // THE ROTARY AXIS FRAME -- persistent MACHINE geometry, expressed in the
        // machine-absolute TIP frame (WPos): the one frame everything physical
        // shares, established by the machine (touch-off) and never reconstructed by
        // us from a tool length.  The axis is a line: the ORIGIN sits on the
        // centreline and the DIRECTION is the rotation axis (a rotary-about-X
        // machine points along +X).  X (a reference point along the axis) is
        // arbitrary/on-faith; Y and Z are the meaningful, calibrated location of the
        // centreline.  THE WORK FRAME IS DERIVED FROM THIS: its origin Y/Z come from
        // here (Set Origin only contributes X + the A index), and the fixture/chuck
        // is displayed AT and oriented BY this frame.
        double rotaryAxisX = 0.0;
        double rotaryAxisY = 0.0;
        double rotaryAxisZ = 0.0;
        double rotaryAxisDirX = 1.0;   // rotation-axis direction (machine +X default)
        double rotaryAxisDirY = 0.0;
        double rotaryAxisDirZ = 0.0;
        bool   rotaryAxisCalibrated = false;
        double rotaryAxisSigma = 0.0;   // 1-sigma confidence on Y/Z (mm)

        // The rotary axis as an orthonormal frame (origin + X along the rotation
        // axis, Y/Z spanning the perpendicular plane).  Built right-handed from the
        // stored direction so a machine-aligned axis yields exactly machine X/Y/Z.
        void rotaryAxisFrame(Pos3& origin, Pos3& X, Pos3& Y, Pos3& Z) const {
            origin = { (float)rotaryAxisX, (float)rotaryAxisY, (float)rotaryAxisZ };
            Pos3 ax = { (float)rotaryAxisDirX, (float)rotaryAxisDirY, (float)rotaryAxisDirZ };
            const float l = ax.pythag();
            X = (l > 1e-6f) ? ax * (1.0f / l) : Pos3{ 1.0f, 0.0f, 0.0f };
            // A reference not parallel to X, then Gram-Schmidt for Y, Z = X x Y.
            const Pos3 ref = (std::fabs(X.z) < 0.9f) ? Pos3{ 0.0f, 0.0f, 1.0f }
                                                     : Pos3{ 0.0f, 1.0f, 0.0f };
            Pos3 y = ref.cross(X); const float yl = y.pythag();
            Y = (yl > 1e-6f) ? y * (1.0f / yl) : Pos3{ 0.0f, 1.0f, 0.0f };
            Z = X.cross(Y);
        }

        // SET-ORIGIN AUTHORITY.  The work frame's Y and Z are ALWAYS the rotary
        // axis centreline -- never the live tool position.  Every "Set Origin" path
        // passes the live tip's Y/Z here; when the axis is calibrated we overwrite
        // them, so Set Origin can only ever move X (along the axis) and A (the
        // index).  Uncalibrated -> left as the faith fallback the caller supplied
        // (the bootstrap before an axis exists).
        //
        // The axis is stored in MACHINE coords (MPos) -- stable across Set Origin.
        // The work origin is captured at the TOOL TIP (WPos), so the caller passes
        // the LIVE MPos-WPos offset (deltaY/deltaZ = MPos - WPos per axis, from
        // telemetry) and we convert the MPos axis into the current WPos frame.
        void pinWorkOriginYZ(double& y, double& z, double deltaY, double deltaZ) const {
            if (rotaryAxisCalibrated) {
                y = rotaryAxisY - deltaY;
                z = rotaryAxisZ - deltaZ;
            }
        }

        // Filenames relative to the machine folder (empty = none).  rotaryStep is
        // the CHUCK (rotates with the rotary coordinate system); rotaryBodyStep is
        // the axis BODY/housing (pinned to the axis LOCATION but always oriented +Z,
        // never rotating).
        std::string spindleStep;
        std::string bedStep;
        std::string workpieceStep;
        std::string rotaryStep;
        std::string rotaryBodyStep;

        // Runtime STEP geometry (not serialized).
        Model spindleModel;
        std::size_t spindleMeshRevision = 0;

        // Runtime STEP geometry for the rotary-axis fixture (the chuck): a cylinder
        // whose STEP origin is the centre of its +X-facing face, displayed AT the
        // rotary axis frame and rotating with it.  Loaded from rotaryStep.
        Model rotaryModel;
        std::size_t rotaryMeshRevision = 0;

        // Runtime STEP geometry for the rotary-axis BODY/housing: pinned to the axis
        // origin but oriented +Z (does not rotate with the chuck).  From rotaryBodyStep.
        Model rotaryBodyModel;
        std::size_t rotaryBodyMeshRevision = 0;
    };

    // Absolute paths chosen in the UI before save copies them into the folder.
    struct MachineStepSources {
        std::string spindle;
        std::string bed;
        std::string workpiece;
        std::string rotary;
        std::string rotaryBody;
    };
}
