module;

#include <string>

export module Cam.Gui.BoreToolpathView;

import Rev.Element;

import Cam.App.Slicer.Strategy.Strategies.Bore;

import Cam.Gui.ToolpathStrategyView;

export namespace Cam::Gui {

    using namespace Rev::Element;

    // Bore (helical/plunge boring of a round pocket): driven entirely by the tool
    // diameter and depth, so it shows only the common controls (tool, stepdown,
    // feed). No stepover or cut-direction choice.
    struct BoreToolpathView : public ToolpathStrategyView {

        BoreToolpathView(Element* parent) : ToolpathStrategyView(parent) {
            buildCommon();
        }

        std::string strategyName() const override {
            return Cam::App::Slicer::Strategy::Strategies::Bore::name();
        }
    };
}
