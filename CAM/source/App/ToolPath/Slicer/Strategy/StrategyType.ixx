module;

#include <string>

export module Cam.App.Slicer.Strategy.StrategyType;

export namespace Cam::App::Slicer::Strategy {

    enum class StrategyType {
        Hatch,
        Profile
    };

    inline std::string strategyTypeToString(StrategyType type) {

        switch (type) {

            case StrategyType::Hatch:
                return "Hatch";

            case StrategyType::Profile:
                return "Profile";
        }

        return "Profile";
    }

    inline StrategyType strategyTypeFromString(const std::string& value) {

        if (value == "Hatch") {
            return StrategyType::Hatch;
        }

        return StrategyType::Profile;
    }

    inline std::string strategyTypeDisplayName(StrategyType type) {

        switch (type) {

            case StrategyType::Hatch:
                return "Hatch";

            case StrategyType::Profile:
                return "Profile";
        }

        return "Profile";
    }
}
