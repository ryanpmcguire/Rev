module;

#include <string>
#include <functional>

export module Cam.Gui.ToolpathSettings;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Dropdown;

import Cam.App;
import Cam.App.Stage;
import Cam.App.ToolPath;
import Cam.App.Slicer.Strategy.Strategies.Bore;
import Cam.App.Slicer.Strategy.Strategies.Profile;
import Cam.App.Slicer.Strategy.Strategies.Hatch;

import Cam.Gui.Theme;
import Cam.Gui.ToolpathStrategyView;
import Cam.Gui.ProfileToolpathView;
import Cam.Gui.HatchToolpathView;
import Cam.Gui.BoreToolpathView;

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

        Style Body = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct }
        };
    };

    // Inline toolpath settings shown in the stage tree's Toolpath property body.
    // This is now just a host: a strategy selector plus a per-strategy settings
    // view (profile / hatch / bore each get their own menu). Picking a strategy
    // swaps the menu and commits the change.
    struct ToolpathSettings : public Box {

        Cam::App::AppState* app = nullptr;
        Cam::App::Stage* state = nullptr;
        Cam::App::Stage* boundState = nullptr;

        Dropdown* strategyDropdown = nullptr;
        Box* strategyBody = nullptr;

        ToolpathStrategyView* strategyView = nullptr;
        std::string viewStrategy;

        std::function<void(Event&)> onChanged;

        ToolpathSettings(Element* parent, StyleList styles = {})
            : Box(parent, styles, "ToolpathSettings") {

            app = Cam::App::AppState::Get(shared->state);

            this->styles.add(&ToolpathSettingsStyle::Self);

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

            strategyDropdown->onChange = [this](Event& e) { onStrategyChanged(e); };

            // The per-strategy settings menu lives here.
            strategyBody = new Box(this, { &ToolpathSettingsStyle::Body }, "ToolpathStrategyBody");
        }

        // Create the settings view for a strategy name.
        ToolpathStrategyView* makeStrategyView(Element* parent, const std::string& strategy) {

            using namespace Cam::App::Slicer::Strategy::Strategies;

            if (strategy == Profile::name()) { return new ProfileToolpathView(parent); }
            if (strategy == Bore::name())    { return new BoreToolpathView(parent); }
            return new HatchToolpathView(parent);   // default
        }

        // Ensure strategyView matches the given strategy, rebuilding if needed.
        void ensureView(const std::string& strategyIn) {

            std::string strategy = strategyIn;
            if (strategy.empty()) {
                strategy = Cam::App::Slicer::Strategy::Strategies::Hatch::name();
            }

            if (strategyView && viewStrategy == strategy) { return; }

            if (strategyView) { delete strategyView; strategyView = nullptr; }

            strategyView = makeStrategyView(strategyBody, strategy);
            viewStrategy = strategy;

            strategyView->onChanged = [this](Event& e) {
                if (onChanged) { onChanged(e); }
            };
        }

        void setState(Cam::App::Stage* stage) {

            state = stage;
            if (state == boundState) { return; }
            boundState = state;

            const std::string strategy = state ? state->toolPath.strategy : std::string();

            if (strategyDropdown) { strategyDropdown->params.value = strategy; }

            ensureView(strategy);
            if (strategyView) { strategyView->setState(state); }
        }

        // The user picked a different strategy: swap the menu and commit so the
        // toolpath adopts the new strategy (carrying over the existing settings).
        void onStrategyChanged(Event& e) {

            const std::string strategy = strategyDropdown ? strategyDropdown->params.value : std::string();

            ensureView(strategy);

            if (strategyView) {
                strategyView->setState(state);
                strategyView->commit(e);
            }
        }
    };
}
