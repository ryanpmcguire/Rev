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

    // One probe CONTACT, recorded in the canonical form the pose solver needs:
    // the nominal target + its outward normal (CAD frame), the actual contact
    // (CAD/world frame, as back-transformed from the [PRB] reply), and the
    // commanded rotary angle the part was at when touched.  These accumulate in
    // the Project (cleared on "Set Origin") and the part pose is RE-SOLVED over
    // the whole set -- so compounding (more probes, more orientations) emerges
    // naturally instead of being a replace-vs-compose special case.
    struct ProbeMeasurement {
        double angleDeg = 0.0;   // commanded rotary (A) angle at the contact
        Pos3   nominal{};        // nominal target point (CAD frame)
        Pos3   normal{};         // outward unit surface normal (CAD frame)
        Pos3   measured{};       // actual contact (CAD/world frame)
        Pos3   machineContact{}; // raw [PRB] contact (ABSOLUTE machine coords) --
                                 // origin-independent, used for rotary-axis fitting
    };

    // The WORK frame's defining feature: the machine's literal rotary (A) axis,
    // expressed as a LINE (direction + a point on it) in the part/CAD frame.
    //
    // This is the long-missing distinction made structural.  There are THREE
    // coordinate systems, not one:
    //   * MACHINE frame  -- global controller coordinates; the app emits in these.
    //                       "Set Origin" anchors where the part frame STARTED in
    //                       machine space (tool tip == ~0.1mm above stock centre).
    //   * WORK frame     -- the physical rotary axis the chuck turns about.  Fixed
    //                       in machine space but its exact location is initially
    //                       ASSUMED (from the machine definition) and refinable by
    //                       probing across orientations.  `measured` flips true
    //                       once we have inferred it rather than assumed it.
    //   * PART frame     -- the stock coordinate system (co/ax), attached to the
    //                       physical part: WORK (rotary axis) o A-rotation o the
    //                       part's mount offset.  The mount offset (part vs. its
    //                       own axis) is what a probe-and-rotate sequence measures.
    //
    // Probing infers the offsets BETWEEN these: the part's offset from the rotary
    // axis (part<->work), and eventually the rotary axis's offset from the assumed
    // machine location (work<->machine).
    struct RotaryAxis {
        Pos3 direction{ 1, 0, 0 };   // unit A-axis direction
        Pos3 point{};                // a point the axis line passes through
        bool measured = false;       // false = assumed (machine def); true = probed
        double residual = 0.0;       // how well the 2-orientation fit closed (mm)

        void reset() { *this = RotaryAxis(); }

        // Locate the rotary axis from a face measured at several known rotation
        // angles.  At each orientation plane-fitting gives a centroid c_i and
        // normal n_i.  Rotation about the axis preserves a plane's distance, so
        // each rotated plane relates to a reference plane by the PLANE-DIFFERENCE
        // constraint  (n_i - n_0) . a = h_i - h_0,  h_i = n_i . c_i.  The axis
        // direction is KNOWN (the A axis); the position a is the least-squares
        // solution of those constraints in the plane perpendicular to dir.
        //
        // This deliberately uses ONLY the plane (normal + offset), NOT point
        // correspondence -- which matters because a flat face probed along fixed
        // rays does NOT trace a point about the axis, so any centroid-tracking
        // method would put the axis at the FACE depth, not the true depth.
        //
        // >= 3 orientations are required (>= 2 constraints) to pin the position;
        // at exactly 3 the fit is exactly determined (residual ~0, correct if the
        // data is clean), and >= 4 over-determines it so the residual can validate.
        static RotaryAxis inferFromNormalPlanes(
            const std::vector<Pos3>& centroids,
            const std::vector<Pos3>& normals,
            const Pos3&              dirIn)
        {
            RotaryAxis out;
            const size_t n = std::min(centroids.size(), normals.size());
            if (n < 3) { return out; }   // need >= 2 constraints to pin the position
            const float dl = dirIn.pythag();
            if (dl < 1e-6f) { return out; }
            const Pos3 d = dirIn * (1.0f / dl);

            // Basis (u, w) of the plane perpendicular to d.  a's along-d component
            // is meaningless for a line, so solve only its (u, w) coordinates.
            const Pos3 ref = (std::fabs(d.z) < 0.9f) ? Pos3{ 0, 0, 1 } : Pos3{ 1, 0, 0 };
            Pos3 u = d.cross(ref);
            const float ul = u.pythag();
            if (ul < 1e-6f) { return out; }
            u = u * (1.0f / ul);
            const Pos3 w = d.cross(u);

            // PLANE-DIFFERENCE constraint (uses ONLY each plane's normal + offset --
            // NO point correspondence, so it works on a featureless flat face
            // probed along fixed rays).  Rotation about the axis preserves a plane's
            // distance, so a plane at orientation i relates to the reference 0 by:
            //     (n_i - n_0) . a = h_i - h_0 ,   h_i = n_i . c_i
            // Each non-reference orientation gives one such constraint (its normal,
            // n_i - n_0, lies in the plane perpendicular to d).  Least-squares for a
            // in (u, w).  The earlier "axis lies in the plane through the centroid"
            // form fails here: with fixed-ray centroids it is forced to put the axis
            // at the FACE depth, not the true axis depth.
            const Pos3   n0 = normals[0];
            const double h0 = double(n0.dot(centroids[0]));
            double Suu = 0, Suw = 0, Sww = 0, Su = 0, Sw = 0;
            size_t used = 0;
            for (size_t i = 1; i < n; i++) {
                const Pos3   g  = normals[i] - n0;        // perpendicular to d
                const double hi = double(normals[i].dot(centroids[i]));
                const double rhs = hi - h0;
                const double gu = double(g.dot(u)), gw = double(g.dot(w));
                Suu += gu*gu; Suw += gu*gw; Sww += gw*gw;
                Su  += rhs*gu; Sw  += rhs*gw;
                used++;
            }
            if (used < 2) { return out; }   // need >= 3 orientations (>= 2 constraints)
            const double det = Suu * Sww - Suw * Suw;
            if (std::fabs(det) < 1e-9) { return out; }   // orientations too close -> undefined

            const double au = ( Sww * Su - Suw * Sw) / det;
            const double aw = (-Suw * Su + Suu * Sw) / det;
            const Pos3   a = u * float(au) + w * float(aw)
                           + d * float(centroids[0].dot(d));

            // Residual: how well the constraints concur (meaningful only when
            // OVER-determined -- i.e. >= 4 orientations; exactly determined at 3).
            double sse = 0.0;
            for (size_t i = 1; i < n; i++) {
                const Pos3   g = normals[i] - n0;
                const double hi = double(normals[i].dot(centroids[i]));
                const double e = double(g.dot(a)) - (hi - h0);
                sse += e * e;
            }
            out.residual  = std::sqrt(sse / double(used));
            out.direction = d;
            out.point     = a;
            out.measured  = true;
            return out;
        }
    };

    // The fitted rigid result of a probing run: a frame correction applied as
    //   p_corrected = R * p_nominal + t
    // mapping nominal CAD coordinates onto the measured part pose.  Persists
    // (in the executor) until the next probe replaces it.  R is row-major 3x3.
    struct ProbeResult {

        bool   valid = false;

        // The ACHIEVABLE correction: what the MACHINE is driven with -- a rotation
        // about its single rotary axis plus translation, the part of the true pose
        // it can physically make.  apply()/applyDirection() use this, so the cut
        // and probe-tool pipeline naturally only ever command achievable motion.
        double r[9]  = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
        Pos3   t{};

        // The TRUE rigid fit of the model to the measured points (full 3-DOF
        // rotation).  This is the honest measured pose -- the part's actual
        // orientation, including any tilt the single rotary axis CANNOT correct.
        // Kept for the record + the view; never sent to the machine.
        double rTrue[9] = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
        Pos3   tTrue{};

        double rmsError = 0.0;

        void reset() { *this = ProbeResult(); }

        // Apply the ACHIEVABLE correction to a CAD-frame point (driven to machine).
        Pos3 apply(const Pos3& p) const {
            return {
                float(r[0] * p.x + r[1] * p.y + r[2] * p.z + t.x),
                float(r[3] * p.x + r[4] * p.y + r[5] * p.z + t.y),
                float(r[6] * p.x + r[7] * p.y + r[8] * p.z + t.z)
            };
        }

        // Inverse of the ACHIEVABLE correction: q -> R^T (q - t).  Maps a point
        // from the corrected (driven) frame back into the nominal frame.  Used to
        // peel the work-frame correction off a measured contact so what remains is
        // the part's offset WITHIN the work frame.
        Pos3 applyInverse(const Pos3& q) const {
            const Pos3 d = q - t;
            return {
                float(r[0] * d.x + r[3] * d.y + r[6] * d.z),
                float(r[1] * d.x + r[4] * d.y + r[7] * d.z),
                float(r[2] * d.x + r[5] * d.y + r[8] * d.z)
            };
        }

        // Apply the ACHIEVABLE correction to a CAD-frame DIRECTION: rotation only.
        // This reorients tool axes before the IK solve so the rotary axis swings
        // to compensate as a consequence of the geometry.  Identity by default.
        Pos3 applyDirection(const Pos3& d) const {
            return {
                float(r[0] * d.x + r[1] * d.y + r[2] * d.z),
                float(r[3] * d.x + r[4] * d.y + r[5] * d.z),
                float(r[6] * d.x + r[7] * d.y + r[8] * d.z)
            };
        }

        // Apply the TRUE rigid fit (for the view -- shows the real measured pose,
        // including off-rotary-axis tilt the machine can't make).
        Pos3 applyTrue(const Pos3& p) const {
            return {
                float(rTrue[0] * p.x + rTrue[1] * p.y + rTrue[2] * p.z + tTrue.x),
                float(rTrue[3] * p.x + rTrue[4] * p.y + rTrue[5] * p.z + tTrue.y),
                float(rTrue[6] * p.x + rTrue[7] * p.y + rTrue[8] * p.z + tTrue.z)
            };
        }

        // Tilt magnitude (degrees) of the DRIVEN / TRUE rotation, from the trace:
        // angle = acos((trace - 1) / 2).
        double drivenTiltDeg() const {
            const double tr = r[0] + r[4] + r[8];
            return std::acos(std::clamp((tr - 1.0) * 0.5, -1.0, 1.0)) * 57.29577951308232;
        }
        double trueTiltDeg() const {
            const double tr = rTrue[0] + rTrue[4] + rTrue[8];
            return std::acos(std::clamp((tr - 1.0) * 0.5, -1.0, 1.0)) * 57.29577951308232;
        }

        // Is this correction trustworthy enough to physically DRIVE a re-probe by
        // it?  A driven re-probe rotates the part by this correction so the
        // confirmation probe can ask "am I where I think I am now?".  RMS is the
        // real trust signal -- a tight fit is trustworthy regardless of how large
        // the misalignment is, and a large-but-confident misalignment is exactly
        // what we want to physically correct.  maxTiltDeg is only a sanity / anti-
        // collision bound (reject degenerate fits, don't swing the probe an absurd
        // amount in one shot); it must not block an ordinary confident mis-mount.
        bool trustedForReprobe(double maxRmsMm, double maxTiltDeg) const {
            return valid
                && rmsError      <= maxRmsMm
                && drivenTiltDeg() <= maxTiltDeg
                && trueTiltDeg()   <= maxTiltDeg;
        }

        // Compose: the correction equivalent to applying `base` first, then
        // `*this`.  Rigid composition of BOTH the achievable (driven) and the
        // true (record) parts:  R = R_this * R_base,  t = R_this * t_base + t_this.
        // Iterative re-probe uses this: a driven pass measures the RESIDUAL
        // relative to the already-applied base, and composing refines/converges
        // the total correction (rmsError carries the latest residual's quality).
        ProbeResult composedOnto(const ProbeResult& base) const {
            ProbeResult o;
            auto mul = [](const double* A, const double* B, double* O) {
                for (int row = 0; row < 3; row++) {
                    for (int col = 0; col < 3; col++) {
                        O[row * 3 + col] = A[row * 3 + 0] * B[0 * 3 + col]
                                         + A[row * 3 + 1] * B[1 * 3 + col]
                                         + A[row * 3 + 2] * B[2 * 3 + col];
                    }
                }
            };
            auto xform = [](const double* M, const Pos3& p) -> Pos3 {
                return Pos3{
                    float(M[0] * p.x + M[1] * p.y + M[2] * p.z),
                    float(M[3] * p.x + M[4] * p.y + M[5] * p.z),
                    float(M[6] * p.x + M[7] * p.y + M[8] * p.z)
                };
            };
            mul(r,     base.r,     o.r);
            mul(rTrue, base.rTrue, o.rTrue);
            o.t        = xform(r,     base.t)     + t;
            o.tTrue    = xform(rTrue, base.tTrue) + tTrue;
            o.valid    = valid && base.valid;
            o.rmsError = rmsError;   // quality of the latest (residual) fit
            return o;
        }

        // A correction that is a PURE rotation of angleDeg about the line through
        // `point` along `dir`.  Used to command a deliberate calibration tilt:
        // driving the probe by this rotates the part by angleDeg about the rotary
        // axis, so the same face is measured at a known, well-separated angle.
        static ProbeResult pureRotation(const Pos3& dir, const Pos3& point, double angleDeg) {
            ProbeResult o;
            o.valid = true;
            const float dl = dir.pythag();
            if (dl < 1e-6f || std::fabs(angleDeg) < 1e-9) { return o; }   // identity
            const Pos3   d  = dir * (1.0f / dl);
            const double th = angleDeg * 0.017453292519943295;
            const double c = std::cos(th), s = std::sin(th), t = 1.0 - c;
            const double ax = d.x, ay = d.y, az = d.z;
            const double R[9] = {
                t*ax*ax + c,    t*ax*ay - s*az, t*ax*az + s*ay,
                t*ax*ay + s*az, t*ay*ay + c,    t*ay*az - s*ax,
                t*ax*az - s*ay, t*ay*az + s*ax, t*az*az + c
            };
            for (int i = 0; i < 9; i++) { o.r[i] = R[i]; o.rTrue[i] = R[i]; }
            const Pos3 Rp{
                float(R[0]*point.x + R[1]*point.y + R[2]*point.z),
                float(R[3]*point.x + R[4]*point.y + R[5]*point.z),
                float(R[6]*point.x + R[7]*point.y + R[8]*point.z)
            };
            o.t = point - Rp;   // pivot about `point`: apply(p) = R(p-point)+point
            o.tTrue = o.t;
            return o;
        }

        // Rotate point p by angleDeg about the LINE through axisPoint along
        // axisDir (Rodrigues).  Used to canonicalize a contact taken at a
        // commanded rotary angle back into the A=0 frame.
        static Pos3 rotateAboutLine(
            const Pos3& p, const Pos3& axisDir, const Pos3& axisPoint, double angleDeg)
        {
            const float len = axisDir.pythag();
            if (len < 1e-6f || std::fabs(angleDeg) < 1e-9) { return p; }
            const Pos3   a  = axisDir * (1.0f / len);
            const double th = angleDeg * 0.017453292519943295;   // deg -> rad
            const double c  = std::cos(th), s = std::sin(th);
            const Pos3   d  = p - axisPoint;
            const Pos3   axd  = a.cross(d);
            const double adot = double(a.dot(d));
            const Pos3   dr =
                d * float(c) + axd * float(s) + a * float(adot * (1.0 - c));
            return axisPoint + dr;
        }

        // Solve the single part pose S over an ENTIRE accumulated measurement
        // set (the Stage-1 source of truth).  Each contact is canonicalized back
        // to the A=0 frame by undoing its commanded rotation about the (assumed)
        // rotary axis line; the pooled nominal/measured/normal then go through
        // the same rigid fit() one orientation uses.  Pooling everything is what
        // makes compounding emergent: more probes -- and more orientations --
        // simply add points and refine S, with no replace-vs-compose branch.
        //
        // The axis is held at its prior here (Stage 1 estimates only S).  Stage 2+
        // will let the same set inform the axis (direction, then the full line),
        // at which point this canonicalization becomes self-consistent rather than
        // prior-dependent.
        static ProbeResult solve(
            const std::vector<ProbeMeasurement>& ms,
            const Pos3&                          rotaryAxis,
            const Pos3&                          rotaryPoint)
        {
            std::vector<Pos3> nominal, measured, normal;
            nominal.reserve(ms.size());
            measured.reserve(ms.size());
            normal.reserve(ms.size());
            for (const ProbeMeasurement& m : ms) {
                nominal.push_back(m.nominal);
                normal.push_back(m.normal);
                measured.push_back(
                    rotateAboutLine(m.measured, rotaryAxis, rotaryPoint, -m.angleDeg));
            }
            return fit(nominal, measured, normal, rotaryAxis, rotaryPoint);
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
        //  * >= 3     -> FIT THE MODEL TO THE FACE (a rigid registration), and
        //    record BOTH:
        //      - rTrue/tTrue: the TRUE rigid fit -- the minimum-arc rotation that
        //        maps the model's face normal onto the measured one, about the
        //        axis the data dictates (NOT a coordinate axis), translated by
        //        t = cM - R*cN (map the probed centroid onto the measured one).
        //        Because the probe touches at nominal X/Y, cM shares cN's X/Y, so
        //        this introduces NO lateral shift -- only height + tilt, which is
        //        all we measured.
        //      - r/t: the DRIVEN fit -- the same alignment but using only the one
        //        rotation the machine has (`rotaryAxis`).  apply() uses this, so
        //        the cut commands only achievable motion; the off-axis remainder
        //        is accepted residual (rmsError reflects the true-fit quality).
        //    rotaryAxis zero (3-axis, no rotary) -> driven rotation is identity.
        //  * == 2     -> uniform offset along the average normal (no lateral).
        static ProbeResult fit(
            const std::vector<Pos3>& nominal,
            const std::vector<Pos3>& measured,
            const std::vector<Pos3>& normal,
            const Pos3&              rotaryAxis  = Pos3{},
            const Pos3&              rotaryPoint = Pos3{})
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
                out.tTrue    = out.t;   // pure translation: true == achievable
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

                        // ---- TRUE rigid fit: rotate the model's face normal n0
                        // onto the measured normal n1 by the MINIMUM-ARC rotation.
                        // The axis is whatever the face tilt dictates (NOT a
                        // coordinate axis); this is fitting the model to the face.
                        double Rt[9] = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
                        {
                            const Pos3 v = n0.cross(n1);
                            const double c = double(n0.dot(n1));
                            if (c > 1.0 - 1e-7) {
                                // already aligned: identity
                            }
                            else if (c < -1.0 + 1e-7) {
                                // 180 deg about u (any axis perpendicular to n0)
                                Rt[0]=2*u.x*u.x-1; Rt[1]=2*u.x*u.y;   Rt[2]=2*u.x*u.z;
                                Rt[3]=2*u.x*u.y;   Rt[4]=2*u.y*u.y-1; Rt[5]=2*u.y*u.z;
                                Rt[6]=2*u.x*u.z;   Rt[7]=2*u.y*u.z;   Rt[8]=2*u.z*u.z-1;
                            }
                            else {
                                const double s = 1.0 / (1.0 + c);
                                const double vx=v.x, vy=v.y, vz=v.z;
                                Rt[0]=1+s*(-vy*vy-vz*vz); Rt[1]=-vz+s*(vx*vy);      Rt[2]= vy+s*(vx*vz);
                                Rt[3]= vz+s*(vx*vy);      Rt[4]=1+s*(-vx*vx-vz*vz); Rt[5]=-vx+s*(vy*vz);
                                Rt[6]=-vy+s*(vx*vz);      Rt[7]= vx+s*(vy*vz);      Rt[8]=1+s*(-vx*vx-vy*vy);
                            }
                        }

                        // ---- ACHIEVABLE fit: the rotation ABOUT the machine's one
                        // rotary axis that best makes that same alignment -- the
                        // part of the true tilt the machine can physically produce.
                        // (Rotation about the axis keeps every toolpath direction
                        // IK-reachable; the off-axis remainder is accepted residual.)
                        double Ra[9] = { 1, 0, 0,  0, 1, 0,  0, 0, 1 };
                        {
                            Pos3 A = rotaryAxis;
                            const float Alen = A.pythag();
                            if (Alen > 1e-6f) {
                                A = A * (1.0f / Alen);
                                Pos3 n0p = n0 - A * n0.dot(A);
                                Pos3 n1p = n1 - A * n1.dot(A);
                                const float l0 = n0p.pythag();
                                const float l1 = n1p.pythag();
                                if (l0 > 1e-5f && l1 > 1e-5f) {
                                    n0p = n0p * (1.0f / l0);
                                    n1p = n1p * (1.0f / l1);
                                    const double th = std::atan2(
                                        double(A.dot(n0p.cross(n1p))), double(n0p.dot(n1p)));
                                    const double ca = std::cos(th), sa = std::sin(th), ta = 1.0 - ca;
                                    const double ax = A.x, ay = A.y, az = A.z;
                                    Ra[0]=ta*ax*ax+ca;    Ra[1]=ta*ax*ay-sa*az; Ra[2]=ta*ax*az+sa*ay;
                                    Ra[3]=ta*ax*ay+sa*az; Ra[4]=ta*ay*ay+ca;    Ra[5]=ta*ay*az-sa*ax;
                                    Ra[6]=ta*ax*az-sa*ay; Ra[7]=ta*ay*az+sa*ax; Ra[8]=ta*az*az+ca;
                                }
                            }
                        }

                        // Rotate a vector v by a row-major 3x3 M.
                        auto mapVec = [](const double* M, const Pos3& v) -> Pos3 {
                            return Pos3{
                                float(M[0]*v.x + M[1]*v.y + M[2]*v.z),
                                float(M[3]*v.x + M[4]*v.y + M[5]*v.z),
                                float(M[6]*v.x + M[7]*v.y + M[8]*v.z)
                            };
                        };

                        for (int i = 0; i < 9; i++) { out.r[i] = Ra[i]; out.rTrue[i] = Rt[i]; }

                        // ---- ACHIEVABLE translation: keep the part ATTACHED TO ITS
                        // AXIS.  The only motion the machine can make to LOCATE the
                        // part is a rotation about the ROTARY AXIS LINE (direction
                        // rotaryAxis through rotaryPoint) plus a height offset.  So
                        // we pivot Ra about that LINE -- not the probed face centroid
                        // -- because driving a centroid-pivot correction would swing
                        // the part about its face and drag the whole coordinate
                        // system (origin + axis) sideways; pivoting about the axis
                        // returns the ENTIRE frame to its assumed place when squared.
                        // The offset is along the part's up-normal n0 only (no X/Y:
                        // we have no lateral information and trust the user's mount).
                        const Pos3   RaLineCN = mapVec(Ra, cN - rotaryPoint) + rotaryPoint;
                        const double dzA      = double((cM - RaLineCN).dot(n0));
                        out.t = (rotaryPoint - mapVec(Ra, rotaryPoint)) + n0 * float(dzA);

                        // ---- TRUE translation (record / rms / honest pose): the
                        // least-squares-optimal pivot is the probed centroid cN,
                        // with the offset purely along the measured normal n1 (the
                        // minimal, in-plane-free fit -- no lateral leak).
                        const double dzT = double((cM - cN).dot(n1));
                        out.tTrue = (cN - mapVec(Rt, cN)) + n1 * float(dzT);
                        out.valid = true;

                        // rms = quality of the TRUE fit (how well the model lines
                        // up with the measured face); the residual that survives
                        // the achievable fit is the part the machine can't make.
                        double sse = 0.0;
                        for (size_t i = 0; i < n; i++) {
                            const Pos3 e = measured[i] - out.applyTrue(nominal[i]);
                            sse += double(e.x)*e.x + double(e.y)*e.y + double(e.z)*e.z;
                        }
                        out.rmsError = std::sqrt(sse / double(n));
                        return out;
                    }
                }
                // Degenerate (collinear points / bad basis): fall through to the
                // translation-only fit below.
            }

            // n == 2, or a degenerate n >= 3: uniform offset ALONG THE NORMAL only.
            // We never measured lateral position, so the correction must not shift
            // the part in X/Y -- project the centroid delta onto the average
            // surface normal and translate by that alone.
            Pos3 n0avg{};
            for (size_t i = 0; i < n && i < normal.size(); i++) { n0avg = n0avg + normal[i]; }
            const float n0avgLen = n0avg.pythag();
            n0avg = (n0avgLen > 1e-6f) ? n0avg * (1.0f / n0avgLen) : Pos3{ 0, 0, 1 };

            out.t     = n0avg * (cM - cN).dot(n0avg);
            out.tTrue = out.t;   // pure translation: true == achievable
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
            j["valid"]    = valid;
            j["r"]        = Json::array({ r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8] });
            j["t"]        = Json::array({ t.x, t.y, t.z });
            j["rTrue"]    = Json::array({ rTrue[0], rTrue[1], rTrue[2], rTrue[3], rTrue[4],
                                          rTrue[5], rTrue[6], rTrue[7], rTrue[8] });
            j["tTrue"]    = Json::array({ tTrue.x, tTrue.y, tTrue.z });
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
            if (j.contains("rTrue") && j["rTrue"].is_array() && j["rTrue"].size() >= 9) {
                for (int i = 0; i < 9; i++) { rTrue[i] = j["rTrue"][i].get<double>(); }
            }
            if (j.contains("tTrue") && j["tTrue"].is_array() && j["tTrue"].size() >= 3) {
                tTrue.x = j["tTrue"][0].get<float>();
                tTrue.y = j["tTrue"][1].get<float>();
                tTrue.z = j["tTrue"][2].get<float>();
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
            // The fitted `result` is a PER-RUN MEASUREMENT, not design intent, and
            // is intentionally NOT serialized.  Persisting it would auto-apply a
            // stale correction from a previous session to a part that may have
            // been re-mounted -- exactly the cross-run compounding that drove the
            // machine to a wrong pose.  A loaded project always starts un-probed.
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
            // `result` deliberately not loaded -- see getState().  Always
            // starts at identity (reset above).
        }
    };
}
