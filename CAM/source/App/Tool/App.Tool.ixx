module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

export module Cam.App.Tool;

import Rev.Core.Pos3;
import Rev.Core.Color;
import Rev.Core.Vertex3;

export namespace Cam::App {

    // A named OPERATION PROFILE -- a list of motion/operation defaults a tool
    // carries.  Generalized so it applies to ANY tool: a cutter reads it as
    // feed / spindle / stepdown ("Roughing", "Finishing"); a PROBE reads the same
    // `feedRate` as its APPROACH rate and uses `rapidSpeed` ("Careful", "Rapid").
    // The same SPEED concept underneath, named contextually at the UI.  Stored as
    // its own referenced .json; a first-class struct so the toolpath AND probe
    // layers can both consume it.
    struct OperationProfile {
        std::string name         = "Default";
        double      feedRate     = 250.0;     // mm/min -- feed (cut) / approach (probe)
        double      plungeRate   = 100.0;     // mm/min, Z entry
        double      spindleSpeed = 10000.0;   // RPM (cutters)
        double      stepdown     = 0.5;       // mm per pass (cutters)
        double      stepover     = 0.25;      // fraction of cutting diameter (cutters)
        double      rapidSpeed   = 10.0;      // mm/s -- travel, all tools
        bool        climbMilling = true;      // cutters
    };

    struct Tool {

        enum class Type {
            EndMill,
            ThreadMill,
            Chamfer,
            Probe          // touch probe — identical geometry pipeline to an end-mill
        };

        // Identity
        //--------------------------------------------------

        Type type = Type::EndMill;
        std::string name = "1mm x 100mm God Tool";

        // Full path to the tool JSON file. Empty until first save.
        std::string filePath = "";

        // Geometry
        //--------------------------------------------------
        //
        // The tool is a profile revolved about its axis, built additively from
        // the tip (y = 0) upward.  Each region contributes its own length, so
        // the overall length is simply their sum — no cross-field reconciliation
        // is needed and any single value can be edited freely.
        //
        //   1. Cutting tip   : `taperAngle` from horizontal (0 = flat end mill,
        //                      45 = chamfer); a cone of height radius*tan(angle).
        //   2. Flutes        : cutting `radius` up to `cuttingLength` (from tip).
        //   3. Shoulder      : an optional wider/narrower neck at
        //                      `shoulderDiameter`, reached over a
        //                      `shoulderTaperAngle` transition, of axial length
        //                      `shoulderLength`.
        //   4. Collar (shank): the gripped section at `collarDiameter` of axial
        //                      length `collarLength`, at the very top.

        double diameter = 1.0;             // cutting diameter
        double radius = 0.5;               // cutting radius (diameter / 2)
        double cuttingLength = 10.0;       // flute length, measured from the tip
        double taperAngle = 0.0;           // tip taper, degrees from horizontal

        double shoulderDiameter = 0.0;     // 0 = same as cutting diameter
        double shoulderLength = 0.0;       // axial shoulder/neck length
        double shoulderTaperAngle = 45.0;  // shoulder transition taper, degrees

        double collarDiameter = 0.0;       // shank diameter (0 = same as shoulder)
        double collarLength = 40.0;        // axial shank length at the top

        // Cached overall length (tip to top); kept equal to totalLength().
        double length = 50.0;

        Rev::Core::Pos3 axis = { 0.0f, 0.0f, 1.0f };

        // Toolpath defaults a new operation adopts when this tool is selected.
        double defaultFeedRate = 250.0;    // mm/min
        double defaultStepdown = 0.5;      // mm
        double defaultStepover = 0.25;     // fraction of diameter
        double defaultRapidSpeed = 10.0;   // mm/s
        bool   defaultClimbMilling = true;

        // Type-specific settings
        //--------------------------------------------------
        //
        // Only the sub-struct matching `type` is meaningful.  Named members
        // (rather than a variant) keep value semantics, trivial JSON, and let the
        // settings window show/hide a whole section by type.

        // Probe (Type::Probe): the stylus is a small CYLINDER, so on a tilted face
        // its uphill edge contacts first and reads high by r*tan(theta).
        // `stylusRadius` is that edge radius -- CALIBRATED once per probe against a
        // flat gauge / cylinder (not typed) -- and the rotary-axis fit subtracts
        // its bias.  See the probe calibration window.
        struct ProbeSettings {
            // The stylus tip shape.  The Tool's `diameter` applies to BOTH; the
            // shape decides the tilted-probe contact model -- a CYLINDER contacts
            // its uphill EDGE (bias ~ r*tan(theta)); a SPHERE rolls to a tangent
            // point (bias ~ R*(1-cos theta), far smaller).
            enum class TipGeometry { Cylinder, Sphere };

            TipGeometry tipGeometry        = TipGeometry::Cylinder;
            double      stylusRadius        = 0.5;   // mm, calibrated edge radius
            bool        calibrated          = false; // set true by a calibration run
            double      calibrationResidual = 0.0;   // mm, fit residual of last calib
        };
        ProbeSettings probe;

        // Thread mill (Type::ThreadMill): a FORM cutter, not a tap.  Its teeth are
        // a V of `toothAngle` (the thread form angle, e.g. 60 deg) ground at the
        // cutting `diameter`, repeated `toothCount` times up the body; the shank is
        // the collar.  Crucially it is NOT tied to a pitch or a major/minor
        // diameter -- those belong to the thread-milling OPERATION, because ONE
        // cutter mills a whole range of pitches and diameters by helical interp.
        struct ThreadMillSettings {
            double toothAngle = 60.0;   // included V angle of the teeth, degrees
            int    toothCount = 1;      // number of cutting teeth (form rows)
        };
        ThreadMillSettings threadMill;

        // Named operation profiles this tool references (roughing / finishing /
        // careful / rapid ...).  Empty => operations use the bare default* fields.
        std::vector<OperationProfile> profiles;

        // Profile
        //--------------------------------------------------

        // A point on the revolved cross-section: radius from the axis at axial
        // height y (y = 0 at the tip).
        struct ProfilePoint {
            double r = 0.0;
            double y = 0.0;
        };

        static constexpr double kPi = 3.14159265358979;

        // Height of the conical tip for a given taper angle (from horizontal).
        static double tipTaperHeight(double cuttingRadius, double taperAngleDeg) {
            if (taperAngleDeg <= 1e-6) { return 0.0; }
            const double clamped = std::clamp(taperAngleDeg, 0.0, 89.9);
            return cuttingRadius * std::tan(clamped * kPi / 180.0);
        }

        // Axial height of the shoulder transition cone.
        static double shoulderTransitionHeight(
            double cuttingRadius,
            double shoulderRadius,
            double shoulderTaperAngleDeg
        ) {
            const double dr = std::fabs(shoulderRadius - cuttingRadius);
            if (dr <= 1e-9 || shoulderTaperAngleDeg <= 1e-6) { return 0.0; }
            const double clamped = std::clamp(shoulderTaperAngleDeg, 1e-6, 89.9);
            return dr / std::tan(clamped * kPi / 180.0);
        }

        static double effectiveShoulderRadius(double cuttingRadius, double shoulderDiameter) {
            if (shoulderDiameter <= 1e-6) { return cuttingRadius; }
            return shoulderDiameter * 0.5;
        }

        static double effectiveCollarRadius(double shoulderRadius, double collarDiameter) {
            if (collarDiameter <= 1e-6) { return shoulderRadius; }
            return collarDiameter * 0.5;
        }

        // Append the shoulder transition, neck, collar/shank and the closing top
        // centre point, starting at axial height `y` with the body currently at
        // radius `fromR`.  Shared by every tool type so the shank always matches.
        void appendShank(std::vector<ProfilePoint>& pts, double y, double fromR) const {

            const double shoulderR = effectiveShoulderRadius(fromR, shoulderDiameter);
            const double collarR = effectiveCollarRadius(shoulderR, collarDiameter);

            const double transH = shoulderTransitionHeight(fromR, shoulderR, shoulderTaperAngle);

            if (transH > 1e-9) {
                y += transH;
                pts.push_back({ shoulderR, y });
            }
            else if (std::fabs(shoulderR - fromR) > 1e-9) {
                pts.push_back({ shoulderR, y });  // instantaneous step
            }

            const double shLen = std::max(shoulderLength, 0.0);

            if (shLen > 1e-9) {
                y += shLen;
                pts.push_back({ shoulderR, y });
            }

            const double colLen = std::max(collarLength, 0.0);

            if (colLen > 1e-9) {
                if (std::fabs(collarR - shoulderR) > 1e-9) {
                    pts.push_back({ collarR, y });  // step to shank radius
                }
                y += colLen;
                pts.push_back({ collarR, y });
            }

            pts.push_back({ 0.0, y });  // top centre
        }

        // The cutting silhouette of a thread mill -- a FORM CUTTER.  The teeth are
        // V-ridges of included angle `threadMill.toothAngle`, crests at the cutting
        // radius (`diameter`/2), `threadMill.toothCount` of them stacked up from the
        // tip; then the shank (collar).  The cutter carries NO pitch / major-minor:
        // those are the OPERATION's (one cutter mills many pitches & diameters).
        //
        // The teeth tessellate root->crest->root.  The radial depth is a modest
        // fraction of the cutting radius and the angle sets the axial spacing
        // (tan(angle/2) = (spacing/2)/depth), so the rendered V reads at exactly the
        // ground tooth angle.  Mirrored this is a thread-mill cutter, unmistakable
        // from an end mill.
        std::vector<ProfilePoint> threadMillProfile() const {

            std::vector<ProfilePoint> pts;

            const double crestR = std::max(radius, 0.0);   // tooth OD / 2
            if (crestR <= 1e-9) { return pts; }

            const int    teeth  = std::max(threadMill.toothCount, 1);
            const double angle  = std::clamp(threadMill.toothAngle, 10.0, 170.0);
            const double depth  = std::max(crestR * 0.18, 1e-3);          // radial tooth depth
            const double rootR  = std::max(crestR - depth, 1e-4);
            const double half   = depth * std::tan(angle * 0.5 * kPi / 180.0);  // axial half-tooth

            pts.push_back({ 0.0, 0.0 });        // tip centre
            pts.push_back({ rootR, 0.0 });      // bottom root at the tip

            double y = 0.0;
            for (int i = 0; i < teeth; i++) {
                y += half; pts.push_back({ crestR, y });   // crest (cutting edge)
                y += half; pts.push_back({ rootR,  y });   // root
            }

            appendShank(pts, y, rootR);

            return pts;
        }

        // Right-hand silhouette of a PROBE stylus.  The tip diameter is `diameter`
        // for both shapes; the shape changes the bottom.  A CYLINDER is flat-ended
        // (a pin); a SPHERE is a ball on a thin stem -- and the 2D preview / 3D
        // mesh follow directly since both consume this.
        std::vector<ProfilePoint> probeProfile() const {

            std::vector<ProfilePoint> pts;

            const double R = std::max(radius, 0.0);
            if (R <= 1e-9) { return pts; }

            const double stylusLen = std::max({ cuttingLength, 2.0 * R, 1e-4 });

            if (probe.tipGeometry == ProbeSettings::TipGeometry::Sphere) {
                // Ball of radius R on a thin stem.  Trace the right outline from the
                // bottom pole, out past the equator, back in to where it narrows to
                // the stem radius; then the stem, then the shank.
                const double stemR = std::max(R * 0.35, 1e-3);
                const double aStem = kPi - std::asin(std::clamp(stemR / R, 0.0, 1.0));
                const int seg = 16;
                for (int i = 0; i <= seg; i++) {
                    const double a = aStem * (static_cast<double>(i) / seg);
                    pts.push_back({ R * std::sin(a), R * (1.0 - std::cos(a)) });
                }
                const double yNeck = R * (1.0 - std::cos(aStem));
                if (stylusLen > yNeck + 1e-6) {
                    pts.push_back({ stemR, stylusLen });
                    appendShank(pts, stylusLen, stemR);
                }
                else {
                    appendShank(pts, yNeck, stemR);
                }
                return pts;
            }

            // Cylinder (flat-ended pin): full radius up the stylus, then shank.
            pts.push_back({ 0.0, 0.0 });
            pts.push_back({ R, 0.0 });
            pts.push_back({ R, stylusLen });
            appendShank(pts, stylusLen, R);
            return pts;
        }

        // Right-hand silhouette of the revolved tool, tip (0,0) to top (0, total).
        // Consumed by both the 2D preview (mirrored) and the 3D mesh (revolved),
        // so the two can never disagree.  Geometry differs by tool type.
        std::vector<ProfilePoint> profile() const {

            if (type == Type::ThreadMill) { return threadMillProfile(); }
            if (type == Type::Probe)      { return probeProfile(); }

            std::vector<ProfilePoint> pts;

            const double cuttingR = std::max(radius, 0.0);

            if (cuttingR <= 1e-9) { return pts; }

            const double tipH = tipTaperHeight(cuttingR, taperAngle);
            const double cutLen = std::max({ cuttingLength, tipH, 1e-4 });

            // Tip + flutes.
            pts.push_back({ 0.0, 0.0 });        // tip centre
            pts.push_back({ cuttingR, tipH });  // end of taper (== (cuttingR, 0) when flat)
            pts.push_back({ cuttingR, cutLen }); // end of flutes

            appendShank(pts, cutLen, cuttingR);

            return pts;
        }

        double totalLength() const {
            const std::vector<ProfilePoint> p = profile();
            return p.empty() ? length : p.back().y;
        }

        // 3D mesh
        //--------------------------------------------------
        //
        // The tool's display mesh lives here, generated once whenever the
        // geometry changes (recomputeLength), in LOCAL space: tip at the origin,
        // body along +Z.  The world view places it with a transform rather than
        // regenerating triangles every frame.  Vertex colour alpha is 0 so the
        // actor's mesh colour tints it.

        std::vector<Rev::Core::Vertex3> mesh;
        std::size_t meshRevision = 0;  // bumped on every rebuild

        void buildMesh(int sides = 32) {

            mesh.clear();
            meshRevision++;

            const std::vector<ProfilePoint> p = profile();

            if (p.size() < 2 || sides < 3) { return; }

            const double twoPi = 2.0 * kPi;
            const Rev::Core::Color tint = { 0.0f, 0.0f, 0.0f, 0.0f };  // alpha 0 -> use actor colour

            auto tri = [&](const Rev::Core::Pos3& a, const Rev::Core::Pos3& b, const Rev::Core::Pos3& c) {
                Rev::Core::Pos3 n = (b - a).cross(c - a);
                const float len = n.pythag();
                n = len > 1e-9f ? n / len : Rev::Core::Pos3(0.0f, 0.0f, 1.0f);
                mesh.push_back(Rev::Core::Vertex3(a.x, a.y, a.z, tint, n));
                mesh.push_back(Rev::Core::Vertex3(b.x, b.y, b.z, tint, n));
                mesh.push_back(Rev::Core::Vertex3(c.x, c.y, c.z, tint, n));
            };

            for (std::size_t s = 0; s + 1 < p.size(); s++) {

                const float r0 = static_cast<float>(p[s].r);
                const float r1 = static_cast<float>(p[s + 1].r);

                if (r0 <= 1e-6f && r1 <= 1e-6f) { continue; }

                const float y0 = static_cast<float>(p[s].y);
                const float y1 = static_cast<float>(p[s + 1].y);

                for (int i = 0; i < sides; i++) {

                    const int j = (i + 1) % sides;

                    const float a0 = static_cast<float>(twoPi * i / sides);
                    const float a1 = static_cast<float>(twoPi * j / sides);

                    const Rev::Core::Pos3 p00(std::cos(a0) * r0, std::sin(a0) * r0, y0);
                    const Rev::Core::Pos3 p01(std::cos(a1) * r0, std::sin(a1) * r0, y0);
                    const Rev::Core::Pos3 p10(std::cos(a0) * r1, std::sin(a0) * r1, y1);
                    const Rev::Core::Pos3 p11(std::cos(a1) * r1, std::sin(a1) * r1, y1);

                    if (r0 <= 1e-6f) {
                        tri(Rev::Core::Pos3(0.0f, 0.0f, y0), p10, p11);
                    }
                    else if (r1 <= 1e-6f) {
                        tri(p00, p01, Rev::Core::Pos3(0.0f, 0.0f, y1));
                    }
                    else {
                        tri(p00, p01, p10);
                        tri(p01, p11, p10);
                    }
                }
            }
        }

        // Keep the cached length + mesh consistent with the profile.
        void recomputeLength() {
            radius = diameter * 0.5;
            length = totalLength();
            buildMesh();
        }

        // Type helpers
        //--------------------------------------------------

        static std::string typeToKindString(Type type) {
            switch (type) {
                case Type::EndMill:    return "EndMill";
                case Type::ThreadMill: return "ThreadMill";
                case Type::Chamfer:    return "Chamfer";
                case Type::Probe:      return "Probe";
            }
            return "EndMill";
        }

        static Type typeFromKindString(const std::string& kind) {
            if (kind == "ThreadMill") { return Type::ThreadMill; }
            if (kind == "Chamfer")    { return Type::Chamfer; }
            if (kind == "Probe")      { return Type::Probe; }
            return Type::EndMill;  // EndMill and legacy "Cylinder"
        }

        static std::string typeDisplayName(Type type) {
            switch (type) {
                case Type::EndMill:    return "End mill";
                case Type::ThreadMill: return "Thread mill";
                case Type::Chamfer:    return "Chamfer";
                case Type::Probe:      return "Probe";
            }
            return "End mill";
        }

        static std::string tipGeometryToString(ProbeSettings::TipGeometry g) {
            return g == ProbeSettings::TipGeometry::Sphere ? "Sphere" : "Cylinder";
        }

        static ProbeSettings::TipGeometry tipGeometryFromString(const std::string& s) {
            return s == "Sphere" ? ProbeSettings::TipGeometry::Sphere
                                 : ProbeSettings::TipGeometry::Cylinder;
        }

        static std::string typeEyebrow(Type type) {
            switch (type) {
                case Type::EndMill:    return "END MILL";
                case Type::ThreadMill: return "THREAD MILL";
                case Type::Chamfer:    return "CHAMFER";
                case Type::Probe:      return "PROBE";
            }
            return "END MILL";
        }

        // Defaults
        //--------------------------------------------------

        static Tool GodTool(double diameterMm = 1.0, int index = 1) {
            Tool tool;

            tool.type = Type::EndMill;
            tool.diameter = diameterMm;
            tool.radius = diameterMm * 0.5;
            tool.cuttingLength = 20.0;
            tool.collarLength = 80.0;   // plain 100mm tool: 20 flutes + 80 shank
            tool.axis = { 0.0f, 0.0f, 1.0f };
            tool.name = "God Tool " + std::to_string(index);

            tool.recomputeLength();

            return tool;
        }
    };
}
