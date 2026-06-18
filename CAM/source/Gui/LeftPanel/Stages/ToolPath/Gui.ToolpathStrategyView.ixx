module;

#include <string>
#include <vector>
#include <functional>
#include <optional>

export module Cam.Gui.ToolpathStrategyView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Dropdown;
import Rev.Element.NumberInput;
import Rev.Element.Checkbox;

import Cam.App;
import Cam.App.Stage;
import Cam.App.ToolPath;
import Cam.App.Tool;

import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolpathViewStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct }
        };

        Style Row = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct }
        };

        Style Field = {
            .size = { .width = Grow() },
            .margin = { .left = 2_px, .right = 2_px }
        };

        Style CheckRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .margin = { .left = 2_px, .top = 2_px }
        };
    }

    // Base class for a toolpath strategy's settings menu. Each strategy (profile,
    // hatch, bore, ...) subclasses this and presents the controls appropriate to
    // it. The common controls (tool, stepdown, feed rate) are built by buildCommon;
    // subclasses add their own extras and override the populate/read hooks.
    struct ToolpathStrategyView : public Box {

        Cam::App::AppState* app = nullptr;
        Cam::App::Stage* state = nullptr;
        Cam::App::Stage* boundState = nullptr;

        Dropdown* toolDropdown = nullptr;
        NumberInput* stepDownInput = nullptr;
        NumberInput* feedRateInput = nullptr;
        NumberInput* retractHeightInput = nullptr;

        std::function<void(Event&)> onChanged;

        ToolpathStrategyView(Element* parent)
            : Box(parent, { &ToolpathViewStyle::Self }, "ToolpathStrategyView") {
            app = Cam::App::AppState::Get(shared->state);
        }

        // The strategy this menu edits (its serialized name).
        virtual std::string strategyName() const = 0;

        // Build the controls every strategy shares. Subclasses call this first in
        // their constructor, then append their own controls.  `withStepDown` is
        // false for strategies where an axial stepdown is meaningless (thread
        // milling, whose axial step IS the pitch).
        void buildCommon(bool withStepDown = true) {

            Box* toolRow = new Box(this, { &ToolpathViewStyle::Row }, "ToolpathToolRow");

            toolDropdown = new Dropdown(
                toolRow,
                { .label = "Tool", .options = toolOptions(), .placeholder = "Tool", .value = "" },
                { &ToolpathViewStyle::Field }
            );
            toolDropdown->onChange = [this](Event& e) { commit(e); };

            Box* machiningRow = new Box(this, { &ToolpathViewStyle::Row }, "ToolpathMachiningRow");

            if (withStepDown) {
                stepDownInput = makeNumberInput(machiningRow, "Stepdown (mm)", "0.5");
            }
            feedRateInput = makeNumberInput(machiningRow, "Feed rate (mm/min)", "250");

            // Retract height applies to EVERY strategy: the clearance plane
            // for retracts/rapids, measured above the feature's top surface.
            Box* retractRow = new Box(this, { &ToolpathViewStyle::Row }, "ToolpathRetractRow");

            retractHeightInput = makeNumberInput(retractRow, "Retract height (mm)", "2");
        }

        // A number input that recomputes the toolpath when Enter is pressed in it
        // (not on every keystroke).
        NumberInput* makeNumberInput(Element* parent, const std::string& label, const std::string& placeholder) {

            NumberInput::Params params;
            params.label = label;
            params.placeholder = placeholder;
            params.maxLength = 32;
            params.selectAllOnFocus = true;
            params.allowNegative = false;
            params.allowDecimal = true;
            params.allowEmpty = false;
            params.maxDecimalPlaces = 4;

            NumberInput* input = new NumberInput(parent, params, { &ToolpathViewStyle::Field });

            input->onKeyDown([this](Event& e) {
                if (e.keyboard.enter) { commit(e); }
            });

            // Live: recompute as the value changes, not only on Enter. (This is the
            // commit path every edit shares; without it the tree-item fields only
            // refreshed on Enter, which read as "settings don't update live".)
            input->onValueChange = [this](Event& e, std::optional<double>) { commit(e); };

            return input;
        }

        // A climb/conventional milling checkbox that commits on toggle.
        Checkbox* makeClimbCheckbox(Element* parent) {

            Checkbox* box = new Checkbox(parent, { .label = "Climb milling", .def = true });

            // The inner toggle runs first (flips value); our listener then commits
            // with the new value.
            box->checkbox->onClick([this](Event& e) { commit(e); });

            return box;
        }

        // Ring order dropdown: "Inside out" (innermost ring first) vs
        // "Outside in".  Stored as the toolpath's insideOut bool, forwarded to
        // the slice strategy's reverse flag.
        Dropdown* makeRingOrderDropdown(Element* parent) {

            Dropdown* dd = new Dropdown(
                parent,
                {
                    .label = "Ring order",
                    .options = {
                        { "Inside out", "inside_out" },
                        { "Outside in", "outside_in" }
                    },
                    .placeholder = "Ring order",
                    .value = "inside_out"
                },
                { &ToolpathViewStyle::Field }
            );

            dd->onChange = [this](Event& e) { commit(e); };

            return dd;
        }

        // Whether `tool` can physically perform the operation this view edits.
        // Default: any cutter qualifies (a probe cannot cut, so it is rejected).
        // Strategy subclasses tighten this using the tool's implied capabilities
        // (e.g. thread milling needs a thread mill whose pitch range fits the
        // callout).  Tools that fail are shown greyed-out in the dropdown.
        // The toolpath itself is the single authority on whether a tool fits the
        // operation (capability + feature requirements); the dropdown greys out
        // whatever it rejects.
        virtual bool toolCanPerform(const Cam::App::Tool& tool) const {
            if (!state) { return true; }
            return state->toolPath.accepts(tool);
        }

        std::vector<Dropdown::Item> toolOptions() const {

            std::vector<Dropdown::Item> items;
            if (!app) { return items; }

            for (size_t i = 0; i < app->toolCount(); i++) {
                Cam::App::Tool* tool = app->toolAt(i);
                if (!tool) { continue; }
                Dropdown::Item item;
                item.name = "#" + std::to_string(i + 1) + "  " + tool->name;
                item.value = tool->name;
                item.disabled = !toolCanPerform(*tool);
                items.push_back(item);
            }

            if (items.empty()) { items.push_back({ "No tools", "" }); }
            return items;
        }

        void setState(Cam::App::Stage* stage) {
            state = stage;
            if (state == boundState) { return; }
            boundState = state;
            populate();
        }

        void populate() {
            if (!state) { return; }

            // The feature's tool requirements are derived from the strategy slices
            // at compute() and persisted with the project, so they are available
            // here (in-session and on load) to disable ill-fitting tools.
            const Cam::App::ToolPath& tp = state->toolPath;

            if (toolDropdown) {
                toolDropdown->params.options = toolOptions();
                toolDropdown->params.value = tp.toolName;
            }
            if (stepDownInput) { stepDownInput->setValue(tp.stepDown); }
            if (feedRateInput) { feedRateInput->setValue(tp.feedRate); }
            if (retractHeightInput) { retractHeightInput->setValue(double(tp.retractHeight)); }

            populateExtras();
        }

        // Subclass hooks: load/read the strategy-specific controls. Defaults keep
        // the existing toolpath values (so non-exposed settings aren't clobbered).
        virtual void populateExtras() {}
        virtual void readExtras(Event& e, double& stepover, bool& climb, bool& insideOut) {}

        // Read the form and save + recompute the toolpath.
        void commit(Event& e) {
            if (!app || !state) { return; }

            if (stepDownInput) { stepDownInput->commit(e); }
            if (feedRateInput) { feedRateInput->commit(e); }
            if (retractHeightInput) { retractHeightInput->commit(e); }

            const Cam::App::ToolPath& tp = state->toolPath;

            const std::string tool = toolDropdown ? toolDropdown->params.value : tp.toolName;
            const double stepDown  = stepDownInput ? stepDownInput->valueOr(tp.stepDown) : tp.stepDown;
            const double feedRate  = feedRateInput ? feedRateInput->valueOr(tp.feedRate) : tp.feedRate;
            const double retract   = retractHeightInput
                ? retractHeightInput->valueOr(double(tp.retractHeight))
                : double(tp.retractHeight);

            double stepover  = tp.stepover;     // kept unless a strategy exposes it
            bool   climb     = tp.climbMilling; // kept unless a strategy exposes it
            bool   insideOut = tp.insideOut;    // kept unless a strategy exposes it
            readExtras(e, stepover, climb, insideOut);

            if (tool.empty() || stepover <= 0.0) { return; }

            if (!app->saveToolPathSettings(
                state, strategyName(), tool,
                stepDown, stepover, feedRate,
                tp.rapidSpeedMmPerSec, climb, static_cast<float>(retract), insideOut
            )) {
                return;
            }

            if (onChanged) { onChanged(e); }
        }
    };
}
