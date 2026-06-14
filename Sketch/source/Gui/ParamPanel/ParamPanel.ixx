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
        Slider* stepover = nullptr;
        Slider* iterations = nullptr;
        Slider* leadInset = nullptr;
        Slider* cuttingDepth = nullptr;
        Slider* plungeSlope = nullptr;
        Dropdown* strategy = nullptr;
        Dropdown* direction = nullptr;
        Dropdown* milling = nullptr;
        Dropdown* leadIn = nullptr;

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

            // Stepover: ring advance as a fraction of the tool radius. Generation
            // 1 always clears by exactly R; this governs every ring after.
            Slider::SliderData so;
            so.min = 0.1f; so.max = 2.0f; so.def = 1.0f;
            so.val = app ? app->stepover : 1.0f;
            stepover = new Slider(this, so);
            if (stepover->labelText) { stepover->labelText->setContent(std::string("Stepover: ")); }

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

            // Direction: a TOOLPATH SETTING handed to the strategy -- "please emit
            // the final toolpath backwards when you're done". Changing it triggers
            // a complete regeneration; the view replays whatever comes back,
            // blindly.
            Dropdown::Params dp;
            dp.label = "Direction";
            dp.options = {
                { "Forward", "forward" },
                { "Reverse", "reverse" }
            };
            dp.placeholder = "Forward";
            dp.value = (app && app->toolReverse) ? "reverse" : "forward";
            direction = new Dropdown(this, dp);

            direction->onChange = [this](Event& e) {
                if (this->app && this->direction) {
                    this->app->toolReverse = (this->direction->params.value == "reverse");
                }
                refresh(e);
            };

            // Milling: climb or conventional -- the HANDEDNESS of every toolpath
            // chain (which way the tool travels around each ring). Order is the
            // Direction setting's business; this never reorders anything.
            Dropdown::Params mp;
            mp.label = "Milling";
            mp.options = {
                { "Climb",        "climb" },
                { "Conventional", "conventional" }
            };
            mp.placeholder = "Climb";
            mp.value = (app && !app->climbMilling) ? "conventional" : "climb";
            milling = new Dropdown(this, mp);

            milling->onChange = [this](Event& e) {
                if (this->app && this->milling) {
                    this->app->climbMilling = (this->milling->params.value != "conventional");
                }
                refresh(e);
            };

            // Lead-in/out: engagement moves woven around retract steps. The toggle
            // enables them; the inset (a fraction of the tool radius) is the "safe
            // offset" the lead rides inside the cut as it approaches / departs.
            Dropdown::Params lp;
            lp.label = "Lead-in";
            lp.options = {
                { "Off", "off" },
                { "On",  "on" }
            };
            lp.placeholder = "Off";
            lp.value = (app && app->leadIn) ? "on" : "off";
            leadIn = new Dropdown(this, lp);

            leadIn->onChange = [this](Event& e) {
                if (this->app && this->leadIn) {
                    this->app->leadIn = (this->leadIn->params.value == "on");
                }
                refresh(e);
            };

            Slider::SliderData li;
            li.min = 0.05f; li.max = 1.0f; li.def = 0.25f;
            li.val = app ? app->leadInset : 0.25f;
            leadInset = new Slider(this, li);
            if (leadInset->labelText) { leadInset->labelText->setContent(std::string("Lead Inset: ")); }

            // Cutting depth + plunge slope set how FAR the lead runs along the inset:
            // the horizontal run of a ramp that descends `depth` at `slope` degrees,
            // i.e. depth / tan(slope). The CAM app extrudes that run into the 3D ramp.
            Slider::SliderData cd;
            cd.min = 0.1f; cd.max = 25.0f; cd.def = 4.0f;
            cd.val = app ? app->cuttingDepth : 4.0f;
            cuttingDepth = new Slider(this, cd);
            if (cuttingDepth->labelText) { cuttingDepth->labelText->setContent(std::string("Cutting Depth: ")); }

            Slider::SliderData ps;
            ps.min = 1.0f; ps.max = 89.0f; ps.def = 23.0f;
            ps.val = app ? app->plungeSlope : 23.0f;
            plungeSlope = new Slider(this, ps);
            if (plungeSlope->labelText) { plungeSlope->labelText->setContent(std::string("Plunge Slope: ")); }

            // Toolpath step readout: one row per generated profile, with the stats
            // a linking / ordering pass will eventually consume.
            areaHeader = new Text(this, "Toolpath steps", { &ParamPanelStyle::AreaHeader });
            for (int i = 0; i < 33; i++) {
                areaRows.push_back(new Text(this, "", { &ParamPanelStyle::AreaRow }));
            }
        }

        // The slider has no change callback, so its value is mirrored into the
        // app state on every style pass (drags refresh, so this tracks live).
        void computeStyle(Event& e) override {
            if (app && radius) { app->toolRadius = radius->data.val; }
            if (app && stepover) { app->stepover = stepover->data.val; }
            if (app && iterations) { app->iterations = static_cast<int>(iterations->data.val + 0.5f); }
            if (app && leadInset) { app->leadInset = leadInset->data.val; }
            if (app && cuttingDepth) { app->cuttingDepth = cuttingDepth->data.val; }
            if (app && plungeSlope) { app->plungeSlope = plungeSlope->data.val; }

            // Refresh the toolpath step readout from the live run.
            if (app && app->activeProject) {
                const auto& steps = app->activeProject->toolpathSteps;
                for (size_t i = 0; i < areaRows.size(); i++) {
                    if (i < steps.size()) {
                        const auto& s = steps[i];
                        char buf[96];
                        std::snprintf(buf, sizeof(buf),
                                      "profile %zu:  %zu ch  A %.2f  dA %.2f  L %.1f%s",
                                      s.profile, s.chains, s.area, s.cleared, s.length,
                                      s.included ? "" : "  [skip]");
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
