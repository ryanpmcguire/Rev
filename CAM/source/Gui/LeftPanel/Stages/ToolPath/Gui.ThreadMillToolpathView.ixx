module;

#include <string>
#include <cmath>

export module Cam.Gui.ThreadMillToolpathView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Box;
import Rev.Element.NumberInput;
import Rev.Element.Checkbox;
import Rev.Element.Dropdown;

import Cam.App.Stage;
import Cam.App.ToolPath;
import Cam.App.Slicer.Strategy.Strategies.ThreadMill;

import Cam.Gui.ToolpathStrategyView;

export namespace Cam::Gui {

    using namespace Rev::Element;

    // Thread milling is the one strategy that uses TWO tools: a rough tool (the
    // common "Tool" dropdown) to open the bore, and a fine tool to cut the
    // dimensionally-critical thread wall.  It also exposes the thread callout
    // that turns a bore into a thread: pitch, radial passes, and cut direction.
    struct ThreadMillToolpathView : public ToolpathStrategyView {

        Dropdown*    fineToolDropdown  = nullptr;
        NumberInput* pitchInput        = nullptr;
        NumberInput* passesInput       = nullptr;
        Dropdown*    directionDropdown = nullptr;
        Checkbox*    climbCheckbox     = nullptr;

        ThreadMillToolpathView(Element* parent) : ToolpathStrategyView(parent) {

            buildCommon();   // rough tool, stepdown, feed, retract

            // The finish tool: the dimensionally-critical thread-wall pass.
            Box* fineRow = new Box(this, { &ToolpathViewStyle::Row }, "TMFineToolRow");
            fineToolDropdown = new Dropdown(
                fineRow,
                { .label = "Fine tool", .options = toolOptions(), .placeholder = "Fine tool", .value = "" },
                { &ToolpathViewStyle::Field }
            );
            fineToolDropdown->onChange = [this](Event& e) { commit(e); };

            Box* threadRow = new Box(this, { &ToolpathViewStyle::Row }, "TMThreadRow");
            pitchInput  = makeNumberInput(threadRow, "Pitch (mm)", "0.4");
            passesInput = makeNumberInput(threadRow, "Passes", "1");

            Box* dirRow = new Box(this, { &ToolpathViewStyle::Row }, "TMDirRow");
            directionDropdown = new Dropdown(
                dirRow,
                {
                    .label = "Cut",
                    .options = { { "Up cut", "up" }, { "Down cut", "down" } },
                    .placeholder = "Cut",
                    .value = "up"
                },
                { &ToolpathViewStyle::Field }
            );
            directionDropdown->onChange = [this](Event& e) { commit(e); };

            Box* checkRow = new Box(this, { &ToolpathViewStyle::CheckRow }, "TMClimbRow");
            climbCheckbox = makeClimbCheckbox(checkRow);
        }

        std::string strategyName() const override {
            return Cam::App::Slicer::Strategy::Strategies::ThreadMill::name();
        }

        void populateExtras() override {
            if (!state) { return; }

            const Cam::App::ToolPath& tp = state->toolPath;

            if (fineToolDropdown) {
                fineToolDropdown->params.options = toolOptions();
                fineToolDropdown->params.value = tp.fineToolName;
            }
            if (pitchInput)  { pitchInput->setValue(tp.threadPitch); }
            if (passesInput) { passesInput->setValue(double(tp.threadPasses)); }
            if (directionDropdown) {
                directionDropdown->params.value = tp.threadUpCut ? "up" : "down";
            }
            if (climbCheckbox) { climbCheckbox->value = tp.climbMilling; }
        }

        // Thread-specific fields are written straight onto the toolpath -- the
        // shared save carries only the common settings, and leaves these intact
        // (so they survive the recompute that save triggers).  The rough tool +
        // climb flow through the base commit as usual.
        void readExtras(Event& e, double& stepover, bool& climb, bool& insideOut) override {

            if (climbCheckbox) { climb = climbCheckbox->value; }

            if (!state) { return; }

            if (pitchInput) {
                pitchInput->commit(e);
                double p = 0.0;
                if (pitchInput->tryGetValue(p) && p > 0.0) { state->toolPath.threadPitch = p; }
            }

            if (passesInput) {
                passesInput->commit(e);
                double n = 0.0;
                if (passesInput->tryGetValue(n) && n >= 1.0) {
                    state->toolPath.threadPasses = static_cast<int>(std::lround(n));
                }
            }

            if (directionDropdown) {
                state->toolPath.threadUpCut = (directionDropdown->params.value != "down");
            }

            if (fineToolDropdown) {
                state->toolPath.fineToolName = fineToolDropdown->params.value;
            }
        }
    };
}
