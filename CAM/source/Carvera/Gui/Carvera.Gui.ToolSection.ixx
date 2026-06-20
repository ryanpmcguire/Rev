module;

#include <string>
#include <vector>
#include <format>

#include <managed.hpp>

export module Carvera.Gui.ToolSection;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
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

    // -- Tool-change section ------------------------------------------
    //
    //   Loaded: <name>             <- the tool currently in the spindle
    //   [ Select tool  v ]         <- dropdown: pick the target tool
    //   [     Change Tool       ]  <- context-sensitive action button
    //
    // The action button reflects the live tool-change phase, and -- when
    // idle -- whether a tool is loaded:
    //   None        "Change Tool"               (sends M6 -> enters Seeking)
    //   Seeking     "Moving to tool position..."+ orange border
    //   Standby     "Ok" (blue)                 + blue border  -- press to confirm
    //   Confirming  "Touching off..."           + orange border (post-confirm
    //                                             touch-off + return to pre-
    //                                             change position; ends only
    //                                             when the machine has settled
    //                                             back to Idle)
    struct ToolSection : public Box {

        static Carvera::Air* airPtr() { return &Carvera::Air::instance(); }

        Carvera::Air*       air_ = nullptr;
        Cam::App::AppState* app  = nullptr;

        Text*     loadedToolText  = nullptr;
        Dropdown* toolDropdown    = nullptr;
        Box*      changeToolBtn   = nullptr;
        Text*     changeToolLabel = nullptr;

        // Last values written to the DOM (skip redundant style/text mutations).
        Carvera::Air::ToolChangePhase lastPhaseApplied_  = Carvera::Air::ToolChangePhase::None;
        bool                          lastPhaseValid_    = false;
        int                           lastLoadedApplied_ = -1;   // -1 = not applied yet

        // Last Air snapshot we scheduled a refresh for (event handlers).
        int                           cachedLoaded_     = -1;
        Carvera::Air::ToolChangePhase cachedPhase_      = Carvera::Air::ToolChangePhase::None;
        bool                          cachedConnected_  = false;
        bool                          airCacheValid_    = false;

        ToolSection(Element* parent, Cam::App::AppState* appState = nullptr)
            : Box(parent, Theme::withPanel({ &Style::Section }), "ToolSection"),
              air_(airPtr()),
              app(appState)
        {
            build();
            subscribe();
        }

        void build() {

            Box* loadedRow = new Box(this, { &Style::Row }, "LoadedToolRow");
            new Text(loadedRow, "Loaded:", Theme::withMutedText({ &Style::LoadedLabel }));
            loadedToolText = new Text(loadedRow, "None", Theme::withText({ &Style::OriginName }));

            toolDropdown = new Dropdown(this, {
                .label       = "Select tool",
                .options     = desiredToolItems(),
                .placeholder = "Select tool",
                .value       = ""
            });

            // Default to the first ENABLED option (the loaded tool is disabled
            // with a "(loaded)" suffix -- selecting it would be a no-op change).
            for (const Dropdown::Item& it : toolDropdown->params.options) {
                if (!it.disabled) { toolDropdown->params.value = it.value; break; }
            }
            if (toolDropdown->params.value.empty() && !toolDropdown->params.options.empty()) {
                toolDropdown->params.value = toolDropdown->params.options.front().value;
            }

            changeToolBtn = makeBtn(this, "Change Tool", Style::Btn);
            changeToolBtn->style->size = { .width = Grow(), .height = 34_px };
            changeToolLabel = static_cast<Text*>(changeToolBtn->children.front());
            changeToolBtn->onClick([this](Event& e) { onChangeToolButton(e); e.propagate = false; });
        }

        void subscribe() {
            if (!air_) { return; }
            air_->onToolChangeBegin([this](Carvera::Air::ToolChangeEvent& e) { onToolChangeBegin(e); });
            air_->onToolChangeStandby([this](Carvera::Air::ToolChangeEvent& e) { onToolChangeStandby(e); });
            air_->onToolChangeConfirm([this](Carvera::Air::ToolChangeEvent& e) { onToolChangeConfirm(e); });
            air_->onToolChangeComplete([this](Carvera::Air::ToolChangeEvent& e) { onToolChangeComplete(e); });
            air_->onConnection([this](Carvera::Air::ConnectionEvent& e) { onConnection(e); });
        }

        void onToolChangeBegin(Carvera::Air::ToolChangeEvent& e) {
            (void)e;
            refreshIfAirStateChanged();
        }

        void onToolChangeStandby(Carvera::Air::ToolChangeEvent& e) {
            (void)e;
            refreshIfAirStateChanged();
        }

        void onToolChangeConfirm(Carvera::Air::ToolChangeEvent& e) {
            (void)e;
            refreshIfAirStateChanged();
        }

        void onToolChangeComplete(Carvera::Air::ToolChangeEvent& e) {
            (void)e;
            refreshIfAirStateChanged();
        }

        void onConnection(Carvera::Air::ConnectionEvent& e) {
            (void)e;
            refreshIfAirStateChanged();
        }

        // Compare live Air state to the last snapshot that triggered a refresh.
        bool airStateDiffersFromCache() const {
            if (!air_) { return false; }
            const int   loaded    = air_->loadedToolSlot();
            const auto  phase     = air_->toolChangePhase();
            const bool  connected = air_->connected();
            if (!airCacheValid_) { return true; }
            return loaded    != cachedLoaded_
                || phase     != cachedPhase_
                || connected != cachedConnected_;
        }

        void commitAirStateCache() {
            if (!air_) { return; }
            cachedLoaded_    = air_->loadedToolSlot();
            cachedPhase_     = air_->toolChangePhase();
            cachedConnected_ = air_->connected();
            airCacheValid_   = true;
        }

        void invalidateAirStateCache() { airCacheValid_ = false; }

        void refreshIfAirStateChanged() {
            if (!airStateDiffersFromCache()) { return; }
            commitAirStateCache();
            requestUiRefresh();
        }

        void requestUiRefresh() {
            if (!shared || !shared->event) { return; }
            refresh(*shared->event);
        }

        void refreshAfterUserAction(Event& e) {
            invalidateAirStateCache();
            commitAirStateCache();
            refresh(e);
        }

        // The list the dropdown SHOULD show.  Marks the currently-loaded slot
        // (per Air's view of the machine) as "(loaded)" and disables it so
        // the user can't initiate a no-op change that would leave us stuck
        // waiting for state transitions the Carvera never makes.
        std::vector<Dropdown::Item> desiredToolItems() const {

            const int loaded = air_ ? air_->loadedToolSlot() : 0;

            auto markLoaded = [&](Dropdown::Item it) {
                const int slot = std::stoi(it.value);
                // Disable ONLY the loaded REAL tool (slot > 0) -- re-selecting it would
                // be a no-op the Carvera never acks.  "None" (slot 0) is ALWAYS
                // selectable: it is an action (unload + touch off the bare nose), and the
                // operator must be able to choose it even when no tool is loaded.
                if (slot > 0 && slot == loaded) {
                    it.name    += " (loaded)";
                    it.disabled = true;
                }
                return it;
            };

            std::vector<Dropdown::Item> items;

            // PERMANENT "None" (T0): unload the spindle.  Changing to None runs the
            // machine's touch-off on the BARE SPINDLE NOSE, which is how we reference
            // the tool setter (zero tool length) for host-side tool-length tracking.
            // Always present so the operator can actually select "no tool".
            items.push_back(markLoaded({ "None", "0" }));

            const size_t beforeTools = items.size();
            if (app) {
                const size_t count = app->toolCount();
                for (size_t i = 0; i < count; i++) {
                    Cam::App::Tool* t = app->toolAt(i);
                    if (!t) { continue; }
                    items.push_back(markLoaded({ t->name, std::to_string(i + 1) }));
                }
            }

            if (items.size() == beforeTools) {   // no real tools -> slot placeholders
                for (int s = 1; s <= 6; s++) {
                    items.push_back(markLoaded({ "Tool " + std::to_string(s), std::to_string(s) }));
                }
            }

            return items;
        }

        // Item equality including the disabled flag -- so a tool flipping from
        // available to "(loaded)" (or vice versa) actually triggers a rebuild.
        static bool sameItem(const Dropdown::Item& a, const Dropdown::Item& b) {
            return a.name == b.name && a.value == b.value && a.disabled == b.disabled;
        }

        void syncToolOptions() {

            if (!toolDropdown) { return; }
            if (toolDropdown->open) { return; }

            std::vector<Dropdown::Item> items = desiredToolItems();

            bool changed = items.size() != toolDropdown->params.options.size();
            if (!changed) {
                for (size_t i = 0; i < items.size(); i++) {
                    if (!sameItem(items[i], toolDropdown->params.options[i])) {
                        changed = true;
                        break;
                    }
                }
            }
            if (!changed) { return; }

            const std::string keep = toolDropdown->params.value;
            toolDropdown->params.options = std::move(items);

            // Pick a valid selection: prefer keeping the current one if it's
            // still present AND not disabled (= not the loaded tool); fall back
            // to the first non-disabled option.
            auto enabledAt = [&](const std::string& v) -> bool {
                for (const Dropdown::Item& it : toolDropdown->params.options) {
                    if (it.value == v) { return !it.disabled; }
                }
                return false;
            };
            auto firstEnabled = [&]() -> std::string {
                for (const Dropdown::Item& it : toolDropdown->params.options) {
                    if (!it.disabled) { return it.value; }
                }
                return toolDropdown->params.options.empty()
                    ? std::string()
                    : toolDropdown->params.options.front().value;
            };

            if (!enabledAt(keep)) {
                toolDropdown->params.value = firstEnabled();
            }
        }

        int currentToolSlot() const {
            if (toolDropdown && !toolDropdown->params.value.empty()) {
                try { return std::stoi(toolDropdown->params.value); }
                catch (...) {}
            }
            return 1;
        }

        std::string toolNameForSlot(int slot) const {
            if (slot <= 0) { return "None"; }
            if (app) {
                if (Cam::App::Tool* t = app->toolAt((size_t)(slot - 1))) {
                    return t->name;
                }
            }
            return "Tool " + std::to_string(slot);
        }

        void onChangeToolButton(Event& e) {

            if (!air_) { return; }

            using Phase = Carvera::Air::ToolChangePhase;
            const Phase phase = air_->toolChangePhase();

            // Diagnostic log: confirms the click reached us and which branch
            // we're taking.  If the user reports "button does nothing" but
            // this line never appears in the log panel, the click isn't even
            // being delivered (overlay / hit-testing issue) -- and we look
            // elsewhere.  If it appears but the machine doesn't react, it's
            // a protocol issue at the Carvera end.
            air_->log(std::format(
                "[tool] button clicked; phase={}",
                phaseName(phase)
            ));

            if (phase == Phase::Standby) {
                air_->confirmToolChange();
                refreshAfterUserAction(e);
                return;
            }

            if (phase == Phase::Seeking || phase == Phase::Confirming) {
                return;
            }

            // No auto-confirm -- the operator must explicitly press Ok (either
            // on the panel or on the machine itself) when Standby is reached.
            //
            // T0 (None / unload) is FORCED: the point of selecting it is to drop the
            // tool and touch off the bare spindle nose (our tool-length reference), so we
            // want it to run even when the spindle is already empty -- otherwise the
            // "already loaded T0" no-op would swallow it.
            const int slot = currentToolSlot();
            air_->changeTool(slot, /*force=*/slot == 0);
            refreshAfterUserAction(e);
        }

        static const char* phaseName(Carvera::Air::ToolChangePhase p) {
            using Phase = Carvera::Air::ToolChangePhase;
            switch (p) {
                case Phase::None:       return "None";
                case Phase::Seeking:    return "Seeking";
                case Phase::Standby:    return "Standby";
                case Phase::Confirming: return "Confirming";
            }
            return "?";
        }

        void updateLoadedToolReadout() {
            if (!air_ || !loadedToolText) { return; }

            const int loaded = air_->loadedToolSlot();
            if (loaded == lastLoadedApplied_) { return; }

            loadedToolText->content = toolNameForSlot(loaded);
            lastLoadedApplied_ = loaded;
        }

        void updateToolChangePresentation() {
            if (!air_ || !changeToolBtn || !changeToolLabel) { return; }

            using Phase = Carvera::Air::ToolChangePhase;
            const Phase phase = air_->toolChangePhase();

            if (lastPhaseValid_ && phase == lastPhaseApplied_) { return; }

            styles.remove(&Style::PanelBorderToolChange);
            styles.remove(&Style::PanelBorderBlue);
            changeToolBtn->styles.remove(&Style::ToolConfirmButton);

            switch (phase) {
                case Phase::Seeking:
                    styles.add(&Style::PanelBorderToolChange);
                    changeToolLabel->content = "Moving to tool position...";
                    break;
                case Phase::Standby:
                    styles.add(&Style::PanelBorderBlue);
                    changeToolBtn->styles.add(&Style::ToolConfirmButton);
                    changeToolLabel->content = "Ok";
                    break;
                case Phase::Confirming:
                    styles.add(&Style::PanelBorderToolChange);
                    changeToolLabel->content = "Touching off...";
                    break;
                case Phase::None:
                    changeToolLabel->content = "Change Tool";
                    break;
            }

            lastPhaseApplied_ = phase;
            lastPhaseValid_   = true;
        }

        void computeChildren(Event& e) override {

            if (air_) {
                syncToolOptions();
                updateLoadedToolReadout();
                updateToolChangePresentation();
            }

            Box::computeChildren(e);
        }
    };
}
