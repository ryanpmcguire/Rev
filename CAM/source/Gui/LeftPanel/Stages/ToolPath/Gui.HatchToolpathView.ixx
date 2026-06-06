module;

#include <string>

export module Cam.Gui.HatchToolpathView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Box;
import Rev.Element.NumberInput;
import Rev.Element.Checkbox;

import Cam.App.Stage;
import Cam.App.ToolPath;
import Cam.App.Slicer.Strategy.Strategies.Hatch;

import Cam.Gui.ToolpathStrategyView;

export namespace Cam::Gui {

    using namespace Rev::Element;

    // Hatch (raster area-clearing): exposes a stepover (raster spacing) and a
    // climb/conventional toggle in addition to the common controls.
    struct HatchToolpathView : public ToolpathStrategyView {

        NumberInput* stepoverInput = nullptr;
        Checkbox* climbCheckbox = nullptr;

        HatchToolpathView(Element* parent) : ToolpathStrategyView(parent) {

            buildCommon();

            Box* row = new Box(this, { &ToolpathViewStyle::Row }, "HatchStepoverRow");
            stepoverInput = makeNumberInput(row, "Stepover (% dia.)", "25");

            Box* checkRow = new Box(this, { &ToolpathViewStyle::CheckRow }, "HatchClimbRow");
            climbCheckbox = makeClimbCheckbox(checkRow);
        }

        std::string strategyName() const override {
            return Cam::App::Slicer::Strategy::Strategies::Hatch::name();
        }

        void populateExtras() override {
            if (!state) { return; }
            const Cam::App::ToolPath& tp = state->toolPath;
            if (stepoverInput)  { stepoverInput->setValue(tp.stepover * 100.0); }
            if (climbCheckbox)  { climbCheckbox->value = tp.climbMilling; }
        }

        void readExtras(Event& e, double& stepover, bool& climb) override {
            if (stepoverInput) {
                stepoverInput->commit(e);
                const double pct = stepoverInput->valueOr(stepover * 100.0);
                if (pct > 0.0) { stepover = pct / 100.0; }
            }
            if (climbCheckbox) { climb = climbCheckbox->value; }
        }
    };
}
