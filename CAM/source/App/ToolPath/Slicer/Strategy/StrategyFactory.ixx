module;

#include <variant>

export module Cam.App.Slicer.Strategy.StrategyFactory;

import Cam.App.Slicer.Strategy.Strategy;
import Cam.App.Slicer.Strategy.StrategyType;
import Cam.App.Slicer.Strategy.HatchStrategy;
import Cam.App.Slicer.Strategy.ProfileStrategy;
import Cam.App.Slicer.Strategy.BoreStrategy;

export namespace Cam::App::Slicer::Strategy {

    using StrategyInstance = std::variant<
        HatchStrategy,
        ProfileStrategy,
        BoreStrategy
    >;

    inline StrategyInstance createStrategy(StrategyType type) {

        switch (type) {

            case StrategyType::Hatch:
                return HatchStrategy {};

            case StrategyType::Profile:
                return ProfileStrategy {};

            case StrategyType::Bore:
                return BoreStrategy {};
        }

        return HatchStrategy {};
    }

    inline Strategy& strategyFrom(StrategyInstance& instance) {
        return std::visit(
            [](auto& strategy) -> Strategy& {
                return strategy;
            },
            instance
        );
    }

    inline const Strategy& strategyFrom(const StrategyInstance& instance) {
        return std::visit(
            [](const auto& strategy) -> const Strategy& {
                return strategy;
            },
            instance
        );
    }
}
