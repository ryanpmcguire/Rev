module;

#include <string>
#include <cmath>

export module Cam.Gui.ThreadMillToolpathView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.NumberInput;
import Rev.Element.Checkbox;
import Rev.Element.Dropdown;

import Cam.App.Stage;
import Cam.App.Tool;
import Cam.App.ToolPath;
import Cam.App.Slicer.Strategy.Strategies.ThreadMill;

import Cam.Gui.Theme;
import Cam.Gui.ToolpathStrategyView;

export namespace Cam::Gui {

    using namespace Rev::Element;

    // The thread-mill toolpath is JUST the threading pass: one tool, cutting the
    // thread the OPERATION's callout specifies (the bore is a prior step).  So
    // this view carries only machining choices -- radial passes, cut direction,
    // climb -- over the common tool/feed controls.  The callout (major / pitch /
    // pre-bore) is edited in the operation's feature view and shown here read-only.
    struct ThreadMillToolpathView : public ToolpathStrategyView {

        Text*        calloutSummary   = nullptr;
        NumberInput* passesInput      = nullptr;
        Dropdown*    directionDropdown = nullptr;
        Checkbox*    climbCheckbox     = nullptr;

        ThreadMillToolpathView(Element* parent) : ToolpathStrategyView(parent) {

            buildCommon(false);   // tool, feed, retract -- no stepdown (the pitch is the axial step)

            // The thread the operation specifies (read-only here -- edit it in the
            // feature step).
            Box* calloutRow = new Box(this, { &ToolpathViewStyle::Row }, "TMCalloutRow");
            calloutSummary = new Text(
                calloutRow, "Thread: --",
                Theme::layer({ &ToolpathViewStyle::Field }, { &Theme::Styles::MutedText })
            );

            Box* machineRow = new Box(this, { &ToolpathViewStyle::Row }, "TMMachineRow");
            passesInput = makeNumberInput(machineRow, "Passes", "1");
            directionDropdown = new Dropdown(
                machineRow,
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

        // Only a thread mill whose pitch range covers the callout AND whose crest
        // fits inside the thread's major diameter can run this operation.
        bool toolCanPerform(const Cam::App::Tool& tool) const override {

            if (!tool.implied.canMillThreads) { return false; }
            if (!toolFitsFeature(tool)) { return false; }   // reach the threaded depth
            if (!state) { return true; }

            const Cam::App::ToolPath& tp = state->toolPath;

            if (tp.threadPitch > 0.0 && !tool.canCutThreadPitch(tp.threadPitch)) {
                return false;
            }
            // The cutter must orbit inside the bore: crest diameter below the major.
            if (tp.threadMajorDiameter > 0.0 && tool.diameter >= tp.threadMajorDiameter) {
                return false;
            }
            return true;
        }

        // A human-readable callout, e.g. "Thread: M2.5 x 0.45  (pre-bore 2.05 mm)".
        static std::string calloutText(const Cam::App::ToolPath& tp) {
            auto trim = [](double v) {
                std::string s = std::to_string(v);
                while (s.size() > 1 && s.back() == '0') { s.pop_back(); }
                if (!s.empty() && s.back() == '.') { s.pop_back(); }
                return s;
            };
            return "Thread: " + trim(tp.threadMajorDiameter) + " x " + trim(tp.threadPitch)
                 + "  (pre-bore " + trim(tp.threadPreBore) + " mm)";
        }

        void populateExtras() override {
            if (!state) { return; }

            const Cam::App::ToolPath& tp = state->toolPath;

            if (calloutSummary) { calloutSummary->setContent(calloutText(tp)); }
            if (passesInput)    { passesInput->setValue(double(tp.threadPasses)); }
            if (directionDropdown) {
                directionDropdown->params.value = tp.threadUpCut ? "up" : "down";
            }
            if (climbCheckbox) { climbCheckbox->value = tp.climbMilling; }
        }

        // Only machining choices are written here; the callout is owned by the
        // operation and mirrored onto the toolpath on recompute.
        void readExtras(Event& e, double& stepover, bool& climb, bool& insideOut) override {

            if (climbCheckbox) { climb = climbCheckbox->value; }

            if (!state) { return; }

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
        }
    };
}
