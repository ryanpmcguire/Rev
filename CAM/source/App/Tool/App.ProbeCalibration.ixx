module;

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

export module Cam.App.ProbeCalibration;

import Rev.Core.Pos3;

// ------------------------------------------------------------------
// Cam::App::ProbeCalibration
//
// The model behind the probe-calibration meta-program: configuration (the spec),
// the derived probing PLAN (which tilt angles and lateral points to sample), the
// collected samples, the fitted result, and the phase the routine is in.
//
// It is deliberately machine-agnostic.  The window owns the spec + UI; a driver
// (the world view / Air layer) is injected to actually move the machine and feed
// contacts back.  The flow:
//
//   1. LEVEL    -- probe the (rectangular) top face flat to get a true A=0 zero.
//   2. SAMPLE   -- tilt to a spread of angles (angleMin..angleMax, both signs)
//                  and probe `samplePoints` locations across `linearBound` at each.
//   3. FIT      -- solve the contact model for the stylus radius (and residual).
//   4. REVIEW   -- show the inferred geometry; save it to the tool, or redo.
// ------------------------------------------------------------------

export namespace Cam::App {

    struct ProbeCalibration {

        // The reference the operator mounted.  Both are assumed to be a rectangular
        // prism for now (a gauge block IS one; "cylinder" reserved for later).
        enum class Artifact { GaugeBlock, Cylinder };

        enum class Phase { Idle, Leveling, Sampling, Fitting, Review, Failed };

        // -- Spec (user-configurable) ----------------------------------
        Artifact artifact     = Artifact::GaugeBlock;
        double   artifactSize = 10.0;   // gauge width / cylinder diameter (mm)
        double   linearBound  = 20.0;   // lateral extent to sample across (mm)
        double   angleMin     = 5.0;    // smallest tilt magnitude (deg)
        double   angleMax      = 30.0;  // largest tilt magnitude (deg)
        int      sampleAngles = 5;      // distinct tilt magnitudes
        int      samplePoints = 3;      // lateral locations per orientation

        // -- Runtime --------------------------------------------------
        Phase phase = Phase::Idle;

        // A single recorded contact: the commanded tilt, the lateral offset along
        // the face, and the measured surface height there (machine Z).
        struct Sample {
            double angleDeg = 0.0;
            double lateral  = 0.0;
            double z        = 0.0;
        };
        std::vector<Sample> samples;

        // -- Result ---------------------------------------------------
        bool   haveResult     = false;
        double resultRadius   = 0.0;   // inferred stylus edge radius (mm)
        double resultResidual = 0.0;   // fit residual (mm)
        double resultRadiusSigma = 0.0;  // 1-sigma uncertainty on the radius (mm)

        // The tilt magnitudes to visit: the leveled 0, then `sampleAngles`
        // magnitudes spread linearly over [angleMin, angleMax], each at + and -
        // (the symmetric/antisymmetric split separates axis depth from radius).
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

        // Lateral offsets to sample at, ACROSS the rotary (X) axis -- i.e. along
        // Y -- centred on the current probe Y and spanning `linearBound` (so a
        // 50 mm linear bound samples currentY-25 .. currentY+25).  The driver adds
        // each offset to the live probe Y.
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

        // The expected height of a sample point above the leveled-flat reference
        // once the face is tilted by `angleDeg` about the rotary (X) axis: a point
        // `lateral` mm out along Y rides up by tan(angle)*lateral, and the whole
        // face lifts by D*(1/cos-1) for axis-to-face depth `axisDepth`.  The driver
        // positions each probe's STANDOFF just above this -- so on a long block,
        // where far points sit far above flat, it approaches the real expected
        // location instead of blindly plunging to a fixed Z.
        static double expectedRise(double angleDeg, double lateral, double axisDepth) {
            const double rad = angleDeg * kPi / 180.0;
            return axisDepth * (1.0 / std::cos(rad) - 1.0) + std::tan(rad) * lateral;
        }

        // How many probe contacts the configured plan will collect.
        int plannedContacts() const {
            return static_cast<int>(angleSchedule().size()) * std::max(samplePoints, 1);
        }

        void clampSpec() {
            artifactSize = std::max(artifactSize, 0.0);
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
            resultRadius = resultResidual = resultRadiusSigma = 0.0;
        }

        static constexpr double kPi = 3.14159265358979;

        // Fit the stylus radius from the collected samples.  On a leveled flat
        // face the SYMMETRIC rise at tilt magnitude phi (mean Z over +/-phi and all
        // lateral points, minus the leveled mean) follows the cylinder contact
        // model:  rise(phi) = D*(1/cos phi - 1) + r*tan(phi).  Two unknowns (the
        // axis-to-face distance D and the stylus radius r), so >= 2 distinct angles
        // separate them by least squares.  r is the calibration result.
        bool fit() {
            haveResult = false;
            if (samples.size() < 4) { return false; }

            double flatSum = 0.0; int flatN = 0;
            for (const Sample& s : samples) {
                if (std::fabs(s.angleDeg) < 0.5) { flatSum += s.z; flatN++; }
            }
            if (flatN == 0) { return false; }
            const double flatZ = flatSum / flatN;

            struct Acc { double mag; double sum; int n; };
            std::vector<Acc> accs;
            for (const Sample& s : samples) {
                const double m = std::fabs(s.angleDeg);
                if (m < 0.5) { continue; }
                Acc* a = nullptr;
                for (Acc& q : accs) { if (std::fabs(q.mag - m) < 0.5) { a = &q; break; } }
                if (!a) { accs.push_back({ m, 0.0, 0 }); a = &accs.back(); }
                a->sum += s.z; a->n++;
            }
            if (accs.size() < 2) { return false; }   // need >= 2 angles for D + r

            double Saa = 0, Sab = 0, Sbb = 0, Say = 0, Sby = 0;
            for (const Acc& q : accs) {
                const double sym = q.sum / q.n - flatZ;
                const double rad = q.mag * kPi / 180.0;
                const double a = 1.0 / std::cos(rad) - 1.0;   // depth term
                const double b = std::tan(rad);               // radius term
                Saa += a*a; Sab += a*b; Sbb += b*b;
                Say += a*sym; Sby += b*sym;
            }
            const double det = Saa * Sbb - Sab * Sab;
            if (std::fabs(det) < 1e-12) { return false; }

            const double r = (-Sab * Say + Saa * Sby) / det;   // stylus radius

            double sse = 0.0;
            for (const Acc& q : accs) {
                const double sym = q.sum / q.n - flatZ;
                const double rad = q.mag * kPi / 180.0;
                const double D   = (Sbb * Say - Sab * Sby) / det;
                const double pred = D * (1.0 / std::cos(rad) - 1.0) + r * std::tan(rad);
                const double e = pred - sym; sse += e * e;
            }

            resultRadius   = r;
            resultResidual = std::sqrt(sse / static_cast<double>(accs.size()));
            // 1-sigma on r from the least-squares covariance: Cov = s^2 (A^T A)^-1,
            // and the (r,r) entry of (A^T A)^-1 is Saa/det.  With only a couple of
            // angles this is optimistic (few residual DOF), so it tightens as the
            // operator runs more sample angles -- exactly the intended behavior.
            resultRadiusSigma = resultResidual * std::sqrt(Saa / det);
            haveResult     = true;
            return true;
        }

        // -- Labels / (de)serialization of enums ----------------------

        static std::string artifactToString(Artifact a) {
            return a == Artifact::Cylinder ? "cylinder" : "gauge";
        }
        static Artifact artifactFromString(const std::string& s) {
            return s == "cylinder" ? Artifact::Cylinder : Artifact::GaugeBlock;
        }
        static std::string phaseLabel(Phase p) {
            switch (p) {
                case Phase::Idle:     return "Ready";
                case Phase::Leveling: return "Levelling surface...";
                case Phase::Sampling: return "Sampling tilted plane...";
                case Phase::Fitting:  return "Fitting...";
                case Phase::Review:   return "Review";
                case Phase::Failed:   return "Failed";
            }
            return "Ready";
        }
    };
}
