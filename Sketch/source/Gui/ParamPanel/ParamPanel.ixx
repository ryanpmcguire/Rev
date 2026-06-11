module;

#include <string>
#include <vector>
#include <cstdio>

export module Sketch.Gui.ParamPanel;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Slider;
import Rev.Element.Dropdown;

import Sketch.App;
import Sketch.Gui.Theme;

export namespace Sketch::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ParamPanelStyle {
        Style AreaHeader = { .margin = { .top = 12_px }, .text = { .size = 13_px } };
        Style AreaRow    = { .text = { .size = 12_px } };
    }

    // The parameter panel on the content's left side: the knobs the toolpathing
    // strategies need. The controls are thin reflections of AppState (the same
    // single-source-of-truth pattern as the toolbar) -- the sketch view watches
    // those values and rebuilds when they move.
    struct ParamPanel : public Box {

        Sketch::App::AppState* app = nullptr;

        Slider* radius = nullptr;
        Slider* iterations = nullptr;
        Dropdown* strategy = nullptr;

        // The per-iteration signed-area readout: a fixed pool of rows, filled
        // from the live profile series each style pass (unused rows go blank).
        Text* areaHeader = nullptr;
        std::vector<Text*> areaRows;

        ParamPanel(Element* parent, StyleList styles = {}) : Box(parent, styles, "ParamPanel") {

            app = Sketch::App::AppState::Get(shared->state);

            this->style->layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
            this->style->size = { .width = 260_px, .height = 100_pct };
            this->style->padding = { 10_px, 10_px, 10_px, 10_px };
            this->styles.add(&Theme::Styles::TabBar);

            // Tool radius: the offset step every strategy is built from.
            Slider::SliderData rd;
            rd.min = 0.1f; rd.max = 10.0f; rd.def = 1.0f;
            rd.val = app ? app->toolRadius : 1.0f;
            radius = new Slider(this, rd);
            if (radius->labelText) { radius->labelText->setContent(std::string("Tool Radius: ")); }

            // Iterations: the maximum number of offset generations per run.
            Slider::SliderData id;
            id.min = 1.0f; id.max = 32.0f; id.def = 32.0f;
            id.val = app ? static_cast<float>(app->iterations) : 32.0f;
            iterations = new Slider(this, id);
            if (iterations->labelText) { iterations->labelText->setContent(std::string("Iterations: ")); }

            // Strategy: which mock toolpathing strategy runs on the live geometry.
            Dropdown::Params sp;
            sp.label = "Strategy";
            sp.options = {
                { "Profile", "profile" },
                { "Hatch",   "hatch" }
            };
            sp.placeholder = "Profile";
            sp.value = app ? app->strategy : "profile";
            strategy = new Dropdown(this, sp);

            strategy->onChange = [this](Event& e) {
                if (this->app && this->strategy) { this->app->strategy = this->strategy->params.value; }
                refresh(e);
            };

            // Signed-area readout: one row per possible iteration.
            areaHeader = new Text(this, "Signed area / iteration", { &ParamPanelStyle::AreaHeader });
            for (int i = 0; i < 33; i++) {
                areaRows.push_back(new Text(this, "", { &ParamPanelStyle::AreaRow }));
            }
        }

        // The slider has no change callback, so its value is mirrored into the
        // app state on every style pass (drags refresh, so this tracks live).
        void computeStyle(Event& e) override {
            if (app && radius) { app->toolRadius = radius->data.val; }
            if (app && iterations) { app->iterations = static_cast<int>(iterations->data.val + 0.5f); }

            // Refresh the signed-area readout from the live profile series.
            if (app && app->activeProject) {
                const auto& profiles = app->activeProject->profiles;
                for (size_t i = 0; i < areaRows.size(); i++) {
                    if (i < profiles.size()) {
                        float area = 0.0f;
                        for (const auto& c : profiles[i].chains) { area += c.signedArea(); }
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%zu:  %.3f  (%zu chains)",
                                      i, area, profiles[i].chains.size());
                        areaRows[i]->setContent(std::string(buf));
                    }
                    else {
                        areaRows[i]->setContent(std::string());
                    }
                }
            }

            Box::computeStyle(e);
        }
    };
}
