module;

#include <cstddef>
#include <vector>
#include <cmath>
#include <algorithm>

#include <nlohmann/json.hpp>

export module Cam.App.Probe;

import Rev.Core.Pos3;

// Module-internal alias (NOT exported) so we don't inject a second `Pos3` into
// Cam::App for importers that already have their own.
namespace Cam::App { using Pos3 = Rev::Core::Pos3; }

export namespace Cam::App {

    using Json = nlohmann::json;

    // ==================================================================
    // Probing data model  (the "define a probe from the GUI side" core)
    // ==================================================================
    //
    // A probe is a *sub-component* of a Stage (see Stage::probe): a break from
    // the normal machining path where the machine task-switches to the probe
    // tool, touches a set of part features, and from the measured vs. nominal
    // geometry fits a persistent frame correction that is applied to every
    // subsequent operation until the next probe runs.
    //
    // All target geometry is stored in the MODEL / CAD frame -- exactly the
    // frame toolpath points live in -- so the same UserFrame transform the
    // executor already applies to cut moves also applies to probe moves.

    // One probe target: a point on the part surface and the outward surface
    // normal there.  The machine starts `standoff` mm out along +normal and
    // drives in along -normal; it may travel up to `overtravel` mm past the
    // nominal point before the touch is declared a failure.
    struct ProbeTarget {

        Pos3   point{};            // nominal contact point (CAD frame)
        Pos3   normal{};           // outward unit surface normal (CAD frame)
        double standoff   = 5.0;   // begin this far out along +normal
        double overtravel = 5.0;   // max travel PAST nominal before "no contact" fail

        // -- Result (transient; filled in after a probing run) ----------
        bool   measured = false;
        Pos3   measuredPoint{};    // actual contact point (CAD frame)

        Json getState() const {
            Json j;
            j["point"]       = Json::array({ point.x,  point.y,  point.z  });
            j["normal"]      = Json::array({ normal.x, normal.y, normal.z });
            j["standoff"]    = standoff;
            j["overtravel"]  = overtravel;
            // Measured contact is a per-run result, not design intent, so it is
            // intentionally NOT serialized -- a loaded project starts un-probed.
            return j;
        }

        void setState(const Json& j) {
            readVec3(j, "point",  point);
            readVec3(j, "normal", normal);
            if (j.contains("standoff")   && j["standoff"].is_number())   { standoff   = j["standoff"].get<double>();   }
            if (j.contains("overtravel") && j["overtravel"].is_number()) { overtravel = j["overtravel"].get<double>(); }
            measured = false;
        }

        static void readVec3(const Json& j, const char* key, Pos3& out) {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 3) {
                out.x = j[key][0].get<float>();
                out.y = j[key][1].get<float>();
                out.z = j[key][2].get<float>();
            }
        }
    };

    // The fitted rigid result of a probing run: a frame correction applied as
    //   p_corrected = R * p_nominal + t
    // mapping nominal CAD coordinates onto the measured part pose.  Persists
    // (in the executor) until the next probe replaces it.  R is row-major 3x3.
    struct ProbeResult {

        bool   valid = false;
        double r[9]  = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
        Pos3   t{};
        double rmsError = 0.0;

        void reset() { *this = ProbeResult(); }

        // Apply the correction to a CAD-frame point.
        Pos3 apply(const Pos3& p) const {
            return {
                float(r[0] * p.x + r[1] * p.y + r[2] * p.z + t.x),
                float(r[3] * p.x + r[4] * p.y + r[5] * p.z + t.y),
                float(r[6] * p.x + r[7] * p.y + r[8] * p.z + t.z)
            };
        }

        // Apply the correction to a CAD-frame DIRECTION: rotation only, no
        // translation (directions don't translate).  This is what lets a probe
        // correction reorient the tool axis *before* the IK solve, so the rotary
        // axis swings to compensate as a natural consequence of the geometry
        // rather than an explicit "rotate the chuck" command.  Identity R (the
        // un-probed default) returns the direction unchanged.
        Pos3 applyDirection(const Pos3& d) const {
            return {
                float(r[0] * d.x + r[1] * d.y + r[2] * d.z),
                float(r[3] * d.x + r[4] * d.y + r[5] * d.z),
                float(r[6] * d.x + r[7] * d.y + r[8] * d.z)
            };
        }

        // Compose: returns (this o inner) -- apply `inner` first, then `this`:
        //   result.apply(p) == this.apply(inner.apply(p))
        //   R = this.R * inner.R ;  t = this.R * inner.t + this.t
        // Used to ACCUMULATE probe corrections: a re-probe of an already-corrected
        // part measures only the RESIDUAL (it presents nearly square), so the new
        // total = oldCorrection.composedWith(residual).  Replacing instead of
        // composing would discard the prior correction the moment the part
        // converges (residual -> identity) and rotate it back.
        ProbeResult composedWith(const ProbeResult& inner) const {
            ProbeResult out;
            for (int row = 0; row < 3; row++) {
                for (int col = 0; col < 3; col++) {
                    out.r[row * 3 + col] =
                        r[row * 3 + 0] * inner.r[0 * 3 + col] +
                        r[row * 3 + 1] * inner.r[1 * 3 + col] +
                        r[row * 3 + 2] * inner.r[2 * 3 + col];
                }
            }
            out.t = Pos3{
                float(r[0] * inner.t.x + r[1] * inner.t.y + r[2] * inner.t.z + t.x),
                float(r[3] * inner.t.x + r[4] * inner.t.y + r[5] * inner.t.z + t.y),
                float(r[6] * inner.t.x + r[7] * inner.t.y + r[8] * inner.t.z + t.z)
            };
            out.valid    = valid || inner.valid;
            out.rmsError = inner.rmsError;   // report the latest residual
            return out;
        }

        // Fit a frame correction (nominal CAD -> measured part pose) from probed
        // contacts.  nominal[i] / measured[i] are paired contact points in the
        // CAD frame; normal[i] is the outward surface normal the probe drove
        // along for that target.
        //
        //  * 1 point  -> translation along that point's normal only.  A single
        //    touch only learns displacement ALONG its approach (a Z-style
        //    touch-off); lateral position is unobserved, so we project the delta
        //    onto the normal and translate by that.  R stays identity.
        //  * >= 3     -> fit the part's tilt + a uniform offset, but CONSTRAINED
        //    to what the machine can actually do: a rotation about its rotary
        //    axis `rotaryAxis` plus a uniform offset along the surface normal.
        //    Any tilt the rotary axis cannot reach is intentionally left as
        //    residual -- no real part is perfectly flat or perfectly mounted, so
        //    we correct what we can and accept the rest (it shows up in rmsError).
        //    If `rotaryAxis` is zero (3-axis machine, no rotary), the rotation is
        //    skipped and only the uniform offset is applied.
        //  * == 2     -> centroid translation only (under-determined for tilt).
        static ProbeResult fit(
            const std::vector<Pos3>& nominal,
            const std::vector<Pos3>& measured,
            const std::vector<Pos3>& normal,
            const Pos3&              rotaryAxis = Pos3{})
        {
            ProbeResult out;   // identity by default
            const size_t n = std::min(nominal.size(), measured.size());
            if (n == 0) { return out; }

            if (n == 1) {
                const Pos3 delta = measured[0] - nominal[0];
                Pos3 nrm = (!normal.empty()) ? normal[0] : Pos3{ 0, 0, 1 };
                const float len = nrm.pythag();
                if (len > 1e-6f) { nrm = nrm * (1.0f / len); }
                const float along = delta.dot(nrm);
                out.t        = nrm * along;
                out.valid    = true;
                out.rmsError = 0.0;
                return out;
            }

            // Centroids (shared by both the rigid and the translation paths).
            Pos3 cN{}, cM{};
            for (size_t i = 0; i < n; i++) { cN = cN + nominal[i]; cM = cM + measured[i]; }
            const float inv = 1.0f / static_cast<float>(n);
            cN = cN * inv;
            cM = cM * inv;

            // n >= 3: RIGID tilt fit.  Fit a plane to the measured contacts and
            // rotate the nominal surface normal onto the measured one -- this is
            // the part's tilt.  A probed face determines tilt + offset (not the
            // in-plane spin, which a flat touch-off cannot see and a top-face
            // probe does not need).  Full 6-DOF Kabsch is a later refinement.
            if (n >= 3) {

                // Nominal surface normal: average of the targets' normals.
                Pos3 n0{};
                for (size_t i = 0; i < n && i < normal.size(); i++) { n0 = n0 + normal[i]; }
                float n0len = n0.pythag();
                n0 = (n0len > 1e-6f) ? n0 * (1.0f / n0len) : Pos3{ 0, 0, 1 };

                // Orthonormal basis (u, w) spanning the plane perpendicular to n0.
                const Pos3 ref = (std::fabs(n0.z) < 0.9f) ? Pos3{ 0, 0, 1 } : Pos3{ 1, 0, 0 };
                Pos3 u = n0.cross(ref);
                const float ulen = u.pythag();

                if (ulen > 1e-6f) {
                    u = u * (1.0f / ulen);
                    Pos3 w = n0.cross(u);   // unit, since n0 and u are orthonormal

                    // Least squares: measured height along n0 as a linear function
                    // of the in-plane (u, w) coords, about the centroid:
                    //   qn = a*qu + b*qw   (no constant: centered).
                    double Suu = 0, Suw = 0, Sww = 0, Sun = 0, Swn = 0;
                    for (size_t i = 0; i < n; i++) {
                        const Pos3 d = measured[i] - cM;
                        const double qu = d.dot(u), qw = d.dot(w), qn = d.dot(n0);
                        Suu += qu * qu; Suw += qu * qw; Sww += qw * qw;
                        Sun += qu * qn; Swn += qw * qn;
                    }
                    const double det = Suu * Sww - Suw * Suw;

                    if (std::fabs(det) > 1e-9) {
                        const double a = ( Sww * Sun - Suw * Swn) / det;
                        const double b = (-Suw * Sun + Suu * Swn) / det;

                        // Measured normal in CAD: the plane qn = a*qu + b*qw has
                        // normal (-a, -b, 1) in the (u, w, n0) basis.
                        Pos3 n1 = u * float(-a) + w * float(-b) + n0;
                        const float n1len = n1.pythag();
                        n1 = (n1len > 1e-6f) ? n1 * (1.0f / n1len) : n0;

                        // CONSTRAIN the rotation to the machine's rotary axis: the
                        // angle ABOUT `rotaryAxis` that best swings the nominal
                        // surface normal n0 toward the measured n1.  Tilt the axis
                        // can't reach is left as residual.  (Rotation about A
                        // preserves dot(D, A), so an A-only correction keeps every
                        // toolpath direction IK-reachable -- which is exactly why
                        // an unconstrained 3D rotation made all points invalid.)
                        double R[9] = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };

                        Pos3 A = rotaryAxis;
                        const float Alen = A.pythag();
                        if (Alen > 1e-6f) {
                            A = A * (1.0f / Alen);

                            // In-plane (perpendicular to A) parts of n0 and n1.
                            Pos3 n0p = n0 - A * n0.dot(A);
                            Pos3 n1p = n1 - A * n1.dot(A);
                            const float l0 = n0p.pythag();
                            const float l1 = n1p.pythag();

                            if (l0 > 1e-5f && l1 > 1e-5f) {
                                n0p = n0p * (1.0f / l0);
                                n1p = n1p * (1.0f / l1);
                                const double cs = double(n0p.dot(n1p));
                                const double sn = double(A.dot(n0p.cross(n1p)));
                                const double th = std::atan2(sn, cs);   // about A: n0 -> n1

                                const double ca = std::cos(th), sa = std::sin(th), ta = 1.0 - ca;
                                const double ax = A.x, ay = A.y, az = A.z;
                                R[0] = ta*ax*ax + ca;    R[1] = ta*ax*ay - sa*az; R[2] = ta*ax*az + sa*ay;
                                R[3] = ta*ax*ay + sa*az; R[4] = ta*ay*ay + ca;    R[5] = ta*ay*az - sa*ax;
                                R[6] = ta*ax*az - sa*ay; R[7] = ta*ay*az + sa*ax; R[8] = ta*az*az + ca;
                            }
                        }

                        for (int i = 0; i < 9; i++) { out.r[i] = R[i]; }

                        // Uniform offset ALONG the surface normal only (a single
                        // touch-off shift).  In-plane translation is not something
                        // a rotary axis + a normal touch-off can fix, so it is not
                        // applied.
                        const Pos3 RcN{
                            float(R[0]*cN.x + R[1]*cN.y + R[2]*cN.z),
                            float(R[3]*cN.x + R[4]*cN.y + R[5]*cN.z),
                            float(R[6]*cN.x + R[7]*cN.y + R[8]*cN.z)
                        };
                        const Pos3 tFull = cM - RcN;
                        out.t     = n0 * tFull.dot(n0);
                        out.valid = true;

                        double sse = 0.0;
                        for (size_t i = 0; i < n; i++) {
                            const Pos3 e = measured[i] - out.apply(nominal[i]);
                            sse += double(e.x)*e.x + double(e.y)*e.y + double(e.z)*e.z;
                        }
                        out.rmsError = std::sqrt(sse / double(n));
                        return out;
                    }
                }
                // Degenerate (collinear points / bad basis): fall through to the
                // translation-only fit below.
            }

            // n == 2, or a degenerate n >= 3: translation only (centroid delta).
            out.t     = cM - cN;
            out.valid = true;

            double sse = 0.0;
            for (size_t i = 0; i < n; i++) {
                const Pos3 e = (measured[i] - nominal[i]) - out.t;
                sse += double(e.x) * e.x + double(e.y) * e.y + double(e.z) * e.z;
            }
            out.rmsError = std::sqrt(sse / double(n));
            return out;
        }

        Json getState() const {
            Json j;
            j["valid"] = valid;
            j["r"]     = Json::array({ r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8] });
            j["t"]     = Json::array({ t.x, t.y, t.z });
            j["rmsError"] = rmsError;
            return j;
        }

        void setState(const Json& j) {
            if (j.contains("valid") && j["valid"].is_boolean()) { valid = j["valid"].get<bool>(); }
            if (j.contains("r") && j["r"].is_array() && j["r"].size() >= 9) {
                for (int i = 0; i < 9; i++) { r[i] = j["r"][i].get<double>(); }
            }
            if (j.contains("t") && j["t"].is_array() && j["t"].size() >= 3) {
                t.x = j["t"][0].get<float>();
                t.y = j["t"][1].get<float>();
                t.z = j["t"][2].get<float>();
            }
            if (j.contains("rmsError") && j["rmsError"].is_number()) { rmsError = j["rmsError"].get<double>(); }
        }
    };

    // The probe sub-component attached to a Stage.  `enabled` marks the stage as
    // carrying a probe step (a break in the machining path before this stage's
    // own cut runs); `targets` are the features to touch; `result` is the most
    // recent fitted correction.
    struct ProbeSpec {

        bool                     enabled = false;
        std::vector<ProbeTarget> targets;
        ProbeResult              result;

        bool   ready()  const { return enabled && targets.size() >= 3; }   // 3+ pts -> full frame
        size_t count()  const { return targets.size(); }

        void clearResult() { result.reset(); for (ProbeTarget& t : targets) { t.measured = false; } }

        Json getState() const {
            Json j;
            j["enabled"] = enabled;
            j["targets"] = Json::array();
            for (const ProbeTarget& t : targets) { j["targets"].push_back(t.getState()); }
            j["result"]  = result.getState();
            return j;
        }

        void setState(const Json& j) {
            targets.clear();
            result.reset();
            if (!j.is_object()) { return; }
            if (j.contains("enabled") && j["enabled"].is_boolean()) { enabled = j["enabled"].get<bool>(); }
            if (j.contains("targets") && j["targets"].is_array()) {
                for (const Json& tj : j["targets"]) {
                    ProbeTarget t;
                    t.setState(tj);
                    targets.push_back(t);
                }
            }
            if (j.contains("result") && j["result"].is_object()) { result.setState(j["result"]); }
        }
    };
}
