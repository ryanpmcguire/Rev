module;

#include <string>
#include <vector>
#include <memory>
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
import Cam.App.Slicer.Strategy.StrategyType;
import Cam.App.Slicer.Strategy.StrategyFactory;
import Cam.App.Slicer.Strategy.StrategyDetect;

export namespace Cam::App {

    using namespace Rev::Core;
    using Segment = Slicer::Strategy::Slice::Segment;

    struct ToolPathPoint {
        Pos3 position = {};
        double t = 0.0;
        bool rapid = false;
        bool cutting = true;
    };

    struct ToolPath {

        std::string toolName = "";

        Slicer::Strategy::StrategyType strategy =
            Slicer::Strategy::StrategyType::Hatch;

        // When true, compute() will overwrite `strategy` based on geometry
        // analysis of the carve model. Set to false once the user explicitly
        // chooses a strategy in the settings UI.
        bool strategyAuto = true;

        std::vector<ToolPathPoint> points;

        bool computed = false;

        double stepDown = 1.0;
        double feedRate = 1000.0;

        ToolPath() = default;
        ToolPath(const ToolPath&) = delete;
        ToolPath& operator=(const ToolPath&) = delete;
        ToolPath(ToolPath&&) = default;
        ToolPath& operator=(ToolPath&&) = default;

        void clearPathData() {
            points.clear();
            computed = false;
            ownedStrategy.reset();
        }

        void clear() {
            toolName.clear();
            clearPathData();
        }

        bool empty() const { return points.empty(); }
        size_t size() const { return points.size(); }

        const Slicer::Strategy::Strategy* strategyResult() const {
            return ownedStrategy.get();
        }

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

        void buildPointsFromStrategy(
            const Slicer::Strategy::Strategy& strategyImpl
        ) {
            points.clear();

            double t = 0.0;

            for (const std::unique_ptr<Slicer::Strategy::SliceLayer>& slicePtr :
                strategyImpl.slices()
            ) {
                if (!slicePtr) { continue; }

                const Slicer::Strategy::SliceLayer& slice = *slicePtr;

                if (slice.hasPointPath()) {
                    for (const Pos& p : slice.points) {
                        addPoint(p, slice.z, t);
                    }

                    continue;
                }

                for (const Segment& segment : slice.paths) {
                    addSegmentPoints(segment, slice.z, t);
                }
            }
        }

        bool compute(Model& toCarve, Model& toAvoid, const Tool& tool) {
            clearPathData();

            toolName = tool.name;

            if (strategyAuto) {

                const Slicer::Strategy::StrategyType detected =
                    Slicer::Strategy::detectStrategy(toCarve);

                if (detected != strategy) {
                    dbg(
                        "[ToolPath] Auto-detected strategy: %s -> %s",
                        Slicer::Strategy::strategyTypeToString(strategy).c_str(),
                        Slicer::Strategy::strategyTypeToString(detected).c_str()
                    );

                    strategy = detected;
                }
            }

            dbg(
                "[ToolPath] Computing toolpath with tool \"%s\" strategy=%s (auto=%i)",
                toolName.c_str(),
                Slicer::Strategy::strategyTypeToString(strategy).c_str(),
                int(strategyAuto)
            );

            auto strategyImpl = Slicer::Strategy::createStrategy(strategy);

            Slicer::Strategy::StrategyContext ctx {
                .positive = &toCarve,
                .negative = &toAvoid,
                .tool = &tool,
                .stepDown = static_cast<float>(stepDown)
            };

            strategyImpl->execute(ctx);

            buildPointsFromStrategy(*strategyImpl);

            computed = !points.empty();

            ownedStrategy = std::move(strategyImpl);

            dbg(
                "[ToolPath] Done. slices=%zu points=%zu computed=%i",
                ownedStrategy ? ownedStrategy->slices().size() : 0,
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

        bool computeFromDelta(
            Model& deltaModel,
            Model& remainingModel,
            const Tool& tool
        ) {
            return compute(deltaModel, remainingModel, tool);
        }

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

        std::unique_ptr<Slicer::Strategy::Strategy> ownedStrategy;
    };
}
