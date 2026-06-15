module;

#include <cstddef>
#include <string>
#include <vector>
#include <optional>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.StageRow;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;
import Rev.Element.Collapsible;
import Rev.Element.NumberInput;
import Rev.Window;

import Cam.App;
import Cam.App.Project;
import Cam.App.Stage;
import Cam.App.Model;
import Cam.App.Operation;
import Cam.Gui.Theme;
import Cam.Gui.OperationView;
import Cam.Gui.FaceOperationView;
import Cam.Gui.ThreadMillOperationView;
import Cam.Gui.ImportOperationView;
import Cam.Gui.ProbeView;
import Cam.Gui.ToolPathSettingsWindow;
import Cam.Gui.ToolpathSettings;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace StageRowStyle::Styles {

        // The whole material state is a subtle rounded card, very slightly
        // lighter than the panel behind it (a translucent white overlay works in
        // both light and dark themes). It gets a touch lighter when selected.
        Style Card = {
            .margin = { .bottom = 4_px },
            .border = { .radius = 4_px },
            .background = { .color = rgba(255, 255, 255, 0.03), .transition = 120_ms }
        };

        // Lightens while the pointer is anywhere over the card (applied
        // declaratively whenever the hover flag is set).
        Style CardHover = {
            .applies = { .hover = true },
            .background = { .color = rgba(255, 255, 255, 0.06) }
        };

        Style CardSelected = {
            .background = { .color = rgba(255, 255, 255, 0.09) }
        };

        // Highlight/cursor for the node header line (selection target).
        Style HeaderRow = {
            .cursor = Cursor::Hand
        };

        Style Number = {
            .margin = { .right = 6_px },
            .text = { .size = 11_px }
        };

        Style Name = {
            .text = { .size = 13_px, .wrap = Wrap::False }
        };

        // A leaf property row (no chevron). Full width so the controls can be
        // pushed to the right; left padding indents the name to line up with the
        // names of collapsible (chevron-bearing) siblings.
        Style ComponentRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .padding = { .left = 24_px, .right = 2_px, .top = 3_px, .bottom = 3_px }
        };

        // Extra padding for a collapsible property's header (it already carries
        // the chevron from the Collapsible base).
        Style PropHeader = {
            .padding = { .top = 3_px, .bottom = 3_px }
        };

        // Body of an expandable property (placeholder content for now).
        Style PropBody = {
            .padding = { .left = 6_px, .top = 2_px, .bottom = 4_px }
        };

        // The Operation body lays its sections out horizontally: the faces
        // column on the left, the labeled parameter(s) (e.g. offset) to its right.
        Style OperationBody = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct }
        };

        // The column of face slots (left side of the operation body).
        Style FacesColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False }
        };

        // One face slot, shown as "name: [face chip]" on its own line.
        Style FaceSlotRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = 100_pct },
            .padding = { .top = 1_px, .bottom = 1_px }
        };

        // The slot-name text preceding a face chip.
        Style SlotLabel = {
            .margin = { .right = 6_px },
            .text = { .size = 11_px, .wrap = Wrap::False }
        };

        // Flat referenced faces (defeature/extend) still flow as wrapping chips.
        Style FlatFaces = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::True },
            .size = { .width = 100_pct }
        };

        // A labeled parameter row (the offset), horizontal, to the right of the
        // faces column.
        Style ParamRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .margin = { .left = 12_px },
            .padding = { .top = 1_px, .bottom = 1_px }
        };

        Style ParamLabel = {
            .margin = { .right = 6_px },
            .text = { .size = 11_px, .wrap = Wrap::False }
        };

        Style Dummy = {
            .text = { .size = 11_px }
        };

        // The numeric offset input that sits beside a face slot's picker.
        Style OffsetInput = {
            .size = { .width = 56_px },
            .margin = { .top = 1_px, .bottom = 1_px }
        };

        // A referenced-face row inside the Operation body.
        Style FaceRow = {
            .padding = { .left = 2_px, .right = 2_px, .top = 1_px, .bottom = 1_px },
            .text = { .size = 11_px, .wrap = Wrap::False },
            .cursor = Cursor::Hand
        };

        Style ComponentLabel = {
            .text = { .size = 12_px, .wrap = Wrap::False }
        };

        // Subtle highlight on the property row that is actively selected.
        Style PropSelected = {
            .border = { .radius = 3_px },
            .background = { .color = rgba(255, 255, 255, 0.10) }
        };

        // Per-property type icon (left of the name). Monochrome SVGs tinted by
        // the icon's text colour.
        Style PropIcon = {
            .size = { 14_px, 14_px },
            .margin = { .right = 6_px }
        };

        Style PropIconModel = {
            .text = { .color = rgba(92, 122, 184, 1.0) }    // steel-blue part
        };

        Style PropIconOperation = {
            .text = { .color = rgba(44, 160, 78, 1.0) }      // green plus
        };

        Style PropIconDelta = {
            .text = { .color = rgba(216, 70, 60, 1.0) }      // red delta
        };

        Style PropIconToolpath = {
            .text = { .color = rgba(208, 150, 48, 1.0) }     // amber endmill
        };

        Style PropIconProbe = {
            .text = { .color = rgba(150, 96, 200, 1.0) }      // violet probe
        };

        // Pushes the trailing controls (settings + eye) to the right edge.
        Style Spacer = {
            .size = { .width = Grow() }
        };

        // Generic small icon button (settings).
        Style IconButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { .right = 4_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 2_px, .bottom = 2_px },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };

        Style SettingsIcon = {
            .size = { 14_px, 14_px }
        };

        // The eye toggle (rightmost control). Transparency is driven imperatively.
        Style Eye = {
            .size = { 15_px, 15_px }
        };

        Style EyeButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { .right = 2_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 2_px, .bottom = 2_px },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };
    };

    using namespace StageRowStyle;

    // A stage shown as a CAD-style tree node: a collapsible whose header holds
    // the step number + name (and is the selection target), and whose body holds
    // a list of component properties. Some properties (Operation, Delta, Toolpath)
    // are themselves expandable collapsibles; Operation and Toolpath also carry a
    // settings button. Every property carries a visibility eye on the right.
    struct StageRow : public Collapsible {

        Cam::App::AppState* app = nullptr;
        Cam::App::Stage* state = nullptr;

        size_t index = 0;

        Text* numberText = nullptr;
        Text* nameText = nullptr;

        static constexpr int ComponentCount = 6;

        // Component indices.
        enum Component { PriorModel = 0, Model = 1, Operation = 2, Delta = 3, Toolpath = 4, Probe = 5 };

        static bool isCollapsible(int c) { return c == Operation || c == Delta || c == Toolpath || c == Probe; }
        static bool hasSettings(int c)   { return c == Operation || c == Toolpath; }

        // Eye opacity stops: faint when visible, fainter when hidden, near-opaque
        // on hover to signal clickability.
        static constexpr float VisibleAlpha = 0.50f;
        static constexpr float HiddenAlpha  = 0.26f;
        static constexpr float HoverAlpha   = 0.95f;

        static const char* componentName(int component) {
            switch (component) {
                case PriorModel: return "Prior Model";
                case Model:      return "Model";
                case Operation:  return "Operation";
                case Delta:      return "Delta";
                case Toolpath:   return "Toolpath";
                case Probe:      return "Probe";
                default:         return "";
            }
        }

        bool* componentFlag(int component) {
            if (!state) { return nullptr; }
            switch (component) {
                case PriorModel: return &state->visible.priorModel;
                case Model:      return &state->visible.model;
                case Operation:  return &state->visible.operation;
                case Delta:      return &state->visible.delta;
                case Toolpath:   return &state->visible.toolPath;
                case Probe:      return &state->visible.probe;
                default:         return nullptr;
            }
        }

        // Per-property type icon: a SolidWorks-style part for the models, a green
        // plus for the operation, a red delta for the delta, and an endmill for
        // the toolpath. Each File literal must appear so the embedder bundles it.
        static Rev::Core::Resource iconResourceFor(int component) {
            switch (component) {
                case Operation: return File("./Operation.svg");
                case Delta:     return File("./Delta.svg");
                case Toolpath:  return File("./Toolpath.svg");
                case Probe:     return File("./Operation.svg");   // reuse until a probe glyph exists
                default:        return File("./Part.svg");   // PriorModel, Model
            }
        }

        static StyleList iconStylesFor(int component) {
            switch (component) {
                case Operation: return { &Styles::PropIcon, &Styles::PropIconOperation };
                case Delta:     return { &Styles::PropIcon, &Styles::PropIconDelta };
                case Toolpath:  return { &Styles::PropIcon, &Styles::PropIconToolpath };
                case Probe:     return { &Styles::PropIcon, &Styles::PropIconProbe };
                default:        return { &Styles::PropIcon, &Styles::PropIconModel };
            }
        }

        Collapsible* propCollapsible[ComponentCount] = {};
        Box*  propHeader[ComponentCount] = {};   // the row/header hosting name + controls
        Text* componentLabels[ComponentCount] = {};
        Box*  settingsButtons[ComponentCount] = {};
        Svg*  settingsIcons[ComponentCount] = {};
        Box*  eyeButtons[ComponentCount] = {};
        Svg*  eyes[ComponentCount] = {};
        float eyeTarget[ComponentCount] = {};

        // Inline toolpath settings (lives in the Toolpath property body).
        ToolpathSettings* toolpathSettings = nullptr;

        // The Operation property body hosts a per-type operation view (its own
        // sub-menu + interaction logic). The view is (re)created when the
        // operation's type changes.
        Box* operationBody = nullptr;
        OperationView* operationView = nullptr;
        Cam::App::OperationType operationViewType = Cam::App::OperationType::Import;

        // The Probe property body hosts the probe target editor.
        Box* probeBody = nullptr;
        ProbeView* probeView = nullptr;

        Rev::Core::Resource eyeOnResource;
        Rev::Core::Resource eyeOffResource;

        // This row owns its own settings window (if any). Ownership lives with
        // the element that triggered creation, so several stages can each have a
        // settings window open at once. The Application deletes the window once
        // shouldClose is set; we only hold a non-owning pointer.
        ToolPathSettingsWindow* settingsWindow = nullptr;

        std::function<void(Event&, Cam::App::Stage*)> onSelect;
        std::function<void(Event&)> onComponentToggled;
        std::function<void(Event&)> onSettingsChanged;

        StageRow(Element* parent, StyleList styles = {})
            : Collapsible(parent, "", styles, /*startOpen*/ true) {

            app = Cam::App::AppState::Get(shared->state);

            eyeOnResource  = File("./Eye.svg");
            eyeOffResource = File("./Eye-Off.svg");

            // The whole stage is a subtle card; the chevron follows the theme.
            this->styles.add(&Styles::Card);
            this->styles.add(&Styles::CardHover);
            arrow->styles.add(&Theme::Styles::Icon);

            // Header line: number + name; the selection target.
            header->styles.add(&Styles::HeaderRow);

            numberText = new Text(
                header, "",
                Theme::layer({ &Styles::Number }, { &Theme::Styles::MutedText })
            );

            nameText = new Text(
                header, "",
                Theme::layer({ &Styles::Name }, { &Theme::Styles::Text })
            );

            header->onClick([this](Event& e) {
                if (onSelect && state) { onSelect(e, state); }
                if (Cam::App::Project* p = activeProject()) { p->clearComponentSelection(); }
                applyOperationHighlightBaseline();
            });

            for (int i = 0; i < ComponentCount; i++) {
                buildProperty(i);
            }
        }

        // Build one property: either a leaf row or an expandable collapsible,
        // with an optional settings button and an always-present visibility eye.
        void buildProperty(int i) {

            if (isCollapsible(i)) {

                propCollapsible[i] = new Collapsible(
                    container, "", { &CollapsibleStyle::Self }, /*startOpen*/ false
                );

                propHeader[i] = propCollapsible[i]->header;
                propHeader[i]->styles.add(&Styles::PropHeader);
                propCollapsible[i]->arrow->styles.add(&Theme::Styles::Icon);

                if (i == Toolpath) {
                    // Real, inline toolpath settings.
                    toolpathSettings = new ToolpathSettings(propCollapsible[i]->container);

                    toolpathSettings->onChanged = [this](Event& e) {
                        if (onSettingsChanged) { onSettingsChanged(e); }
                    };
                }
                else if (i == Operation) {
                    // The operation body hosts a per-type operation view, created
                    // and refreshed in computeChildren.
                    operationBody = propCollapsible[i]->container;
                }
                else if (i == Probe) {
                    // The probe body hosts the probe target editor.
                    probeBody = propCollapsible[i]->container;
                }
                else {
                    // Placeholder body content (Delta) for now.
                    new Text(
                        propCollapsible[i]->container,
                        "hello world",
                        Theme::layer({ &Styles::Dummy, &Styles::PropBody }, { &Theme::Styles::MutedText })
                    );
                }
            }
            else {
                propHeader[i] = new Box(container, { &Styles::ComponentRow }, "StageComponentRow");
            }

            // Type icon (coloured), then the name.
            new Svg(propHeader[i], iconResourceFor(i), iconStylesFor(i), "StagePropertyIcon");

            componentLabels[i] = new Text(
                propHeader[i],
                componentName(i),
                Theme::layer({ &Styles::ComponentLabel }, { &Theme::Styles::Text })
            );

            // Spacer pushes the trailing controls to the right.
            new Box(propHeader[i], { &Styles::Spacer }, "StagePropertySpacer");

            // Settings button (Operation, Toolpath) — just before the eye.
            if (hasSettings(i)) {

                settingsButtons[i] = new Box(
                    propHeader[i], { &Styles::IconButton }, "StagePropertySettingsButton"
                );

                settingsIcons[i] = new Svg(
                    settingsButtons[i],
                    File("./ToolPath/Settings.svg"),
                    Theme::layer(
                        { &Styles::SettingsIcon },
                        { &Theme::Styles::Icon, &Theme::Styles::IconHover }
                    ),
                    "StagePropertySettingsIcon"
                );

                const int captured = i;
                settingsButtons[i]->onClick([this, captured](Event& e) {

                    e.propagate = false;

                    // For now only the Toolpath settings open a window (the
                    // whole-stage settings window). Operation is a no-op stub.
                    if (captured == Toolpath) {
                        openSettings(e);
                    }
                });
            }

            // Visibility eye (rightmost control) — the sole visibility toggle.
            eyeButtons[i] = new Box(propHeader[i], { &Styles::EyeButton }, "StageComponentEyeButton");

            eyes[i] = new Svg(
                eyeButtons[i],
                File("./Eye.svg"),
                Theme::layer({ &Styles::Eye }, { &Theme::Styles::Text }),
                "StageComponentEye"
            );

            eyes[i]->opacity = VisibleAlpha;
            eyeTarget[i] = VisibleAlpha;

            const int captured = i;
            eyeButtons[i]->onClick([this, captured](Event& e) {

                e.propagate = false;

                if (bool* flag = componentFlag(captured)) {
                    *flag = !*flag;
                }

                if (onComponentToggled) {
                    onComponentToggled(e);
                }
            });

            // Clicking the property row selects the stage AND this component,
            // force-showing it in the world view (even if its eye is off).
            const int capturedSelect = i;
            propHeader[i]->onClick([this, capturedSelect](Event& e) {

                if (onSelect && state) { onSelect(e, state); }

                if (Cam::App::Project* p = activeProject()) {
                    p->selectComponent(state, capturedSelect);
                }

                applyOperationHighlightBaseline();

                if (onComponentToggled) { onComponentToggled(e); }
            });

            // Hovering the Operation property highlights the faces it referenced
            // (in the prior model) in the world view — a quick way to see which
            // faces participated in the operation.
            if (i == Operation) {

                propHeader[i]->onMouseEnter([this](Event& e) {
                    if (state && state->operation) {
                        state->highlightedOperationFaces = state->operation->referencedFaces;
                        if (onComponentToggled) { onComponentToggled(e); }
                    }
                });

                propHeader[i]->onMouseLeave([this](Event& e) {
                    applyOperationHighlightBaseline();
                    if (onComponentToggled) { onComponentToggled(e); }
                });
            }
        }

        ~StageRow() {
            retireSettingsWindow();
        }

        Cam::App::Project* activeProject() {

            if (!app) { return nullptr; }

            return app->activeProject;
        }

        bool componentSelected(int i) {
            Cam::App::Project* p = activeProject();
            return p && p->selectedComponentStage == state && p->selectedComponentIndex == i;
        }

        // When the Operation component is the selected one, keep its referenced
        // faces highlighted persistently; otherwise leave them cleared. Hover
        // overrides this transiently and restores to it on leave.
        void applyOperationHighlightBaseline() {

            if (!state) { return; }

            if (componentSelected(Operation) && state->operation) {
                state->highlightedOperationFaces = state->operation->referencedFaces;
            }
            else {
                state->highlightedOperationFaces.clear();
            }
        }

        void setState(Cam::App::Stage* newState, size_t index) {

            // Reassigning this row to a different stage retires its window — the
            // window references the old stage, which may be about to go away.
            if (settingsWindow && newState != state) {
                retireSettingsWindow();
            }

            this->state = newState;
            this->index = index;
        }

        // Settings window
        //--------------------------------------------------

        std::string settingsTitle() const {
            return state ? stageName() : "Material State";
        }

        // Detach + close our window without deleting it (the Application owns the
        // lifetime). Severs callbacks/state so the soon-to-be-deleted window can't
        // touch this (possibly dying) row or a freed stage.
        void retireSettingsWindow() {

            if (!settingsWindow) { return; }

            settingsWindow->onSaved = nullptr;
            settingsWindow->onClosed = nullptr;
            settingsWindow->state = nullptr;
            settingsWindow->shouldClose = true;
            settingsWindow = nullptr;
        }

        void openSettings(Event& e) {

            if (!state) { return; }

            // Already open — just bring it forward.
            if (settingsWindow && !settingsWindow->shouldClose) {
                settingsWindow->show();
                return;
            }

            Rev::Window* owner = ToolPathSettingsWindow::rootWindow(this);

            if (!owner || !owner->shared) { return; }

            settingsWindow = new ToolPathSettingsWindow(owner, state, settingsTitle());

            settingsWindow->onSaved = [this](Event& ev) {
                if (onSettingsChanged) { onSettingsChanged(ev); }
            };

            settingsWindow->onClosed = [this](Event& ev) {
                settingsWindow = nullptr;
                if (onSettingsChanged) { onSettingsChanged(ev); }
            };
        }

        // Stage display name (without a number — the number is its own column).
        std::string stageName() const {

            if (!state) { return ""; }

            if (!state->name.empty()) { return state->name; }

            if (state->working) { return "Working State"; }

            if (index == 0) { return "Final State"; }

            return "Material State";
        }

        // Create the operation view appropriate to an operation type. Each
        // operation type owns its own body element (sub-menu + interactions).
        OperationView* makeOperationView(Element* parent, Cam::App::OperationType type) {
            switch (type) {
                case Cam::App::OperationType::Import:
                    return new ImportOperationView(parent);
                case Cam::App::OperationType::ThreadMill:
                    return new ThreadMillOperationView(parent);
                default:
                    return new FaceOperationView(parent);
            }
        }

        // Ensure the Operation body hosts a view matching the current operation's
        // type, then let that view refresh its own content.
        void syncOperationView(Event& e) {

            if (!operationBody) { return; }

            const bool hasOp = state && state->operation;
            const Cam::App::OperationType type =
                hasOp ? state->operation->type() : Cam::App::OperationType::Import;

            if (!hasOp) {
                if (operationView) { delete operationView; operationView = nullptr; }
                return;
            }

            if (!operationView || operationViewType != type) {
                if (operationView) { delete operationView; operationView = nullptr; }
                operationView = makeOperationView(operationBody, type);
                operationViewType = type;

                operationView->onChanged = [this](Event& ev) {
                    if (onComponentToggled) { onComponentToggled(ev); }
                };
                operationView->onSelectStage = [this](Event& ev, Cam::App::Stage* st) {
                    if (onSelect) { onSelect(ev, st); }
                };
            }

            operationView->setState(state);
            operationView->sync(e);
        }

        // Ensure the Probe body hosts its editor and let it refresh.
        void syncProbeView(Event& e) {

            if (!probeBody) { return; }

            if (!probeView) {
                probeView = new ProbeView(probeBody);
                probeView->onChanged = [this](Event& ev) {
                    if (onSettingsChanged) { onSettingsChanged(ev); }
                };
            }

            probeView->setState(state);
            probeView->sync(e);
        }

        // Structure + content only — no style mutation here (see computeStyle).
        void computeChildren(Event& e) override {

            // If the window closed itself (e.g. via the OS close button) drop our
            // dangling pointer; the Application has/will delete it.
            if (settingsWindow && settingsWindow->shouldClose) {
                settingsWindow = nullptr;
            }

            if (toolpathSettings) {
                toolpathSettings->setState(state);
            }

            // Host the per-type operation view and let it refresh itself.
            syncOperationView(e);

            // Host the probe target editor and let it refresh itself.
            syncProbeView(e);

            // The Operation property label reflects the actual operation type.
            if (componentLabels[Operation]) {
                componentLabels[Operation]->content =
                    (state && state->operation) ? state->operation->displayName() : "Operation";
            }

            // While a face slot of this stage is being filled, auto-expand the
            // Operation item so the slot to fill is visible.
            if (propCollapsible[Operation] && !propCollapsible[Operation]->open) {
                Cam::App::Project* p = activeProject();
                if (p && p->activeRefStage == state) {
                    propCollapsible[Operation]->expand(&e);
                }
            }

            if (numberText) {
                numberText->content = state ? std::to_string(index) : "";
            }

            if (nameText) {
                nameText->content = stageName();
            }
        }

        // Per-frame style computations belong here, not in computeChildren.
        void computeStyle(Event& e) override {

            const bool selected =
                state && activeProject() && activeProject()->isViewSelected(state);

            for (int i = 0; i < ComponentCount; i++) {

                if (!eyes[i]) { continue; }

                const bool* flag = componentFlag(i);
                const bool on = flag ? *flag : true;

                // Swap eye / eye-off (re-bakes only when it actually changes).
                eyes[i]->resource = on ? eyeOnResource : eyeOffResource;

                // Transparency: faint when resting, near-opaque on hover.
                const bool hover = eyeButtons[i] && eyeButtons[i]->targetFlags.hover;

                const float target = hover
                    ? HoverAlpha
                    : (on ? VisibleAlpha : HiddenAlpha);

                if (eyeTarget[i] != target) {
                    eyes[i]->transition(&eyes[i]->opacity, target, 120);
                    eyeTarget[i] = target;
                }

                // Dim the label of a hidden component.
                if (componentLabels[i]) {
                    if (on) { componentLabels[i]->styles.remove(&Theme::Styles::MutedText); }
                    else    { componentLabels[i]->styles.add(&Theme::Styles::MutedText); }
                }

                // Highlight the actively-selected property row.
                if (propHeader[i]) {
                    if (componentSelected(i)) { propHeader[i]->styles.add(&Styles::PropSelected); }
                    else                      { propHeader[i]->styles.remove(&Styles::PropSelected); }
                }
            }

            // Selection highlight on the whole card (a touch lighter).
            if (selected) { styles.add(&Styles::CardSelected); }
            else          { styles.remove(&Styles::CardSelected); }

            Element::computeStyle(e);
        }
    };
}
