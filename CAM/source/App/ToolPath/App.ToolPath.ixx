module;

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <cstddef>

#include <dbg.hpp>

export module Cam.App.ToolPath;

import Rev.Core.Vertex3;
import Rev.Core.Color;
import Rev.Core.Pos;
import Rev.Core.Pos3;

import Cam.App.Model;
import Cam.App.Tool;
import Cam.App.Slicer.Strategy.Slice.Segment2;

import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.Strategies.Bore;
import Cam.App.Slicer.Strategy.Strategies.Profile;
import Cam.App.Slicer.Strategy.Strategies.Hatch;

export namespace Cam::App {

    using namespace Rev::Core;
    using Segment = Slicer::Strategy::Slice::Segment;

    struct ToolPathPoint {
        Pos3 position = {};
        double t = 0.0;
        bool rapid = false;
        bool cutting = true;
    };

    // Computed tool motion for one material state.
    struct ToolPath {

        using Bore = Slicer::Strategy::Strategies::Bore;
        using Profile = Slicer::Strategy::Strategies::Profile;
        using Hatch = Slicer::Strategy::Strategies::Hatch;

        using StrategyInstance = std::variant<Bore, Profile, Hatch>;

        // Settings
        std::string toolName = "";
        std::string strategy = Hatch::name();
        bool strategyAuto = true;

        double stepDown = 1.0;
        double feedRate = 1000.0;
        double stepover = 0.25;

        // Result
        std::vector<ToolPathPoint> points;
        bool computed = false;

        // State
        //--------------------------------------------------

        ToolPath() = default;
        ToolPath(const ToolPath&) = delete;
        ToolPath& operator=(const ToolPath&) = delete;
        ToolPath(ToolPath&&) = default;
        ToolPath& operator=(ToolPath&&) = default;

        void clearPathData() {
            points.clear();
            computed = false;
            strategyInstance.reset();
        }

        void clear() {
            toolName.clear();
            clearPathData();
        }

        bool empty() const { return points.empty(); }
        size_t size() const { return points.size(); }

        const Slicer::Strategy::Strategy* strategyResult() const {
            if (!strategyInstance) { return nullptr; }

            return std::visit(
                [](const auto& s) -> const Slicer::Strategy::Strategy* {
                    return &s;
                },
                *strategyInstance
            );
        }

        // Strategy detection
        //--------------------------------------------------

        static std::string detectStrategy(const Model& model) {

            if (Bore::detect(model)) { return Bore::name(); }
            if (Profile::detect(model)) { return Profile::name(); }
            if (Hatch::detect(model)) { return Hatch::name(); }

            return Hatch::name();
        }

        // Point building
        //--------------------------------------------------

        void addPoint(const Pos& p, float z, double& t, bool rapid = false, bool cutting = true) {
            points.push_back({
                .position = { p.x, p.y, z },
                .t = t,
                .rapid = rapid,
                .cutting = cutting
            });

            t += 1.0;
        }

        void addSegmentPoints(const Segment& segment, float z, double& t, int samples = 24) {
            if (segment.kind == Segment::Kind::Line) {
                addPoint(segment.start(), z, t);
                addPoint(segment.end(), z, t);
                return;
            }

            Pos last = segment.at(0.0f);

            for (int i = 1; i <= samples; i++) {
                Pos p = segment.at(float(i) / float(samples));

                addPoint(last, z, t);
                addPoint(p, z, t);

                last = p;
            }
        }

        void buildPointsFromStrategy(const Slicer::Strategy::Strategy& strategyImpl) {
            points.clear();

            double t = 0.0;

            for (const Slicer::Strategy::LayerPath& layer : strategyImpl.paths()) {
                if (!layer.points.empty()) {
                    for (const Pos& p : layer.points) {
                        addPoint(p, layer.z, t);
                    }

                    continue;
                }

                for (const Segment& segment : layer.segments) {
                    addSegmentPoints(segment, layer.z, t);
                }
            }
        }

        // Compute
        //--------------------------------------------------

        bool compute(Model& toCarve, Model& toAvoid, const Tool& tool) {
            clearPathData();

            toolName = tool.name;

            if (strategyAuto) {

                const std::string detected = detectStrategy(toCarve);

                if (detected != strategy) {
                    dbg(
                        "[ToolPath] Auto-detected strategy: %s -> %s",
                        strategy.c_str(),
                        detected.c_str()
                    );

                    strategy = detected;
                }
            }

            dbg(
                "[ToolPath] Computing toolpath with tool \"%s\" strategy=%s (auto=%i)",
                toolName.c_str(),
                strategy.c_str(),
                int(strategyAuto)
            );

            Slicer::Strategy::StrategyContext ctx {
                .positive = &toCarve,
                .negative = &toAvoid,
                .tool = &tool,
                .stepDown = static_cast<float>(stepDown),
                .stepover = static_cast<float>(stepover)
            };

            if (strategy == Bore::name()) { strategyInstance = Bore {}; }
            else if (strategy == Profile::name()) { strategyInstance = Profile {}; }
            else {
                strategy = Hatch::name();
                strategyInstance = Hatch {};
            }

            std::visit([&ctx](auto& s) {
                Slicer::Strategy::Strategy::run(s, ctx);
            }, *strategyInstance);

            const Slicer::Strategy::Strategy& strategyImpl =
                *strategyResult();

            buildPointsFromStrategy(strategyImpl);

            computed = !points.empty();

            dbg(
                "[ToolPath] Done. slices=%zu paths=%zu points=%zu computed=%i",
                strategyImpl.slices().size(),
                strategyImpl.paths().size(),
                points.size(),
                int(computed)
            );

            return computed;
        }

        bool compute(Model& toCarve, const Tool& tool) {
            return compute(toCarve, toCarve, tool);
        }

        bool compute(Model& toCarve) {
            return compute(toCarve, Tool::GodTool());
        }

        bool compute(Model& toCarve, Model& toAvoid) {
            return compute(toCarve, toAvoid, Tool::GodTool());
        }

        bool computeFromDelta(Model& deltaModel, Model& remainingModel, const Tool& tool) {
            return compute(deltaModel, remainingModel, tool);
        }

        // Rendering
        //--------------------------------------------------

        void buildLineSegments(std::vector<Vertex3>& lines) const {
            lines.clear();

            if (points.size() < 2) { return; }

            Color cutColor = { 1.0f, 0.0f, 1.0f, 1.0f };
            Color rapidColor = { 0.6f, 0.0f, 1.0f, 0.35f };

            for (size_t i = 0; i + 1 < points.size(); i++) {
                const Pos3& a = points[i].position;
                const Pos3& b = points[i + 1].position;

                Color color = points[i + 1].rapid ? rapidColor : cutColor;

                lines.push_back({ a.x, a.y, a.z, color });
                lines.push_back({ b.x, b.y, b.z, color });
            }
        }

    private:

        std::optional<StrategyInstance> strategyInstance;
    };
}
