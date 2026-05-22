module;

#include <vector>
#include <memory>
#include <cstddef>

export module Cam.App.Slicer.Strategy.Strategy;

import Rev.Core.Pos3;
import Rev.Core.Vertex3;

import Cam.App.Model;
import Cam.App.Tool;

import Cam.App.Slicer.Strategy.StrategyType;
import Cam.App.Slicer.Strategy.Slice.Slice;

export namespace Cam::App::Slicer::Strategy {

    using namespace Rev::Core;

    using SliceLayer = Slice::Slice;

    struct StrategyContext {

        const Model* positive = nullptr;
        const Model* negative = nullptr;
        const Tool* tool = nullptr;

        float stepDown = 1.0f;
    };

    struct Strategy {

        virtual ~Strategy() = default;

        virtual StrategyType type() const = 0;

        virtual void execute(const StrategyContext& ctx) = 0;

        virtual const std::vector<std::unique_ptr<SliceLayer>>& slices() const = 0;

        static bool boundsFromModel(
            const Model& model,
            Pos3& min,
            Pos3& max
        ) {
            if (!model.loaded) { return false; }
            if (model.render.triangles.empty()) { return false; }

            bool valid = false;

            for (const Vertex3& v : model.render.triangles) {
                if (!valid) {
                    min = v;
                    max = v;
                    valid = true;
                    continue;
                }

                min = Pos3::min(min, v);
                max = Pos3::max(max, v);
            }

            return valid;
        }
    };
}
