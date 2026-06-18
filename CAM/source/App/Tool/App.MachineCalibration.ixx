module;

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

export module Cam.App.MachineCalibration;

// ------------------------------------------------------------------
// Cam::App::MachineCalibration
//
// The model behind the MACHINE-calibration meta-program.  Where probe
// calibration solves for the stylus radius against a known reference, machine
// calibration does the inverse: with a CALIBRATED probe (known radius r and its
// 1-sigma uncertainty), it probes a mounted flat aluminum plane at a spread of
// tilt angles and lateral positions to locate the ROTARY (A) AXIS in Y and Z as
// precisely as possible -- with a stored confidence.
//
// The flow mirrors probe calibration:
//
//   1. LEVEL    -- probe the flat top once at A=0 to fix the reference height.
//   2. SAMPLE   -- tilt to a spread of angles (both signs) and probe a few
//                  lateral points across `linearBound` at each.
//   3. FIT      -- de-bias each contact by the known r, then least-squares the
//                  tilted-plane contact model for the axis depth D (=> axis Z)
//                  and the axis lateral position (=> axis Y), with 1-sigma
//                  confidences.  The radius uncertainty is propagated in.
//   4. REVIEW   -- show the inferred axis; commit it to the machine definition.
//
// The contact model on a flat plane tilted by phi about the axis at (axisY,axisZ),
// with the flat top at faceZ and D = faceZ - axisZ the axis-to-face depth:
//
//   z(phi,y) = axisZ + D/cos(phi) + tan(phi)*(y - axisY) + r*tan|phi|
//
// The last term is the CYLINDER stylus uphill-edge bias.  Knowing r, subtract it.
// Then, writing rise = z - r*tan|phi| - faceZ and Y' = rise - tan(phi)*y:
//
//   Y' = D*(1/cos(phi) - 1)  +  (-axisY)*tan(phi)
//
// a 2-parameter linear least squares.  Symmetric +/- angles make the (D, axisY)
// columns independent and well-conditioned.
// ------------------------------------------------------------------

export namespace Cam::App {

    struct MachineCalibration {

        enum class Phase { Idle, Leveling, Sampling, Fitting, Review, Failed };

        // -- Calibrated probe (input) ---------------------------------
        // Supplied from the loaded probe tool; r de-biases each contact and its
        // sigma is propagated into the axis confidence.
        double probeRadius      = 0.5;
        double probeRadiusSigma = 0.0;

        // -- Spec (user-configurable) ----------------------------------
        double linearBound  = 20.0;   // lateral extent to sample across, along Y (mm)
        double angleMin     = 5.0;    // smallest tilt magnitude (deg)
        double angleMax     = 30.0;   // largest tilt magnitude (deg)
        int    sampleAngles = 5;      // distinct tilt magnitudes
        int    samplePoints = 3;      // lateral locations per orientation

        // -- Runtime --------------------------------------------------
        Phase phase = Phase::Idle;

        struct Sample {
            double angleDeg = 0.0;    // SIGNED commanded tilt
            double lateral  = 0.0;    // Y offset from the lateral origin (mm)
            double z        = 0.0;    // measured surface height (machine Z, mm)
        };
        std::vector<Sample> samples;

        // -- Result ---------------------------------------------------
        bool   haveResult   = false;
        double resultAxisZ  = 0.0;    // machine Z of the axis (mm)
        double resultAxisY  = 0.0;    // axis Y relative to the lateral origin (mm)
        double resultAxisYAbs = 0.0;  // axis Y in absolute machine coords (mm); the
                                      // driver fills this (lateral origin + resultAxisY)
        double resultDepth  = 0.0;    // axis-to-face depth D (mm)
        double resultAxisZSigma = 0.0;
        double resultAxisYSigma = 0.0;
        double resultResidual   = 0.0;

        static constexpr double kPi = 3.14159265358979;

        // Tilt magnitudes: leveled 0, then `sampleAngles` magnitudes spread over
        // [angleMin, angleMax], each at + and - (both signs make the fit conditioned).
        std::vector<double> angleSchedule() const {
            std::vector<double> out;
            out.push_back(0.0);
            const int    n  = std::max(sampleAngles, 1);
            const double lo = std::clamp(angleMin, 1.0, 80.0);
            const double hi = std::clamp(std::max(angleMax, lo), lo, 80.0);
            for (int i = 0; i < n; i++) {
                const double t = (n == 1) ? hi : lo + (hi - lo) * (double(i) / (n - 1));
                out.push_back(+t);
                out.push_back(-t);
            }
            return out;
        }

        // Lateral offsets, ACROSS the rotary (X) axis -- i.e. along Y -- centred on
        // the lateral origin and spanning `linearBound`.  The driver adds each to
        // the live probe Y.
        std::vector<double> lateralSchedule() const {
            std::vector<double> out;
            const int    m    = std::max(samplePoints, 1);
            const double half = std::max(linearBound, 0.0) * 0.5;
            for (int i = 0; i < m; i++) {
                const double f = (m == 1) ? 0.0 : (double(i) / (m - 1)) * 2.0 - 1.0;  // -1..+1
                out.push_back(f * half);
            }
            return out;
        }

        // Expected height of a sample once the face is tilted -- so the driver
        // approaches just above the real expected location on a long plane instead
        // of plunging blindly.  Mirrors ProbeCalibration::expectedRise.
        static double expectedRise(double angleDeg, double lateral, double axisDepth) {
            const double rad = angleDeg * kPi / 180.0;
            return axisDepth * (1.0 / std::cos(rad) - 1.0) + std::tan(rad) * lateral;
        }

        int plannedContacts() const {
            return static_cast<int>(angleSchedule().size()) * std::max(samplePoints, 1);
        }

        void clampSpec() {
            linearBound  = std::max(linearBound, 0.0);
            angleMin     = std::clamp(angleMin, 1.0, 80.0);
            angleMax     = std::clamp(std::max(angleMax, angleMin), angleMin, 80.0);
            sampleAngles = std::clamp(sampleAngles, 1, 30);
            samplePoints = std::clamp(samplePoints, 1, 30);
        }

        void reset() {
            phase = Phase::Idle;
            samples.clear();
            haveResult = false;
            resultAxisZ = resultAxisY = resultDepth = 0.0;
            resultAxisZSigma = resultAxisYSigma = resultResidual = 0.0;
        }

        // Solve the 2-parameter model at a GIVEN stylus radius `r`.  Fills D, axisY,
        // axisZ and the RMS residual, and reports the normal-matrix terms (so the
        // caller can form the least-squares covariance).  Returns false if the data
        // is degenerate.
        bool solveAxis(double r, double faceZ,
                       double& D, double& axisY, double& axisZ, double& rms,
                       double& Saa, double& Sbb, double& det) const {
            Saa = 0; Sbb = 0; det = 0;
            double Sab = 0, SaY = 0, SbY = 0;
            int used = 0;
            for (const Sample& s : samples) {
                if (std::fabs(s.angleDeg) < 0.5) { continue; }   // flats anchor faceZ only
                const double rad  = s.angleDeg * kPi / 180.0;    // signed
                const double tphi = std::tan(rad);
                const double a    = 1.0 / std::cos(rad) - 1.0;
                const double b    = tphi;
                const double corrected = s.z - r * std::tan(std::fabs(rad));  // de-bias
                const double rise = corrected - faceZ;
                const double Yp   = rise - tphi * s.lateral;
                Saa += a*a; Sab += a*b; Sbb += b*b;
                SaY += a*Yp; SbY += b*Yp;
                used++;
            }
            if (used < 3) { return false; }
            det = Saa * Sbb - Sab * Sab;
            if (std::fabs(det) < 1e-12) { return false; }

            D            = ( Sbb * SaY - Sab * SbY) / det;   // p1
            const double p2 = (-Sab * SaY + Saa * SbY) / det;
            axisY        = -p2;
            axisZ        = faceZ - D;

            double sse = 0.0; int n = 0;
            for (const Sample& s : samples) {
                if (std::fabs(s.angleDeg) < 0.5) { continue; }
                const double rad  = s.angleDeg * kPi / 180.0;
                const double tphi = std::tan(rad);
                const double a    = 1.0 / std::cos(rad) - 1.0;
                const double corrected = s.z - r * std::tan(std::fabs(rad));
                const double rise = corrected - faceZ;
                const double Yp   = rise - tphi * s.lateral;
                const double pred = D * a + p2 * tphi;
                const double e = pred - Yp; sse += e * e; n++;
            }
            rms = std::sqrt(sse / std::max(n, 1));
            return true;
        }

        // Fit the rotary axis Y/Z from the collected samples, propagating the
        // calibrated radius uncertainty into the Z (depth) confidence.
        bool fit() {
            haveResult = false;
            if (samples.size() < 4) { return false; }

            double flatSum = 0.0; int flatN = 0;
            for (const Sample& s : samples) {
                if (std::fabs(s.angleDeg) < 0.5) { flatSum += s.z; flatN++; }
            }
            if (flatN == 0) { return false; }
            const double faceZ = flatSum / flatN;

            double D, axisY, axisZ, rms, Saa, Sbb, det;
            if (!solveAxis(probeRadius, faceZ, D, axisY, axisZ, rms, Saa, Sbb, det)) {
                return false;
            }

            // Least-squares 1-sigma: Cov = s^2 (A^T A)^-1; (2,2)->Saa/det is axisY,
            // (1,1)->Sbb/det is D (== axisZ up to faceZ noise).
            const double lsZ = rms * std::sqrt(Sbb / det);
            const double lsY = rms * std::sqrt(Saa / det);

            // Radius uncertainty propagates almost entirely into the symmetric
            // (depth/Z) direction -- re-solve at r+sigma and take the D shift.
            double dZr = 0.0, dYr = 0.0;
            if (probeRadiusSigma > 0.0) {
                double D2, aY2, aZ2, rms2, s1, s2, dt2;
                if (solveAxis(probeRadius + probeRadiusSigma, faceZ,
                              D2, aY2, aZ2, rms2, s1, s2, dt2)) {
                    dZr = std::fabs(aZ2 - axisZ);
                    dYr = std::fabs(aY2 - axisY);
                }
            }

            resultDepth      = D;
            resultAxisZ      = axisZ;
            resultAxisY      = axisY;
            resultResidual   = rms;
            resultAxisZSigma = std::sqrt(lsZ * lsZ + dZr * dZr);
            resultAxisYSigma = std::sqrt(lsY * lsY + dYr * dYr);
            haveResult       = true;
            return true;
        }

        static std::string phaseLabel(Phase p) {
            switch (p) {
                case Phase::Idle:     return "Ready";
                case Phase::Leveling: return "Leveling";
                case Phase::Sampling: return "Sampling";
                case Phase::Fitting:  return "Fitting";
                case Phase::Review:   return "Review";
                case Phase::Failed:   return "Failed";
            }
            return "";
        }
    };
}
