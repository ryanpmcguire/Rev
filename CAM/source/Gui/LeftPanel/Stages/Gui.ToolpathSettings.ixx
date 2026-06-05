module;

#include <string>
#include <vector>
#include <functional>

export module Cam.Gui.ToolpathSettings;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Dropdown;
import Rev.Element.NumberInput;

import Cam.App;
import Cam.App.Stage;
import Cam.App.ToolPath;
import Cam.App.Tool;
import Cam.App.Slicer.Strategy.Strategies.Bore;
import Cam.App.Slicer.Strategy.Strategies.Profile;
import Cam.App.Slicer.Strategy.Strategies.Hatch;

import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ToolpathSettingsStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct },
            .padding = { .left = 2_px, .right = 4_px, .top = 2_px, .bottom = 4_px }
        };

        Style Row = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct }
        };

        Style Field = {
            .size = { .width = Grow() },
            .margin = { .left = 2_px, .right = 2_px }
        };
    };

    // Inline, compact toolpath settings shown in the stage tree's Toolpath
    // property body. Strategy + tool are committed on selection; the numeric
    // settings recompute the toolpath only when Enter is pressed in them (never
    // on every keystroke), as requested.
    struct ToolpathSettings : public Box {

        Cam::App::AppState* app = nullptr;
        Cam::App::Stage* state = nullptr;
        Cam::App::Stage* boundState = nullptr;

        Dropdown* strategyDropdown = nullptr;
        Dropdown* toolDropdown = nullptr;
        NumberInput* stepDownInput = nullptr;
        NumberInput* stepoverInput = nullptr;
        NumberInput* feedRateInput = nullptr;

        std::function<void(Event&)> onChanged;

        ToolpathSettings(Element* parent, StyleList styles = {})
            : Box(parent, styles, "ToolpathSettings") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&ToolpathSettingsStyle::Self);

            // Strategy + tool
            //--------------------------------------------------

            Box* setupRow = new Box(this, { &ToolpathSettingsStyle::Row }, "ToolpathSetupRow");

            strategyDropdown = new Dropdown(
                setupRow,
                {
                    .label = "Strategy",
                    .options = {
                        { "Hatch",   Cam::App::Slicer::Strategy::Strategies::Hatch::name() },
                        { "Profile", Cam::App::Slicer::Strategy::Strategies::Profile::name() },
                        { "Bore",    Cam::App::Slicer::Strategy::Strategies::Bore::name() }
                    },
                    .placeholder = "Strategy",
                    .value = ""
                },
                { &ToolpathSettingsStyle::Field }
            );

            strategyDropdown->onChange = [this](Event& e) {
                recalc(e);
            };

            toolDropdown = new Dropdown(
                setupRow,
                {
                    .label = "Tool",
                    .options = toolOptions(),
                    .placeholder = "Tool",
                    .value = ""
                },
                { &ToolpathSettingsStyle::Field }
            );

            toolDropdown->onChange = [this](Event& e) {
                recalc(e);
            };

            // Strategy settings (recompute on Enter only)
            //--------------------------------------------------

            Box* machiningRow = new Box(this, { &ToolpathSettingsStyle::Row }, "ToolpathMachiningRow");

            stepDownInput  = makeNumberInput(machiningRow, "Stepdown (mm)", "0.5");
            stepoverInput  = makeNumberInput(machiningRow, "Stepover (% dia.)", "25");

            Box* feedRow = new Box(this, { &ToolpathSettingsStyle::Row }, "ToolpathFeedRow");

            feedRateInput  = makeNumberInput(feedRow, "Feed rate (mm/min)", "250");
        }

        // Build a number input wired to recompute the toolpath when Enter is
        // pressed inside it (not on content change).
        NumberInput* makeNumberInput(
            Element* parent,
            const std::string& label,
            const std::string& placeholder
        ) {
            NumberInput::Params params;
            params.label = label;
            params.placeholder = placeholder;
            params.maxLength = 32;
            params.selectAllOnFocus = true;
            params.allowNegative = false;
            params.allowDecimal = true;
            params.allowEmpty = false;
            params.maxDecimalPlaces = 4;

            NumberInput* input = new NumberInput(
                parent, params, { &ToolpathSettingsStyle::Field }
            );

            input->onKeyDown([this](Event& e) {
                if (e.keyboard.enter) {
                    recalc(e);
                }
            });

            return input;
        }

        std::vector<Dropdown::Item> toolOptions() const {

            std::vector<Dropdown::Item> items;

            if (!app) { return items; }

            for (size_t i = 0; i < app->toolCount(); i++) {

                Cam::App::Tool* tool = app->toolAt(i);

                if (!tool) { continue; }

                items.push_back({
                    "#" + std::to_string(i + 1) + "  " + tool->name,
                    tool->name
                });
            }

            if (items.empty()) {
                items.push_back({ "No tools", "" });
            }

            return items;
        }

        // Point the panel at a stage. Field values are (re)loaded only when the
        // stage actually changes, so the user's in-progress edits aren't clobbered.
        void setState(Cam::App::Stage* stage) {

            state = stage;

            if (state == boundState) { return; }

            boundState = state;
            populate();
        }

        void populate() {

            if (!state) { return; }

            const Cam::App::ToolPath& tp = state->toolPath;

            if (strategyDropdown) { strategyDropdown->params.value = tp.strategy; }

            if (toolDropdown) {
                toolDropdown->params.options = toolOptions();
                toolDropdown->params.value = tp.toolName;
            }

            if (stepDownInput) { stepDownInput->setValue(tp.stepDown); }
            if (stepoverInput) { stepoverInput->setValue(tp.stepover * 100.0); }
            if (feedRateInput) { feedRateInput->setValue(tp.feedRate); }
        }

        // Read the form and recompute the toolpath. Settings not exposed inline
        // (cut direction, rapid speed, link retract) are preserved from the
        // stage's existing toolpath so we don't clobber them.
        void recalc(Event& e) {

            if (!app || !state) { return; }

            stepDownInput->commit(e);
            stepoverInput->commit(e);
            feedRateInput->commit(e);

            const Cam::App::ToolPath& tp = state->toolPath;

            std::string strategy = strategyDropdown->params.value;
            if (strategy.empty()) { strategy = tp.strategy; }

            const std::string toolName = toolDropdown->params.value;

            const double stepDown      = stepDownInput->valueOr(tp.stepDown);
            const double stepoverPct   = stepoverInput->valueOr(tp.stepover * 100.0);
            const double feedRate      = feedRateInput->valueOr(tp.feedRate);

            if (toolName.empty() || stepoverPct <= 0.0) { return; }

            if (!app->saveToolPathSettings(
                state,
                strategy,
                toolName,
                stepDown,
                stepoverPct / 100.0,
                feedRate,
                tp.rapidSpeedMmPerSec,
                tp.climbMilling,
                tp.linkRetractDistance
            )) {
                return;
            }

            if (onChanged) { onChanged(e); }
        }
    };
}
