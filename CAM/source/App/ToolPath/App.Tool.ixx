module;

#include <string>

export module Cam.App.Tool;

import Rev.Core.Pos3;

export namespace Cam::App {

    struct Tool {
        
        enum class Kind { Cylinder };

        Kind kind = Kind::Cylinder;

        std::string name = "1mm x 100mm God Tool";

        double diameter = 1.0;
        double radius = 0.5;
        double length = 100.0;

        Rev::Core::Pos3 axis = { 0.0f, 0.0f, 1.0f };

        static Tool GodTool(
            double diameterMm = 1.0,
            int index = 1
        ) {
            Tool tool;

            tool.kind = Kind::Cylinder;
            tool.diameter = diameterMm;
            tool.radius = diameterMm * 0.5;
            tool.length = 100.0;
            tool.axis = { 0.0f, 0.0f, 1.0f };

            tool.name =
                "God Tool " +
                std::to_string(index) +
                " (" +
                std::to_string(static_cast<int>(diameterMm)) +
                "mm)";

            return tool;
        }
    };
}