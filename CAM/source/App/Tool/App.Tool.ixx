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

        // Right-hand silhouette of the revolved tool, tip (0,0) to top (0, total).
        // Consumed by both the 2D preview (mirrored) and the 3D mesh (revolved),
        // so the two can never disagree.
        std::vector<ProfilePoint> profile() const {

            std::vector<ProfilePoint> pts;

            const double cuttingR = std::max(radius, 0.0);

            if (cuttingR <= 1e-9) { return pts; }

            const double shoulderR = effectiveShoulderRadius(cuttingR, shoulderDiameter);
            const double collarR = effectiveCollarRadius(shoulderR, collarDiameter);

            const double tipH = tipTaperHeight(cuttingR, taperAngle);
            const double cutLen = std::max({ cuttingLength, tipH, 1e-4 });

            double y = 0.0;

            // Tip + flutes.
            pts.push_back({ 0.0, 0.0 });        // tip centre
            pts.push_back({ cuttingR, tipH });  // end of taper (== (cuttingR, 0) when flat)
            pts.push_back({ cuttingR, cutLen }); // end of flutes
            y = cutLen;

            // Shoulder transition + neck.
            const double transH = shoulderTransitionHeight(cuttingR, shoulderR, shoulderTaperAngle);

            if (transH > 1e-9) {
                y += transH;
                pts.push_back({ shoulderR, y });
            }
            else if (std::fabs(shoulderR - cuttingR) > 1e-9) {
                pts.push_back({ shoulderR, y });  // instantaneous step
            }

            const double shLen = std::max(shoulderLength, 0.0);

            if (shLen > 1e-9) {
                y += shLen;
                pts.push_back({ shoulderR, y });
            }

            // Collar / shank.
            const double colLen = std::max(collarLength, 0.0);

            if (colLen > 1e-9) {
                if (std::fabs(collarR - shoulderR) > 1e-9) {
                    pts.push_back({ collarR, y });  // step to shank radius
                }
                y += colLen;
                pts.push_back({ collarR, y });
            }

            pts.push_back({ 0.0, y });  // top centre

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
