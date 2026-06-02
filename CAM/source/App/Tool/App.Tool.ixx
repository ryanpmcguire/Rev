module;

#include <string>

export module Cam.App.Tool;

import Rev.Core.Pos3;

export namespace Cam::App {

    struct Tool {

        enum class Type {
            EndMill,
            ThreadMill,
            Chamfer
        };

        // Identity
        Type type = Type::EndMill;
        std::string name = "1mm x 100mm God Tool";

        // Full path to the tool JSON file. Empty until first save.
        std::string filePath = "";

        // Geometry
        //--------------------------------------------------

        double diameter = 1.0;
        double radius = 0.5;
        double length = 100.0;

        // The tool is a symmetric profile revolved about its axis, measured from
        // the tip (y = 0) upward: a cutting tip of `taperAngle` degrees from
        // horizontal (0 = flat, 45 = chamfer), full `radius` flutes up to
        // `shoulderLength`, then a `collarRadius` shank over the top
        // `collarDepth`.  `length` is the overall stickout length.
        double taperAngle = 0.0;      // degrees from horizontal; 0 = flat end mill
        double shoulderLength = 20.0; // cutting flute length from the tip
        double collarRadius = 0.0;    // shank radius (0 = same as cutting radius)
        double collarDepth = 0.0;     // length of the shank/collar at the top

        // Toolpath defaults a new operation adopts when this tool is selected.
        double defaultFeedRate = 250.0;   // mm/min
        double defaultStepdown = 0.5;     // mm
        double defaultStepover = 0.25;    // fraction of diameter
        double defaultRapidSpeed = 10.0;  // mm/s
        bool   defaultClimbMilling = true;

        Rev::Core::Pos3 axis = { 0.0f, 0.0f, 1.0f };

        // Type helpers
        //--------------------------------------------------

        static std::string typeToKindString(Type type) {

            switch (type) {

                case Type::EndMill:
                    return "EndMill";

                case Type::ThreadMill:
                    return "ThreadMill";

                case Type::Chamfer:
                    return "Chamfer";
            }

            return "EndMill";
        }

        static Type typeFromKindString(const std::string& kind) {

            if (kind == "ThreadMill") {
                return Type::ThreadMill;
            }

            if (kind == "Chamfer") {
                return Type::Chamfer;
            }

            // EndMill and legacy "Cylinder"
            return Type::EndMill;
        }

        static std::string typeDisplayName(Type type) {

            switch (type) {

                case Type::EndMill:
                    return "End mill";

                case Type::ThreadMill:
                    return "Thread mill";

                case Type::Chamfer:
                    return "Chamfer";
            }

            return "End mill";
        }

        static std::string typeEyebrow(Type type) {

            switch (type) {

                case Type::EndMill:
                    return "END MILL";

                case Type::ThreadMill:
                    return "THREAD MILL";

                case Type::Chamfer:
                    return "CHAMFER";
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
            tool.length = 100.0;
            tool.axis = { 0.0f, 0.0f, 1.0f };

            tool.name = "God Tool " + std::to_string(index);

            return tool;
        }
    };
}
