module;

#include <string>

export module Cam.Gui.ProfileToolpathView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Box;
import Rev.Element.Checkbox;
import Rev.Element.Dropdown;
import Rev.Element.NumberInput;

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
        Checkbox* finishCheckbox = nullptr;
        NumberInput* finishWidthInput = nullptr;

        ProfileToolpathView(Element* parent) : ToolpathStrategyView(parent) {

            buildCommon();

            Box* orderRow = new Box(this, { &ToolpathViewStyle::Row }, "ProfileRingOrderRow");
            ringOrderDropdown = makeRingOrderDropdown(orderRow);

            Box* checkRow = new Box(this, { &ToolpathViewStyle::CheckRow }, "ProfileClimbRow");
            climbCheckbox = makeClimbCheckbox(checkRow);

            // Finishing pass: a thin extra ring after the boundary clearance pass,
            // with its width (fraction of the tool radius) beside the toggle.
            Box* finishRow = new Box(this, { &ToolpathViewStyle::Row }, "ProfileFinishRow");
            finishWidthInput = makeNumberInput(finishRow, "Finish width (xR)", "0.1");

            Box* finishCheckRow = new Box(this, { &ToolpathViewStyle::CheckRow }, "ProfileFinishCheckRow");
            finishCheckbox = new Checkbox(finishCheckRow, { .label = "Finishing pass", .def = true });
            finishCheckbox->checkbox->onClick([this](Event& e) { commit(e); });
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
            if (finishCheckbox) { finishCheckbox->value = state->toolPath.finishPass; }
            if (finishWidthInput) { finishWidthInput->setValue(state->toolPath.finishWidth); }
        }

        void readExtras(Event& e, double& stepover, bool& climb, bool& insideOut) override {
            if (climbCheckbox) { climb = climbCheckbox->value; }
            if (ringOrderDropdown) { insideOut = (ringOrderDropdown->params.value != "outside_in"); }

            // finishPass / finishWidth aren't part of saveToolPathSettings' fixed
            // signature, so write them straight onto the toolpath (the save call
            // below preserves any field it isn't given, then recomputes).
            if (state && finishCheckbox) { state->toolPath.finishPass = finishCheckbox->value; }
            if (state && finishWidthInput) {
                finishWidthInput->commit(e);
                state->toolPath.finishWidth = finishWidthInput->valueOr(state->toolPath.finishWidth);
            }
        }
    };
}
