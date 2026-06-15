module;

#include <cstddef>
#include <string>
#include <vector>
#include <optional>

export module Cam.Gui.FaceOperationView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.NumberInput;

import Cam.App.Project;
import Cam.App.Stage;
import Cam.App.Model;
import Cam.App.Operation;

import Cam.Gui.Theme;
import Cam.Gui.Face;
import Cam.Gui.OperationView;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace FaceOpStyle {

        // The body lays its sections horizontally: faces column, then params.
        Style Body = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
            .size = { .width = 100_pct }
        };

        Style FacesColumn = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False }
        };

        Style FaceSlotRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .padding = { .top = 1_px, .bottom = 1_px }
        };

        Style SlotLabel = {
            .margin = { .right = 6_px },
            .text = { .size = 11_px, .wrap = Wrap::False }
        };

        Style FlatFaces = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::True },
            .size = { .width = 100_pct }
        };

        Style ParamRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .margin = { .left = 12_px },
            .padding = { .top = 1_px, .bottom = 1_px }
        };

        Style ParamLabel = {
            .margin = { .right = 6_px },
            .text = { .size = 11_px, .wrap = Wrap::False }
        };

        Style OffsetInput = {
            .size = { .width = 56_px },
            .margin = { .top = 1_px, .bottom = 1_px }
        };

        Style EmptyRow = {
            .padding = { .left = 2_px, .right = 2_px, .top = 1_px, .bottom = 1_px },
            .text = { .size = 11_px, .wrap = Wrap::False }
        };
    }

    // The body for face-driven operations (extrude, defeature, extend). Renders
    // named face slots (with an optional offset input) or a flat list of
    // referenced-face chips, and drives hover-highlighting + slot filling.
    struct FaceOperationView : public OperationView {

        Cam::App::Stage* boundState = nullptr;
        std::vector<std::size_t> boundFaces;
        int boundActiveSlot = -2;

        std::vector<Element*> rows;

        FaceOperationView(Element* parent)
            : OperationView(parent, { &FaceOpStyle::Body }, "FaceOperationView") {}

        // The model the operation's faces live on (the prior/parent model).
        Cam::App::Model* priorModel() {
            if (!state) { return nullptr; }
            return state->parent ? &state->parent->model : &state->model;
        }

        bool operationSelected() {
            Cam::App::Project* p = project();
            return p && p->selectedComponentStage == state &&
                   p->selectedComponentIndex == OperationComponent;
        }

        // Keep the referenced faces highlighted while the operation is selected;
        // otherwise clear. Hover overrides this transiently.
        void applyHighlightBaseline() {
            if (!state) { return; }
            if (operationSelected() && state->operation) {
                state->highlightedOperationFaces = state->operation->referencedFaces;
            }
            else {
                state->highlightedOperationFaces.clear();
            }
        }

        void sync(Event& e) override {

            std::vector<std::size_t> faces;
            if (state && state->operation) { faces = state->operation->referencedFaces; }

            Cam::App::Project* p = project();
            const int activeSlot = (p && p->activeRefStage == state) ? p->activeRefSlot : -1;

            if (state != boundState || faces != boundFaces || activeSlot != boundActiveSlot) {
                boundState = state;
                boundFaces = faces;
                boundActiveSlot = activeSlot;
                rebuild();
            }
        }

        Cam::Gui::Face* makeFaceElement(Element* parent, Cam::App::Model::Face f, const std::string& label) {

            Cam::Gui::Face* el = new Cam::Gui::Face(parent);
            el->setFace(f, label);

            el->onHover = [this](Event& ev, Cam::App::Model::Face hf) {
                if (state && hf.valid()) {
                    state->highlightedOperationFaces = { hf.id };
                    notifyChanged(ev);
                }
            };

            el->onUnhover = [this](Event& ev) {
                applyHighlightBaseline();
                notifyChanged(ev);
            };

            return el;
        }

        virtual void rebuild() {

            for (Element* row : rows) { delete row; }
            rows.clear();

            if (!state || !state->operation) {
                rows.push_back(new Text(
                    this, "No operation",
                    Theme::layer({ &FaceOpStyle::EmptyRow }, { &Theme::Styles::MutedText })
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

                Cam::App::Project* p = project();
                const int activeSlot = (p && p->activeRefStage == state) ? p->activeRefSlot : -1;

                int offsetSlot = -1;
                double offsetValue = 0.0;

                Box* facesCol = new Box(this, { &FaceOpStyle::FacesColumn }, "FacesColumn");

                for (int s = 0; s < static_cast<int>(slots.size()); s++) {

                    const Cam::App::FaceSlot& slot = slots[s];
                    const int face = slot.face ? *slot.face : -1;
                    const bool active = (s == activeSlot);

                    if (slot.offset) {
                        offsetSlot = s;
                        offsetValue = *slot.offset;
                    }

                    Box* slotRow = new Box(facesCol, { &FaceOpStyle::FaceSlotRow }, "FaceSlotRow");

                    new Text(
                        slotRow,
                        (active ? std::string("> ") : std::string("")) + slot.name + ":",
                        Theme::layer({ &FaceOpStyle::SlotLabel }, { &Theme::Styles::MutedText })
                    );

                    Cam::Gui::Face* chip = makeFaceElement(
                        slotRow, handleFor(face),
                        face >= 0 ? "Face " + std::to_string(face) : "pick a face"
                    );

                    if (active) { chip->setSelected(true); }

                    const int capturedSlot = s;
                    chip->onSelect = [this, capturedSlot](Event& e, Cam::App::Model::Face) {
                        if (Cam::App::Project* pr = project()) {
                            selectStage(e);
                            pr->selectComponent(state, OperationComponent);
                            pr->setActiveFaceReference(state, capturedSlot);
                            notifyChanged(e);
                        }
                    };
                }

                rows.push_back(facesCol);

                if (offsetSlot >= 0) {

                    Box* paramRow = new Box(this, { &FaceOpStyle::ParamRow }, "OffsetRow");

                    new Text(
                        paramRow, "Offset (mm)",
                        Theme::layer({ &FaceOpStyle::ParamLabel }, { &Theme::Styles::MutedText })
                    );

                    NumberInput::Params params = NumberInput::Params::Default();
                    params.label = "Offset (mm)";
                    params.placeholder = "0";
                    params.maxDecimalPlaces = 3;

                    NumberInput* input = new NumberInput(
                        paramRow, params, { &FaceOpStyle::OffsetInput }
                    );

                    input->setValue(offsetValue);

                    const int capturedOffsetSlot = offsetSlot;
                    input->onValueChange =
                        [this, capturedOffsetSlot](Event& e, std::optional<double> v) {
                            if (!v) { return; }
                            if (Cam::App::Project* pr = project()) {
                                pr->setOperationSlotOffset(state, capturedOffsetSlot, *v);
                                notifyChanged(e);
                            }
                        };

                    input->onKeyDown([input](Event& e) {
                        if (e.keyboard.enter) {
                            e.propagate = false;
                            input->commit(e);
                        }
                    });

                    rows.push_back(paramRow);
                }

                return;
            }

            // Flat referenced faces (defeature / extend) flow as wrapping chips.
            if (state->operation->referencedFaces.empty()) {
                rows.push_back(new Text(
                    this, "No referenced faces",
                    Theme::layer({ &FaceOpStyle::EmptyRow }, { &Theme::Styles::MutedText })
                ));
                return;
            }

            Box* flatFaces = new Box(this, { &FaceOpStyle::FlatFaces }, "FlatFaces");

            for (std::size_t fid : state->operation->referencedFaces) {

                Cam::Gui::Face* row = makeFaceElement(flatFaces, handleFor(static_cast<int>(fid)),
                                                      "Face " + std::to_string(fid));

                row->onSelect = [this](Event& e, Cam::App::Model::Face) {
                    selectStage(e);
                    if (Cam::App::Project* pr = project()) {
                        pr->selectComponent(state, OperationComponent);
                    }
                    applyHighlightBaseline();
                    notifyChanged(e);
                };
            }

            rows.push_back(flatFaces);
        }
    };
}
