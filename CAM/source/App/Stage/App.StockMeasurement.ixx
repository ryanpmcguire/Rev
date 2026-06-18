module;

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

export module Cam.App.StockMeasurement;

// ------------------------------------------------------------------
// Cam::App::StockMeasurement
//
// The model behind the Measure Stock meta-program.  By this point everything
// upstream is KNOWN: probe geometry (probe calibration), the rotary axis Y/Z
// (machine calibration), the machine position (homing), and the tip (touch-off).
// So probing the stock against the true axis resolves only the few residual
// unknowns about the raw stock itself:
//
//   * its angular offset (how the mounted top face is tilted vs. level),
//   * its cross-section size (width W / height H, or a radius), and
//   * where that cross-section sits relative to the rotary axis (centre Y/Z).
//
// PLAN (mirrors the calibration routines; driven by the window):
//   1. Park at a safe height above the known axis Y/Z.
//   2. User confirms the work-origin X (Set Origin -- X only on a pre-calibrated
//      machine; Y/Z come from the measured axis).
//   3. LEVEL the top face (lateral spread at A=0) -> angular offset + top distance.
//   4. FLIP through A = 90/180/270 and probe each face up -> its axis distance.
//
// The four axis->face distances give W, H, and the centre offset directly; the
// result feeds the four material-state (Stock +/-Y, +/-Z) steps so the nominal
// prism becomes the real stock.
// ------------------------------------------------------------------

export namespace Cam::App {

    struct StockMeasurement {

        enum class Phase { Idle, Leveling, Sampling, Fitting, Review, Failed };

        // Four orientations: bring each prism face (or cylinder quadrant) "up" by
        // rotating the A axis.  Face 0 (A=0) is also the leveling face.
        static constexpr int kFaceCount = 4;
        static constexpr double kFaceAngle[kFaceCount] = { 0.0, 90.0, 180.0, 270.0 };

        // -- Known inputs (from the calibrated machine + probe) --------
        double axisY = 0.0;        // rotary axis Y (machine frame, mm)
        double axisZ = 0.0;        // rotary axis Z (machine frame, mm)
        double probeRadius = 0.5;  // calibrated stylus radius (mm)
        bool   cylinder = false;   // measuring a cylinder vs a rectangular prism

        // -- Spec (user-suggested geometry; seeds safe standoffs) ------
        double suggestedWidth  = 10.0;
        double suggestedHeight = 10.0;
        double suggestedRadius = 5.0;
        double suggestedLength = 0.0;
        double linearBound  = 10.0;   // lateral spread per face, along Y (mm)
        int    samplePoints = 3;      // lateral probes per face

        // -- Runtime --------------------------------------------------
        Phase phase = Phase::Idle;

        struct Sample {
            int    face    = 0;       // 0..3
            double lateral = 0.0;     // Y offset from directly-above-axis (mm)
            double z       = 0.0;     // measured surface height (machine Z, mm)
        };
        std::vector<Sample> samples;

        // -- Result ---------------------------------------------------
        bool   haveResult = false;
        double resultWidth  = 0.0;     // axis Y extent (mm)
        double resultHeight = 0.0;     // axis Z extent (mm)
        double resultRadius = 0.0;     // cylinder radius (mm)
        double resultCenterY = 0.0;    // cross-section centre vs axis (mm)
        double resultCenterZ = 0.0;
        double resultAngleDeg = 0.0;   // A offset that levels the top face (deg)
        double resultResidual = 0.0;
        double faceDistance[kFaceCount] = {};   // axis -> each face (mm)

        static constexpr double kPi = 3.14159265358979;

        // Lateral offsets across the face (along Y), centred over the axis.
        std::vector<double> lateralSchedule() const {
            std::vector<double> out;
            const int    m    = std::max(samplePoints, 1);
            const double half = std::max(linearBound, 0.0) * 0.5;
            for (int i = 0; i < m; i++) {
                const double f = (m == 1) ? 0.0 : (double(i) / (m - 1)) * 2.0 - 1.0;
                out.push_back(f * half);
            }
            return out;
        }

        // The nominal axis->face distance for `face`, from the suggested geometry --
        // used by the driver to stand off just above where it expects the surface.
        double expectedDistance(int face) const {
            if (cylinder) { return suggestedRadius; }
            const bool topBottom = (face % 2 == 0);   // faces 0,2 along Z; 1,3 along Y
            return 0.5 * (topBottom ? suggestedHeight : suggestedWidth);
        }

        int plannedContacts() const {
            return kFaceCount * std::max(samplePoints, 1);
        }

        void clampSpec() {
            linearBound  = std::max(linearBound, 0.0);
            samplePoints = std::clamp(samplePoints, 1, 30);
        }

        void reset() {
            phase = Phase::Idle;
            samples.clear();
            haveResult = false;
            resultWidth = resultHeight = resultRadius = 0.0;
            resultCenterY = resultCenterZ = resultAngleDeg = resultResidual = 0.0;
            for (double& d : faceDistance) { d = 0.0; }
        }

        // Line-fit z = slope*lateral + intercept over one face's samples.  Returns
        // false if that face has too few points.  `intercept` is the surface height
        // directly above the axis (lateral 0); `slope` is dz/dy (the local tilt).
        bool fitFace(int face, double& slope, double& intercept, int& count) const {
            double Sx = 0, Sy = 0, Sxx = 0, Sxy = 0; int n = 0;
            for (const Sample& s : samples) {
                if (s.face != face) { continue; }
                Sx += s.lateral; Sy += s.z; Sxx += s.lateral * s.lateral;
                Sxy += s.lateral * s.z; n++;
            }
            count = n;
            if (n == 0) { return false; }
            if (n == 1) { slope = 0.0; intercept = Sy; return true; }
            const double det = n * Sxx - Sx * Sx;
            if (std::fabs(det) < 1e-12) { slope = 0.0; intercept = Sy / n; return true; }
            slope     = (n * Sxy - Sx * Sy) / det;
            intercept = (Sy - slope * Sx) / n;
            return true;
        }

        // Solve the stock placement from the four face fits.  Each face's intercept
        // is the contact height directly above the axis when that face is up; the
        // axis->face distance is (intercept - axisZ) at A=0 face, generalized for
        // each orientation (the probe always touches the up-facing surface, so the
        // distance is measured along +Z from the axis every time).
        bool fit() {
            haveResult = false;

            double slope[kFaceCount] = {}; double inter[kFaceCount] = {};
            int    cnt[kFaceCount] = {};
            int filled = 0;
            for (int f = 0; f < kFaceCount; f++) {
                if (fitFace(f, slope[f], inter[f], cnt[f]) && cnt[f] > 0) { filled++; }
            }
            if (filled < kFaceCount) { return false; }

            for (int f = 0; f < kFaceCount; f++) {
                faceDistance[f] = inter[f] - axisZ;
            }

            // Two faces along Z (0 up, 2 down) -> height; two along Y (1,3) -> width.
            resultHeight = faceDistance[0] + faceDistance[2];
            resultWidth  = faceDistance[1] + faceDistance[3];
            resultCenterZ = 0.5 * (faceDistance[0] - faceDistance[2]);
            resultCenterY = 0.5 * (faceDistance[1] - faceDistance[3]);
            resultRadius  = 0.25 * (faceDistance[0] + faceDistance[1] +
                                    faceDistance[2] + faceDistance[3]);

            // Angular offset: the top face's measured tilt (dz/dy) is the rotation
            // about the X (rotary) axis that would level it.
            resultAngleDeg = std::atan(slope[0]) * 180.0 / kPi;

            // Residual: RMS of each sample about its face's fitted line.
            double sse = 0.0; int n = 0;
            for (const Sample& s : samples) {
                const double pred = slope[s.face] * s.lateral + inter[s.face];
                const double e = pred - s.z; sse += e * e; n++;
            }
            resultResidual = n > 0 ? std::sqrt(sse / n) : 0.0;

            haveResult = true;
            return true;
        }

        static std::string phaseLabel(Phase p) {
            switch (p) {
                case Phase::Idle:     return "Ready";
                case Phase::Leveling: return "Leveling";
                case Phase::Sampling: return "Probing faces";
                case Phase::Fitting:  return "Fitting";
                case Phase::Review:   return "Review";
                case Phase::Failed:   return "Failed";
            }
            return "";
        }
    };
}
