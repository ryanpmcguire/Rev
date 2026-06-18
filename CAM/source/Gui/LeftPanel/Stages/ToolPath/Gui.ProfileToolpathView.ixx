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
        Dropdown* finishProfileDropdown = nullptr;
        NumberInput* finishWidthInput = nullptr;
        NumberInput* finishStepdownInput = nullptr;
        NumberInput* finishFeedInput = nullptr;
        NumberInput* finishSpindleInput = nullptr;
        NumberInput* leadSlopeInput = nullptr;
        NumberInput* leadFeedInput = nullptr;

        ProfileToolpathView(Element* parent) : ToolpathStrategyView(parent) {

            buildCommon();

            Box* orderRow = new Box(this, { &ToolpathViewStyle::Row }, "ProfileRingOrderRow");
            ringOrderDropdown = makeRingOrderDropdown(orderRow);

            Box* checkRow = new Box(this, { &ToolpathViewStyle::CheckRow }, "ProfileClimbRow");
            climbCheckbox = makeClimbCheckbox(checkRow);

            // Lead-in/out ramp: slope (deg) and its own gentle feed.
            Box* leadRow = new Box(this, { &ToolpathViewStyle::Row }, "ProfileLeadRow");
            leadSlopeInput = makeNumberInput(leadRow, "Lead slope (deg)", "30");
            leadFeedInput  = makeNumberInput(leadRow, "Lead feed (mm/min)", "200");

            // Finishing pass: a finishing PROFILE drives its feed / stepdown /
            // spindle (mirroring the roughing profile); skin width stays a manual
            // geometric setting.
            Box* finishProfileRow = new Box(this, { &ToolpathViewStyle::Row }, "ProfileFinishProfileRow");
            finishProfileDropdown = new Dropdown(
                finishProfileRow,
                { .label = "Finish profile", .options = profileOptions(), .placeholder = "Finish profile", .value = "" },
                { &ToolpathViewStyle::Field }
            );
            finishProfileDropdown->onChange = [this](Event& e) { commit(e); };

            // Finishing pass: enabled by the toggle; skin width + final floor
            // stepdown (both mm), and its own feed + spindle.
            Box* finishRow = new Box(this, { &ToolpathViewStyle::Row }, "ProfileFinishRow");
            finishWidthInput = makeNumberInput(finishRow, "Finish skin (mm)", "0.2");
            finishStepdownInput = makeNumberInput(finishRow, "Finish stepdown (mm)", "0.25");

            Box* finishFeedRow = new Box(this, { &ToolpathViewStyle::Row }, "ProfileFinishFeedRow");
            finishFeedInput = makeNumberInput(finishFeedRow, "Finish feed (mm/min)", "150");
            finishSpindleInput = makeNumberInput(finishFeedRow, "Finish spindle (RPM)", "12000");

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
            if (finishProfileDropdown) {
                finishProfileDropdown->params.options = profileOptions();
                finishProfileDropdown->params.value = state->toolPath.finishProfileName;
            }
            if (finishWidthInput) { finishWidthInput->setValue(state->toolPath.finishWidth); }
            if (finishStepdownInput) { finishStepdownInput->setValue(state->toolPath.finishStepdown); }
            if (finishFeedInput) { finishFeedInput->setValue(state->toolPath.finishFeedRate); }
            if (finishSpindleInput) { finishSpindleInput->setValue(state->toolPath.finishSpindleSpeed); }
            if (leadSlopeInput) { leadSlopeInput->setValue(state->toolPath.leadSlope); }
            if (leadFeedInput) { leadFeedInput->setValue(state->toolPath.leadFeedRate); }
        }

        void readExtras(Event& e, double& stepover, bool& climb, bool& insideOut) override {
            if (climbCheckbox) { climb = climbCheckbox->value; }
            if (ringOrderDropdown) { insideOut = (ringOrderDropdown->params.value != "outside_in"); }

            // These aren't part of saveToolPathSettings' fixed signature, so write
            // them straight onto the toolpath (the save preserves any field it isn't
            // given, then recomputes).
            if (!state) { return; }
            auto readInto = [&](NumberInput* in, double& dst) {
                if (!in) { return; }
                in->commit(e);
                dst = in->valueOr(dst);
            };
            if (finishCheckbox) { state->toolPath.finishPass = finishCheckbox->value; }
            if (finishProfileDropdown) {
                state->toolPath.finishProfileName = finishProfileDropdown->params.value;
                finishProfileDropdown->params.options = profileOptions();
            }
            readInto(finishWidthInput, state->toolPath.finishWidth);
            readInto(finishStepdownInput, state->toolPath.finishStepdown);
            readInto(finishFeedInput, state->toolPath.finishFeedRate);
            readInto(finishSpindleInput, state->toolPath.finishSpindleSpeed);
            readInto(leadSlopeInput, state->toolPath.leadSlope);
            readInto(leadFeedInput, state->toolPath.leadFeedRate);
        }
    };
}
