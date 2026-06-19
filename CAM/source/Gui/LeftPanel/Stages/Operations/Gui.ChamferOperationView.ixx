module;

#include <string>
#include <optional>

export module Cam.Gui.ChamferOperationView;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.NumberInput;

import Cam.App.Project;
import Cam.App.Stage;
import Cam.App.Operation;

import Cam.Gui.Theme;
import Cam.Gui.FaceOperationView;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ChamferOpStyle {

        Style Column = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .margin = { .left = 12_px }
        };

        Style Row = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .padding = { .top = 1_px, .bottom = 1_px }
        };
    }

    // The chamfer operation's feature body: the standard face slot(s) (the chamfer
    // faces), inherited from FaceOperationView, plus the inferred bevel ANGLE,
    // editable here.  Changing it mirrors onto the toolpath (Project::setChamferAngle).
    struct ChamferOperationView : public FaceOperationView {

        NumberInput* angleInput = nullptr;

        ChamferOperationView(Element* parent) : FaceOperationView(parent) {}

        Cam::App::ChamferOperation* chamferOp() {
            if (!state || !state->operation) { return nullptr; }
            if (state->operation->type() != Cam::App::OperationType::Chamfer) { return nullptr; }
            return static_cast<Cam::App::ChamferOperation*>(state->operation);
        }

        void commitAngle(Event& e) {
            Cam::App::ChamferOperation* o = chamferOp();
            if (!o) { return; }
            const double angle = angleInput ? angleInput->valueOr(o->chamferAngle) : o->chamferAngle;
            if (Cam::App::Project* p = project()) { p->setChamferAngle(state, angle); }
            notifyChanged(e);
        }

        void rebuild() override {

            FaceOperationView::rebuild();   // the chamfer face chips

            angleInput = nullptr;

            Cam::App::ChamferOperation* o = chamferOp();
            if (!o) { return; }

            Box* col = new Box(this, { &ChamferOpStyle::Column }, "ChamferCalloutColumn");
            rows.push_back(col);

            Box* row = new Box(col, { &ChamferOpStyle::Row }, "ChamferAngleRow");
            new Text(
                row, "Angle (deg)",
                Theme::layer({ &FaceOpStyle::ParamLabel }, { &Theme::Styles::MutedText })
            );

            NumberInput::Params params = NumberInput::Params::Default();
            params.label = "Angle (deg)";
            params.placeholder = "45";
            params.maxDecimalPlaces = 2;

            angleInput = new NumberInput(row, params, { &FaceOpStyle::OffsetInput });
            angleInput->setValue(o->chamferAngle);
            angleInput->onValueChange = [this](Event& e, std::optional<double>) { commitAngle(e); };
            angleInput->onKeyDown([this](Event& e) {
                if (e.keyboard.enter) { e.propagate = false; commitAngle(e); }
            });
        }
    };
}
