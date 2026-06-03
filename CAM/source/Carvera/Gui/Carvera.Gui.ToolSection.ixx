module;

#include <string>
#include <vector>

#include <managed.hpp>

export module Carvera.Gui.ToolSection;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Dropdown;

import CarveraAir;

import Cam.App;
import Cam.App.Tool;
import Cam.Gui.Theme;
import Carvera.Gui.Style;

export namespace Carvera::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace Theme = Cam::Gui::Theme;

    // ── Tool-change section ──────────────────────────────────────────
    //
    //   Loaded: <name>             <- the tool currently in the spindle
    //   [ Select tool  ▼ ]         <- dropdown: pick the target tool
    //   [  Confirm/Change Tool  ]  <- context-sensitive action button
    //
    // The action button reflects the live tool-change phase, and — when
    // idle — whether a tool is loaded:
    //   None  + nothing loaded  "Confirm Tool"  (change + auto-Ok + touch-off)
    //   None  + tool loaded     "Change Tool"   (change, operator confirms at Standby)
    //   Seeking                 "Please wait..." + orange border
    //   Standby                 "Ok" (blue)     + blue border
    //   Confirming              "Please wait..." + orange border (touch-off)
    struct ToolSection : public Box {

        static Carvera::Air& air() { return Carvera::Air::instance(); }

        Cam::App::AppState* app = nullptr;

        Text*     loadedToolText  = nullptr;
        Dropdown* toolDropdown    = nullptr;
        Box*      changeToolBtn   = nullptr;
        Text*     changeToolLabel = nullptr;

        // What the section last applied; used to skip redundant style mutations
        // in computeChildren that would otherwise mark the panel dirty every
        // frame and prevent the window from going quiet when nothing changed.
        Carvera::Air::ToolChangePhase lastPhaseApplied_  = Carvera::Air::ToolChangePhase::None;
        bool                          lastPhaseValid_    = false;
        int                           lastLoadedApplied_ = -1;   // -1 = "no value applied yet"

        ToolSection(Element* parent, Cam::App::AppState* appState = nullptr)
            : Box(parent, Theme::withPanel({ &Style::Section }), "ToolSection"),
              app(appState)
        {
            build();
            subscribe();
        }

        void build() {

            // Currently-loaded tool readout — separate from the selection
            // dropdown so the operator can always see what's physically in
            // the spindle without changing the dropdown's pending target.
            Box* loadedRow = new Box(this, { &Style::Row }, "LoadedToolRow");
            new Text(loadedRow, "Loaded:", Theme::withMutedText({ &Style::LoadedLabel }));
            loadedToolText = new Text(loadedRow, "None", Theme::withText({ &Style::OriginName }));

            // Tool selector — populated up-front with the desired list so the
            // dropdown is never empty when first opened.
            toolDropdown = new Dropdown(this, {
                .label       = "Select tool",
                .options     = desiredToolItems(),
                .placeholder = "Select tool",
                .value       = ""
            });

            // Default to the first option so the change button has something
            // to do on first launch.
            if (!toolDropdown->params.options.empty()) {
                toolDropdown->params.value = toolDropdown->params.options.front().value;
            }

            // Context-sensitive action button (full width).
            changeToolBtn = makeBtn(this, "Change Tool", Style::Btn);
            changeToolBtn->style->size = { .width = Grow(), .height = 34_px };
            changeToolLabel = static_cast<Text*>(changeToolBtn->children.front());
            changeToolBtn->onClick([this](Event& e) { onChangeToolButton(e); e.propagate = false; });
        }

        void subscribe() {
            auto bump = [this](auto&) { if (shared && shared->event) { refresh(*shared->event); } };
            air().onToolChangeBegin   ([bump](Carvera::Air::ToolChangeEvent& e) { bump(e); });
            air().onToolChangeStandby ([bump](Carvera::Air::ToolChangeEvent& e) { bump(e); });
            air().onToolChangeConfirm ([bump](Carvera::Air::ToolChangeEvent& e) { bump(e); });
            air().onToolChangeComplete([bump](Carvera::Air::ToolChangeEvent& e) { bump(e); });
            air().onConnection        ([bump](Carvera::Air::ConnectionEvent& e) { bump(e); });
        }

        // The list the dropdown SHOULD show: library tools when a project is
        // loaded, otherwise the machine's physical ATC slots so the control is
        // always usable (and tool changes are always testable).  Pure function
        // of current state — never mutates anything.
        std::vector<Dropdown::Item> desiredToolItems() const {

            std::vector<Dropdown::Item> items;

            if (app) {
                const size_t count = app->toolCount();
                for (size_t i = 0; i < count; i++) {
                    Cam::App::Tool* t = app->toolAt(i);
                    if (!t) { continue; }
                    items.push_back({ t->name, std::to_string(i + 1) });  // value = T-number
                }
            }

            if (items.empty()) {
                // No project / empty library — fall back to ATC slots T1..T6.
                for (int s = 1; s <= 6; s++) {
                    items.push_back({ "Tool " + std::to_string(s), std::to_string(s) });
                }
            }

            return items;
        }

        // Reconcile the dropdown's options with the desired list.  Skipped
        // while the menu is open so a click never causes the option list to
        // mutate underneath the user.
        void syncToolOptions() {

            if (!toolDropdown) { return; }
            if (toolDropdown->open) { return; }

            std::vector<Dropdown::Item> items = desiredToolItems();

            bool changed = items.size() != toolDropdown->params.options.size();
            if (!changed) {
                for (size_t i = 0; i < items.size(); i++) {
                    if (items[i].name  != toolDropdown->params.options[i].name ||
                        items[i].value != toolDropdown->params.options[i].value) {
                        changed = true;
                        break;
                    }
                }
            }
            if (!changed) { return; }

            // Preserve the current selection if it still exists.
            const std::string keep = toolDropdown->params.value;
            toolDropdown->params.options = std::move(items);

            bool stillValid = false;
            for (const Dropdown::Item& it : toolDropdown->params.options) {
                if (it.value == keep) { stillValid = true; break; }
            }
            if (!stillValid) {
                toolDropdown->params.value =
                    toolDropdown->params.options.empty()
                        ? std::string()
                        : toolDropdown->params.options.front().value;
            }
        }

        // The selected ATC slot (T-number), parsed from the dropdown value.
        int currentToolSlot() const {
            if (toolDropdown && !toolDropdown->params.value.empty()) {
                try { return std::stoi(toolDropdown->params.value); }
                catch (...) {}
            }
            return 1;
        }

        // Display name for a loaded ATC slot (library name when known).
        std::string toolNameForSlot(int slot) const {
            if (slot <= 0) { return "None"; }
            if (app) {
                if (Cam::App::Tool* t = app->toolAt((size_t)(slot - 1))) {
                    return t->name;
                }
            }
            return "Tool " + std::to_string(slot);
        }

        // Single action button, behaviour depends on the tool-change phase and
        // (when idle) whether a tool is currently loaded.
        void onChangeToolButton(Event& e) {

            using Phase = Carvera::Air::ToolChangePhase;
            const Phase phase = air().toolChangePhase();

            if (phase == Phase::Standby) {
                air().confirmToolChange();          // == pressing the machine's button
                refresh(e);
                return;
            }

            // Seeking (travelling) or Confirming (finishing) — passive.
            if (phase == Phase::Seeking || phase == Phase::Confirming) {
                return;
            }

            // Idle.  With nothing loaded yet, "Confirm Tool" runs the change AND
            // auto-presses Ok so it flows straight into touch-off; once a tool is
            // loaded, a swap uses the normal change (operator confirms at Standby).
            const bool nothingLoaded = (air().loadedToolSlot() == 0);
            air().changeTool(currentToolSlot(), /*autoConfirm=*/nothingLoaded);
            refresh(e);
        }

        void computeChildren(Event& e) override {

            Carvera::Air& a = air();

            syncToolOptions();

            using Phase = Carvera::Air::ToolChangePhase;

            const int   loaded = a.loadedToolSlot();
            const Phase phase  = a.toolChangePhase();

            // Currently-loaded tool readout — only touch when the slot itself
            // changes (the displayed string is the same either way).
            if (loadedToolText && loaded != lastLoadedApplied_) {
                loadedToolText->content = toolNameForSlot(loaded);
                lastLoadedApplied_ = loaded;
            }

            // Section border + action button.  Both depend on (phase, loaded).
            // The label text in the idle branches also depends on whether a
            // tool is loaded — so re-apply when EITHER phase or "loaded != 0"
            // (the only thing that matters for the label) actually changed.
            const bool nothingLoaded = (loaded == 0);
            const bool phaseChanged  = (!lastPhaseValid_ || phase != lastPhaseApplied_);
            const bool labelMayChange =
                phase == Phase::None &&
                (lastLoadedApplied_ == loaded ? false : (loaded == 0) != (lastLoadedApplied_ == 0));

            if (changeToolBtn && changeToolLabel && (phaseChanged || labelMayChange)) {

                styles.remove(&Style::PanelBorderToolChange);
                styles.remove(&Style::PanelBorderBlue);
                changeToolBtn->styles.remove(&Style::ToolConfirmButton);

                if (phase == Phase::Seeking || phase == Phase::Confirming) {
                    styles.add(&Style::PanelBorderToolChange);
                    changeToolLabel->content = "Please wait...";
                }
                else if (phase == Phase::Standby) {
                    styles.add(&Style::PanelBorderBlue);
                    changeToolBtn->styles.add(&Style::ToolConfirmButton);
                    changeToolLabel->content = "Ok";
                }
                else if (nothingLoaded) {
                    changeToolLabel->content = "Confirm Tool";
                }
                else {
                    changeToolLabel->content = "Change Tool";
                }

                lastPhaseApplied_ = phase;
                lastPhaseValid_   = true;
            }

            Box::computeChildren(e);
        }
    };
}
