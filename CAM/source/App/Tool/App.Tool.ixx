module;



#include <algorithm>

#include <cmath>

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



        // Geometry (profile from tip y = 0 upward)

        //--------------------------------------------------



        double diameter = 1.0;              // cutting diameter

        double radius = 0.5;                // cutting radius (diameter / 2)

        double cuttingLength = 20.0;        // axial cutting / flute length

        double length = 100.0;              // total stickout



        double taperAngle = 0.0;            // tip / cutting taper (deg from horizontal)



        double shoulderDiameter = 0.0;      // 0 = same as cutting diameter

        double shoulderLength = 0.0;        // axial shoulder shank length

        double shoulderTaperAngle = 45.0;   // shoulder transition taper (deg)



        enum class GeometryDriver {

            TotalLength,

            CuttingLength,

            CuttingDiameter,

            ShoulderLength,

            ShoulderDiameter,

            ShoulderTaper,

            CuttingTaper

        };



        static constexpr double kPi = 3.14159265358979;



        static double tipTaperHeight(double cuttingRadius, double taperAngleDeg) {



            if (taperAngleDeg <= 1e-6) {

                return 0.0;

            }



            const double clamped = std::clamp(taperAngleDeg, 0.0, 89.9);

            return cuttingRadius * std::tan(clamped * kPi / 180.0);

        }



        static double shoulderTransitionHeight(

            double cuttingRadius,

            double shoulderRadius,

            double shoulderTaperAngleDeg

        ) {



            if (shoulderTaperAngleDeg <= 1e-6) {

                return 0.0;

            }



            const double dr = std::fabs(shoulderRadius - cuttingRadius);



            if (dr <= 1e-9) {

                return 0.0;

            }



            const double clamped = std::clamp(shoulderTaperAngleDeg, 1e-6, 89.9);

            return dr / std::tan(clamped * kPi / 180.0);

        }



        static double effectiveShoulderRadius(

            double cuttingRadius,

            double shoulderDiameter

        ) {



            if (shoulderDiameter <= 1e-6) {

                return cuttingRadius;

            }



            return std::max(shoulderDiameter * 0.5, 0.0);

        }



        // Keep dependent dimensions consistent; `driver` stays fixed.

        static void reconcileGeometry(

            double& totalLength,

            double& cuttingLength,

            double& shoulderLength,

            double& shoulderDiameter,

            double& cuttingDiameter,

            double& cuttingTaperAngle,

            double& shoulderTaperAngle,

            GeometryDriver driver

        ) {



            cuttingDiameter = std::max(cuttingDiameter, 1e-6);

            totalLength = std::max(totalLength, 1e-6);

            cuttingLength = std::max(cuttingLength, 0.0);

            shoulderLength = std::max(shoulderLength, 0.0);

            shoulderDiameter = std::max(shoulderDiameter, 0.0);

            cuttingTaperAngle = std::clamp(cuttingTaperAngle, 0.0, 89.9);

            shoulderTaperAngle = std::clamp(shoulderTaperAngle, 0.0, 89.9);



            const double cuttingR = cuttingDiameter * 0.5;

            const double shoulderR = effectiveShoulderRadius(cuttingR, shoulderDiameter);



            const double tipH = std::min(

                tipTaperHeight(cuttingR, cuttingTaperAngle),

                totalLength

            );



            const double transitionH = shoulderTransitionHeight(

                cuttingR,

                shoulderR,

                shoulderTaperAngle

            );



            const bool hasShoulder =

                shoulderLength > 1e-6 ||

                std::fabs(shoulderR - cuttingR) > 1e-6;



            const double shoulderBlock = hasShoulder

                ? (transitionH + shoulderLength)

                : 0.0;



            const double minCutting = tipH;

            const double maxCutting = std::max(minCutting, totalLength - shoulderBlock);



            auto fitCuttingAndShoulder = [&]() {

                cuttingLength = std::clamp(cuttingLength, minCutting, maxCutting);



                const double transitionAfterCut = shoulderTransitionHeight(

                    cuttingR,

                    shoulderR,

                    shoulderTaperAngle

                );



                const double maxShoulder = std::max(

                    0.0,

                    totalLength - cuttingLength - transitionAfterCut

                );



                shoulderLength = std::clamp(shoulderLength, 0.0, maxShoulder);

            };



            switch (driver) {



                case GeometryDriver::TotalLength:

                    fitCuttingAndShoulder();

                    break;



                case GeometryDriver::CuttingLength:

                    cuttingLength = std::clamp(cuttingLength, minCutting, maxCutting);

                    shoulderLength = std::min(

                        shoulderLength,

                        std::max(

                            0.0,

                            totalLength - cuttingLength - shoulderTransitionHeight(

                                cuttingR,

                                shoulderR,

                                shoulderTaperAngle

                            )

                        )

                    );

                    break;



                case GeometryDriver::ShoulderLength:

                    shoulderLength = std::max(0.0, shoulderLength);



                    if (cuttingLength + shoulderBlock > totalLength) {

                        cuttingLength = std::max(

                            minCutting,

                            totalLength - shoulderBlock

                        );

                    }



                    cuttingLength = std::clamp(cuttingLength, minCutting, maxCutting);

                    shoulderLength = std::min(

                        shoulderLength,

                        std::max(

                            0.0,

                            totalLength - cuttingLength - shoulderTransitionHeight(

                                cuttingR,

                                shoulderR,

                                shoulderTaperAngle

                            )

                        )

                    );

                    break;



                case GeometryDriver::ShoulderDiameter:

                case GeometryDriver::ShoulderTaper:

                case GeometryDriver::CuttingDiameter:

                case GeometryDriver::CuttingTaper:

                    fitCuttingAndShoulder();

                    break;

            }

        }



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

            tool.cuttingLength = 20.0;

            tool.axis = { 0.0f, 0.0f, 1.0f };



            tool.name = "God Tool " + std::to_string(index);



            return tool;

        }

    };

}


