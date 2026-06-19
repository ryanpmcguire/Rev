module;

#include <string>

export module Cam.Gui.ChamferToolpathView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Checkbox;

import Cam.App.Stage;
import Cam.App.ToolPath;
import Cam.App.Slicer.Strategy.Strategies.Chamfer;

import Cam.Gui.Theme;
import Cam.Gui.ToolpathStrategyView;

export namespace Cam::Gui {

    using namespace Rev::Element;

    // The chamfer toolpath: a multi-pass stepped contour cut with a chamfer bit.
    // The bevel ANGLE is owned by the operation (inferred from the selected face)
    // and shown here read-only; the view carries the machining choices (tool /
    // profile / feed / stepdown via the common controls, plus climb).
    struct ChamferToolpathView : public ToolpathStrategyView {

        Text*     calloutSummary = nullptr;
        Checkbox* climbCheckbox  = nullptr;

        ChamferToolpathView(Element* parent) : ToolpathStrategyView(parent) {

            buildCommon();   // tool, profile, stepdown, feed, retract

            Box* calloutRow = new Box(this, { &ToolpathViewStyle::Row }, "ChamferCalloutRow");
            calloutSummary = new Text(
                calloutRow, "Chamfer: --",
                Theme::layer({ &ToolpathViewStyle::Field }, { &Theme::Styles::MutedText })
            );

            Box* checkRow = new Box(this, { &ToolpathViewStyle::CheckRow }, "ChamferClimbRow");
            climbCheckbox = makeClimbCheckbox(checkRow);
        }

        std::string strategyName() const override {
            return Cam::App::Slicer::Strategy::Strategies::Chamfer::name();
        }

        static std::string calloutText(const Cam::App::ToolPath& tp) {
            std::string s = std::to_string(tp.chamferAngle);
            while (s.size() > 1 && s.back() == '0') { s.pop_back(); }
            if (!s.empty() && s.back() == '.') { s.pop_back(); }
            return "Chamfer: " + s + " deg";
        }

        void populateExtras() override {
            if (!state) { return; }
            if (calloutSummary) { calloutSummary->setContent(calloutText(state->toolPath)); }
            if (climbCheckbox) { climbCheckbox->value = state->toolPath.climbMilling; }
        }

        void readExtras(Event& e, double& stepover, bool& climb, bool& insideOut) override {
            if (climbCheckbox) { climb = climbCheckbox->value; }
        }
    };
}
