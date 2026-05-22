module;

#include <string>

export module Cam.App.Slicer.Strategy.StrategyType;

export namespace Cam::App::Slicer::Strategy {

    enum class StrategyType {
        Hatch,
        Profile,
        Bore
    };

    inline std::string strategyTypeToString(StrategyType type) {

        switch (type) {

            case StrategyType::Hatch:
                return "Hatch";

            case StrategyType::Profile:
                return "Profile";

            case StrategyType::Bore:
                return "Bore";
        }

        return "Hatch";
    }

    inline StrategyType strategyTypeFromString(const std::string& value) {

        if (value == "Profile") {
            return StrategyType::Profile;
        }

        if (value == "Bore") {
            return StrategyType::Bore;
        }

        return StrategyType::Hatch;
    }

    inline std::string strategyTypeDisplayName(StrategyType type) {

        switch (type) {

            case StrategyType::Hatch:
                return "Hatch";

            case StrategyType::Profile:
                return "Profile";

            case StrategyType::Bore:
                return "Bore";
        }

        return "Hatch";
    }
}
