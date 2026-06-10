module;

#include <cstddef>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <format>

export module Cam.Gui.ProbeView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.NumberInput;

import Cam.App;
import Cam.App.Project;
import Cam.App.Stage;
import Cam.App.Probe;

import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ProbeStyle {

        Style Body = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct },
            .padding = { .left = 6_px, .top = 2_px, .bottom = 4_px }
        };

        // The enable toggle row (clickable).
        Style ToggleRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .padding = { .top = 1_px, .bottom = 3_px },
            .cursor = Cursor::Hand
        };

        Style ToggleBox = {
            .margin = { .right = 6_px },
            .text = { .size = 12_px, .wrap = Wrap::False }
        };

        Style Hint = {
            .padding = { .top = 1_px, .bottom = 3_px },
            .text = { .size = 11_px, .wrap = Wrap::True }
        };

        Style TargetRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .padding = { .top = 1_px, .bottom = 1_px }
        };

        Style Index = {
            .margin = { .right = 6_px },
            .text = { .size = 11_px, .wrap = Wrap::False }
        };

        Style Coords = {
            .margin = { .right = 8_px },
            .text = { .size = 11_px, .wrap = Wrap::False }
        };

        Style Spacer = {
            .size = { .width = Grow() }
        };

        Style SmallInput = {
            .size = { .width = 48_px },
            .margin = { .left = 4_px }
        };

        Style InputLabel = {
            .margin = { .left = 8_px, .right = 2_px },
            .text = { .size = 10_px, .wrap = Wrap::False }
        };

        Style RemoveButton = {
            .margin = { .left = 8_px },
            .padding = { .left = 4_px, .right = 4_px },
            .text = { .size = 12_px, .wrap = Wrap::False },
            .cursor = Cursor::Hand
        };

        Style Result = {
            .padding = { .top = 4_px, .bottom = 1_px },
            .text = { .size = 11_px, .wrap = Wrap::True }
        };
    }

    // The Probe sub-component body in a stage tree row.  Lets the user enable a
    // probe step on the stage, see/edit the list of probe targets (each a point
    // + surface normal in the CAD frame, with a standoff/overtravel), and read
    // back the fitted frame result.  Points themselves are placed in the 3D view
    // (press "P" over the part); this panel is the list/edit surface for them.
    struct ProbeView : public Box {

        Cam::App::AppState* app = nullptr;
        Cam::App::Stage* state = nullptr;

        std::function<void(Event&)> onChanged;

        // Rebuild tracking: re-render the list when the stage, enabled flag, or
        // target count changes (standoff/overtravel edits mutate in place and
        // don't need a structural rebuild).
        Cam::App::Stage* boundState = nullptr;
        bool   boundEnabled = false;
        size_t boundCount   = static_cast<size_t>(-1);
        bool   boundResultValid = false;

        std::vector<Element*> rows;

        ProbeView(Element* parent)
            : Box(parent, { &ProbeStyle::Body }, "ProbeView") {
            app = Cam::App::AppState::Get(shared->state);
        }

        Cam::App::Project* project() { return app ? app->activeProject : nullptr; }

        void setState(Cam::App::Stage* s) { state = s; }

        void notifyChanged(Event& e) {
            if (Cam::App::Project* p = project()) { p->markDirty(); }
            if (onChanged) { onChanged(e); }
        }

        void sync(Event& e) {

            const bool   enabled = state && state->probe.enabled;
            const size_t count   = state ? state->probe.count() : 0;
            const bool   resValid = state && state->probe.result.valid;

            if (state != boundState || enabled != boundEnabled ||
                count != boundCount || resValid != boundResultValid) {
                boundState       = state;
                boundEnabled     = enabled;
                boundCount       = count;
                boundResultValid = resValid;
                rebuild();
            }
        }

        void rebuild() {

            for (Element* row : rows) { delete row; }
            rows.clear();

            if (!state) {
                rows.push_back(new Text(
                    this, "No stage",
                    Theme::layer({ &ProbeStyle::Hint }, { &Theme::Styles::MutedText })
                ));
                return;
            }

            // -- Enable toggle ------------------------------------------
            Box* toggle = new Box(this, { &ProbeStyle::ToggleRow }, "ProbeToggle");
            new Text(
                toggle,
                state->probe.enabled ? "[x]" : "[ ]",
                Theme::layer({ &ProbeStyle::ToggleBox }, { &Theme::Styles::Text })
            );
            new Text(
                toggle, "Probe this stage",
                Theme::layer({ &ProbeStyle::ToggleBox }, { &Theme::Styles::Text })
            );
            toggle->onClick([this](Event& e) {
                if (state) {
                    state->probe.enabled = !state->probe.enabled;
                    notifyChanged(e);
                }
                e.propagate = false;
            });
            rows.push_back(toggle);

            if (!state->probe.enabled) { return; }

            // -- Hint ---------------------------------------------------
            rows.push_back(new Text(
                this,
                "Press P with the cursor over the part to add a probe point.",
                Theme::layer({ &ProbeStyle::Hint }, { &Theme::Styles::MutedText })
            ));

            // -- Targets ------------------------------------------------
            for (size_t i = 0; i < state->probe.targets.size(); i++) {
                buildTargetRow(i);
            }

            if (state->probe.targets.empty()) {
                rows.push_back(new Text(
                    this, "No probe points yet.",
                    Theme::layer({ &ProbeStyle::Hint }, { &Theme::Styles::MutedText })
                ));
            }

            // -- Result readout -----------------------------------------
            buildResultRow();
        }

        void buildTargetRow(size_t i) {

            Cam::App::ProbeTarget& t = state->probe.targets[i];

            Box* row = new Box(this, { &ProbeStyle::TargetRow }, "ProbeTargetRow");

            new Text(
                row, std::to_string(i),
                Theme::layer({ &ProbeStyle::Index }, { &Theme::Styles::MutedText })
            );

            new Text(
                row,
                std::format("({:.1f}, {:.1f}, {:.1f})", t.point.x, t.point.y, t.point.z),
                Theme::layer({ &ProbeStyle::Coords }, { &Theme::Styles::Text })
            );

            // Standoff input.
            new Text(
                row, "off",
                Theme::layer({ &ProbeStyle::InputLabel }, { &Theme::Styles::MutedText })
            );
            NumberInput::Params sp = NumberInput::Params::Default();
            sp.label = "Standoff (mm)";
            sp.placeholder = "5";
            sp.maxDecimalPlaces = 2;
            NumberInput* standoff = new NumberInput(row, sp, { &ProbeStyle::SmallInput });
            standoff->setValue(t.standoff);
            standoff->onValueChange = [this, i](Event& e, std::optional<double> v) {
                if (v && state && i < state->probe.targets.size()) {
                    state->probe.targets[i].standoff = *v;
                    notifyChanged(e);
                }
            };
            standoff->onKeyDown([standoff](Event& e) {
                if (e.keyboard.enter) { e.propagate = false; standoff->commit(e); }
            });

            // Spacer + remove.
            new Box(row, { &ProbeStyle::Spacer }, "ProbeRowSpacer");

            Text* remove = new Text(
                row, "remove",
                Theme::layer({ &ProbeStyle::RemoveButton }, { &Theme::Styles::MutedText })
            );
            remove->onClick([this, i](Event& e) {
                if (state && i < state->probe.targets.size()) {
                    state->probe.targets.erase(state->probe.targets.begin() + static_cast<std::ptrdiff_t>(i));
                    state->probe.clearResult();   // targets changed -> stale fit
                    notifyChanged(e);
                }
                e.propagate = false;
            });

            rows.push_back(row);
        }

        void buildResultRow() {

            const Cam::App::ProbeResult& r = state->probe.result;

            std::string text;
            if (r.valid) {
                text = std::format(
                    "Fit: RMS {:.3f} mm | shift ({:.2f}, {:.2f}, {:.2f})",
                    r.rmsError, r.t.x, r.t.y, r.t.z);
            }
            else if (state->probe.targets.size() < 3) {
                text = std::format(
                    "Add {} more point(s) for a full-frame fit (3 minimum).",
                    3 - state->probe.targets.size());
            }
            else {
                text = "Ready to probe (not yet run).";
            }

            rows.push_back(new Text(
                this, text,
                Theme::layer({ &ProbeStyle::Result }, { &Theme::Styles::MutedText })
            ));
        }
    };
}
