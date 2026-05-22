module;

#include <memory>

export module Cam.App.Slicer.Strategy.StrategyFactory;

import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.StrategyType;
import Cam.App.Slicer.Strategy.HatchStrategy;
import Cam.App.Slicer.Strategy.ProfileStrategy;
import Cam.App.Slicer.Strategy.BoreStrategy;

export namespace Cam::App::Slicer::Strategy {

    inline std::unique_ptr<Strategy> createStrategy(StrategyType type) {

        switch (type) {

            case StrategyType::Hatch:
                return std::make_unique<HatchStrategy>();

            case StrategyType::Profile:
                return std::make_unique<ProfileStrategy>();

            case StrategyType::Bore:
                return std::make_unique<BoreStrategy>();
        }

        return std::make_unique<HatchStrategy>();
    }
}
