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
