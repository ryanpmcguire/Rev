module;

#include <string>
#include <optional>

#include <managed.hpp>

export module Cam.Gui.ImportOperationView;

import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;
import Rev.Element.NumberInput;

import Cam.App.Project;
import Cam.App.Stage;
import Cam.App.Operation;

import Cam.Gui.Theme;
import Cam.Gui.OperationView;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace ImportOpStyle {

        Style Body = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::True },
            .size = { .width = 100_pct }
        };

        Style SectionLabel = {
            .margin = { .right = 8_px },
            .text = { .size = 11_px, .wrap = Wrap::False }
        };

        Style AxisRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .margin = { .right = 6_px }
        };

        Style AxisLabel = {
            .margin = { .right = 3_px },
            .text = { .size = 11_px, .wrap = Wrap::False }
        };

        Style AxisInput = {
            .size = { .width = 52_px },
            .margin = { .top = 1_px, .bottom = 1_px }
        };

        Style LockButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { .left = 2_px },
            .padding = { .left = 2_px, .right = 2_px, .top = 2_px, .bottom = 2_px },
            .border = { .radius = 4_px },
            .cursor = Cursor::Hand
        };

        Style LockIcon = {
            .size = { 15_px, 15_px }
        };
    }

    // The Import operation's body: per-axis scale inputs plus a lock toggle that
    // ties the axes together (locked by default). Editing the scale re-imports
    // the source geometry at the new scale.
    struct ImportOperationView : public OperationView {

        NumberInput* axisInput[3] = {};
        Box* lockButton = nullptr;
        Svg* lockIcon = nullptr;

        Rev::Core::Resource lockClosed;
        Rev::Core::Resource lockOpen;

        Cam::App::Stage* boundState = nullptr;

        ImportOperationView(Element* parent)
            : OperationView(parent, { &ImportOpStyle::Body }, "ImportOperationView") {

            lockClosed = File("./Lock-Closed.svg");
            lockOpen   = File("./Lock-Open.svg");

            new Text(
                this, "Scale",
                Theme::layer({ &ImportOpStyle::SectionLabel }, { &Theme::Styles::MutedText })
            );

            axisInput[0] = makeAxis("X", 0);
            axisInput[1] = makeAxis("Y", 1);
            axisInput[2] = makeAxis("Z", 2);

            lockButton = new Box(this, { &ImportOpStyle::LockButton }, "ScaleLockButton");
            lockIcon = new Svg(
                lockButton, lockClosed,
                Theme::layer(
                    { &ImportOpStyle::LockIcon },
                    { &Theme::Styles::Icon, &Theme::Styles::IconHover }
                ),
                "ScaleLockIcon"
            );

            lockButton->onClick([this](Event& e) {
                e.propagate = false;
                toggleLock(e);
            });
        }

        Cam::App::ImportOperation* importOp() {
            if (!state || !state->operation) { return nullptr; }
            if (state->operation->type() != Cam::App::OperationType::Import) { return nullptr; }
            return static_cast<Cam::App::ImportOperation*>(state->operation);
        }

        NumberInput* makeAxis(const std::string& label, int axis) {

            Box* row = new Box(this, { &ImportOpStyle::AxisRow }, "ScaleAxisRow");

            new Text(
                row, label,
                Theme::layer({ &ImportOpStyle::AxisLabel }, { &Theme::Styles::MutedText })
            );

            NumberInput::Params params = NumberInput::Params::Default();
            params.label = "Scale " + label;
            params.placeholder = "1";
            params.allowNegative = false;
            params.allowEmpty = false;
            params.maxDecimalPlaces = 3;
            params.min = 0.001;

            NumberInput* input = new NumberInput(row, params, { &ImportOpStyle::AxisInput });

            input->onValueChange = [this, axis](Event& e, std::optional<double> v) {
                if (!v) { return; }
                onAxisEdited(e, axis, *v);
            };

            input->onKeyDown([input](Event& e) {
                if (e.keyboard.enter) {
                    e.propagate = false;
                    input->commit(e);
                }
            });

            return input;
        }

        void onAxisEdited(Event& e, int axis, double value) {

            Cam::App::ImportOperation* op = importOp();
            if (!op) { return; }

            double sx = op->scaleX, sy = op->scaleY, sz = op->scaleZ;

            if (op->scaleLocked) {
                sx = sy = sz = value;
            }
            else {
                if      (axis == 0) { sx = value; }
                else if (axis == 1) { sy = value; }
                else                { sz = value; }
            }

            if (Cam::App::Project* p = project()) {
                p->setImportScale(state, sx, sy, sz, op->scaleLocked);
            }

            refreshInputs();
            notifyChanged(e);
        }

        void toggleLock(Event& e) {

            Cam::App::ImportOperation* op = importOp();
            if (!op) { return; }

            const bool locked = !op->scaleLocked;

            double sx = op->scaleX, sy = op->scaleY, sz = op->scaleZ;

            // Re-tie the other axes to X when (re)locking.
            if (locked) { sy = sz = sx; }

            if (Cam::App::Project* p = project()) {
                p->setImportScale(state, sx, sy, sz, locked);
            }

            refreshInputs();
            notifyChanged(e);
        }

        // Reflect the operation's scale + lock state into the controls without
        // firing change callbacks (setValue with no event is silent).
        void refreshInputs() {

            Cam::App::ImportOperation* op = importOp();
            if (!op) { return; }

            if (axisInput[0]) { axisInput[0]->setValue(op->scaleX); }
            if (axisInput[1]) { axisInput[1]->setValue(op->scaleY); }
            if (axisInput[2]) { axisInput[2]->setValue(op->scaleZ); }

            if (lockIcon) {
                lockIcon->resource = op->scaleLocked ? lockClosed : lockOpen;
            }
        }

        void sync(Event& e) override {
            if (state != boundState) {
                boundState = state;
                refreshInputs();
            }
        }
    };
}
