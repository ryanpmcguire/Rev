module;

#include <string>

export module Cam.Gui.ProfileToolpathView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Box;
import Rev.Element.Checkbox;
import Rev.Element.Dropdown;

import Cam.App.Stage;
import Cam.App.ToolPath;
import Cam.App.Slicer.Strategy.Strategies.Profile;

import Cam.Gui.ToolpathStrategyView;

export namespace Cam::Gui {

    using namespace Rev::Element;

    // Profile (contour following): no stepover — the tool tracks the contour — but
    // cut direction (climb/conventional) matters, so it exposes that toggle.
    struct ProfileToolpathView : public ToolpathStrategyView {

        Checkbox* climbCheckbox = nullptr;
        Dropdown* ringOrderDropdown = nullptr;

        ProfileToolpathView(Element* parent) : ToolpathStrategyView(parent) {

            buildCommon();

            Box* orderRow = new Box(this, { &ToolpathViewStyle::Row }, "ProfileRingOrderRow");
            ringOrderDropdown = makeRingOrderDropdown(orderRow);

            Box* checkRow = new Box(this, { &ToolpathViewStyle::CheckRow }, "ProfileClimbRow");
            climbCheckbox = makeClimbCheckbox(checkRow);
        }

        std::string strategyName() const override {
            return Cam::App::Slicer::Strategy::Strategies::Profile::name();
        }

        void populateExtras() override {
            if (!state) { return; }
            if (climbCheckbox) { climbCheckbox->value = state->toolPath.climbMilling; }
            if (ringOrderDropdown) {
                ringOrderDropdown->params.value = state->toolPath.insideOut ? "inside_out" : "outside_in";
            }
        }

        void readExtras(Event& e, double& stepover, bool& climb, bool& insideOut) override {
            if (climbCheckbox) { climb = climbCheckbox->value; }
            if (ringOrderDropdown) { insideOut = (ringOrderDropdown->params.value != "outside_in"); }
        }
    };
}
