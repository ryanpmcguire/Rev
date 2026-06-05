module;

#include <cstddef>
#include <string>
#include <vector>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.StageRow;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;
import Rev.Element.Collapsible;
import Rev.Window;

import Cam.App;
import Cam.App.Project;
import Cam.App.Stage;
import Cam.App.Model;
import Cam.App.Operation;
import Cam.Gui.Theme;
import Cam.Gui.Face;
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

        // The Operation body lays its Face chips out horizontally (wrapping),
        // rather than stacking them — each face hugs its own width. This opens
        // the door to richer face-row UX later.
        Style OperationBody = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::True },
            .size = { .width = 100_pct }
        };

        Style Dummy = {
            .text = { .size = 11_px }
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

        static constexpr int ComponentCount = 5;

        // Component indices.
        enum Component { PriorModel = 0, Model = 1, Operation = 2, Delta = 3, Toolpath = 4 };

        static bool isCollapsible(int c) { return c == Operation || c == Delta || c == Toolpath; }
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
                default:        return File("./Part.svg");   // PriorModel, Model
            }
        }

        static StyleList iconStylesFor(int component) {
            switch (component) {
                case Operation: return { &Styles::PropIcon, &Styles::PropIconOperation };
                case Delta:     return { &Styles::PropIcon, &Styles::PropIconDelta };
                case Toolpath:  return { &Styles::PropIcon, &Styles::PropIconToolpath };
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

        // The Operation property body lists the faces the operation referenced;
        // each row highlights its face in the world view on hover.
        Box* operationBody = nullptr;
        std::vector<Element*> operationFaceRows;
        Cam::App::Stage* operationFacesBoundState = nullptr;
        std::vector<std::size_t> operationFacesBound;
        int operationActiveSlotBound = -2;

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
                    // The operation body holds the list of referenced faces,
                    // populated dynamically in computeChildren, laid out
                    // horizontally as chips.
                    operationBody = propCollapsible[i]->container;
                    operationBody->styles.add(&Styles::OperationBody);
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

        // The model the operation's faces live on: the prior (parent) model,
        // whose face ids the operation references. Falls back to this stage's
        // own model at the root.
        Cam::App::Model* priorModel() {
            if (!state) { return nullptr; }
            return state->parent ? &state->parent->model : &state->model;
        }

        // Create a Face element bound to a Model::Face. Hovering highlights that
        // face in the world view; leaving restores the selection baseline. The
        // caller wires onSelect (slots and flat faces select differently).
        Cam::Gui::Face* makeFaceElement(Cam::App::Model::Face f, const std::string& label) {

            Cam::Gui::Face* el = new Cam::Gui::Face(operationBody);
            el->setFace(f, label);

            el->onHover = [this](Event& ev, Cam::App::Model::Face hf) {
                if (state && hf.valid()) {
                    state->highlightedOperationFaces = { hf.id };
                    if (onComponentToggled) { onComponentToggled(ev); }
                }
            };

            el->onUnhover = [this](Event& ev) {
                applyOperationHighlightBaseline();
                if (onComponentToggled) { onComponentToggled(ev); }
            };

            return el;
        }

        // (Re)build the Operation body. Operations with named face slots (e.g.
        // extrude) get one selectable/fillable Face row per slot; others list
        // their referenced faces. Each row is a true Face element carrying the
        // Model::Face it represents.
        void rebuildOperationFaces() {

            if (!operationBody) { return; }

            for (Element* row : operationFaceRows) {
                delete row;
            }
            operationFaceRows.clear();

            if (!state || !state->operation) {
                operationFaceRows.push_back(new Text(
                    operationBody, "No operation",
                    Theme::layer({ &Styles::FaceRow }, { &Theme::Styles::MutedText })
                ));
                return;
            }

            Cam::App::Model* pm = priorModel();

            auto handleFor = [&](int faceId) -> Cam::App::Model::Face {
                if (faceId >= 0 && pm) { return pm->face(static_cast<std::size_t>(faceId)); }
                return Cam::App::Model::Face{ pm, static_cast<std::size_t>(-1) };
            };

            std::vector<Cam::App::FaceSlot> slots = state->operation->faceSlots();

            if (!slots.empty()) {

                const int activeSlot =
                    (activeProject() && activeProject()->activeRefStage == state)
                        ? activeProject()->activeRefSlot
                        : -1;

                for (int s = 0; s < static_cast<int>(slots.size()); s++) {

                    const Cam::App::FaceSlot& slot = slots[s];
                    const int face = slot.face ? *slot.face : -1;
                    const bool active = (s == activeSlot);

                    std::string label =
                        (active ? std::string("> ") : std::string("")) +
                        slot.name + ": " +
                        (face >= 0 ? "Face " + std::to_string(face) : "pick a face");

                    Cam::Gui::Face* row = makeFaceElement(handleFor(face), label);

                    if (active) { row->setSelected(true); }

                    // Clicking a slot selects the stage + operation and activates
                    // the slot, so the next ctrl+click in the world view fills it.
                    const int capturedSlot = s;
                    row->onSelect = [this, capturedSlot](Event& e, Cam::App::Model::Face) {
                        if (Cam::App::Project* p = activeProject()) {
                            if (onSelect && state) { onSelect(e, state); }
                            p->selectComponent(state, Operation);
                            p->setActiveFaceReference(state, capturedSlot);
                            if (onComponentToggled) { onComponentToggled(e); }
                        }
                    };

                    operationFaceRows.push_back(row);
                }

                return;
            }

            // Flat referenced faces (defeature / extend).
            if (state->operation->referencedFaces.empty()) {
                operationFaceRows.push_back(new Text(
                    operationBody, "No referenced faces",
                    Theme::layer({ &Styles::FaceRow }, { &Theme::Styles::MutedText })
                ));
                return;
            }

            for (std::size_t fid : state->operation->referencedFaces) {

                Cam::Gui::Face* row = makeFaceElement(handleFor(static_cast<int>(fid)),
                                                      "Face " + std::to_string(fid));

                // Clicking a referenced face selects the stage + operation.
                row->onSelect = [this](Event& e, Cam::App::Model::Face) {
                    if (onSelect && state) { onSelect(e, state); }
                    if (Cam::App::Project* p = activeProject()) {
                        p->selectComponent(state, Operation);
                    }
                    applyOperationHighlightBaseline();
                    if (onComponentToggled) { onComponentToggled(e); }
                };

                operationFaceRows.push_back(row);
            }
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

            // Rebuild the operation body when the stage, its referenced faces, or
            // the active face slot changes.
            {
                std::vector<std::size_t> faces;
                if (state && state->operation) { faces = state->operation->referencedFaces; }

                Cam::App::Project* p = activeProject();
                const int activeSlot =
                    (p && p->activeRefStage == state) ? p->activeRefSlot : -1;

                if (state != operationFacesBoundState ||
                    faces != operationFacesBound ||
                    activeSlot != operationActiveSlotBound) {

                    operationFacesBoundState = state;
                    operationFacesBound = faces;
                    operationActiveSlotBound = activeSlot;
                    rebuildOperationFaces();
                }
            }

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
