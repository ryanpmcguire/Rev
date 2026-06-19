module;

#include <string>
#include <optional>
#include <functional>

#include <managed.hpp>

export module Cam.Gui.LockableNumberInput;

import Rev.Core.Resource;
import Rev.OS.File;

import Rev.Element;
import Rev.Element.Event;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Svg;
import Rev.Element.NumberInput;

import Cam.Gui.Theme;

export namespace Cam::Gui {

    using namespace Rev;
    using namespace Rev::Element;

    namespace LockableInputStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = Grow() }
        };

        Style LabelRow = {
            .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
            .size = { .width = Grow() },
            .margin = { .bottom = 2_px }
        };

        Style LabelText = {
            .text = { .size = 11_px, .wrap = Wrap::False }
        };

        Style LockButton = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
            .margin = { .left = 4_px },
            .padding = { .left = 1_px, .right = 1_px, .top = 1_px, .bottom = 1_px },
            .border = { .radius = 3_px },
            .cursor = Cursor::Hand
        };

        Style LockIcon = {
            .size = { 11_px, 11_px }
        };

        // Collapse the NumberInput's own (unused) label -- we render our own.
        Style CollapsedLabel = {
            .visibility = { Visibility::Hidden },
            .size = { 0_px, 0_px },
            .margin = { .top = 0_px, .bottom = 0_px }
        };

        Style Field = {
            .margin = { .top = 0_px, .bottom = 0_px }
        };
    };

    // A composable numeric input with a per-field LOCK toggle shown next to its
    // label.  Locked = display-only (the value is shown, not editable); the user
    // can click the lock to unlock and edit, or lock again.  Wraps NumberInput so
    // all parsing/validation/value behaviour is inherited; the lock simply drives
    // NumberInput's disabled state.
    struct LockableNumberInput : public Box {

        Text* labelText = nullptr;
        Box*  lockButton = nullptr;
        Svg*  lockIcon = nullptr;
        NumberInput* field = nullptr;

        Rev::Core::Resource lockClosed;
        Rev::Core::Resource lockOpen;

        std::function<void(Event&, std::optional<double>)> onValueChange;
        std::function<void(Event&)> onLockChanged;

        LockableNumberInput(
            Element* parent,
            const std::string& label,
            bool lockedByDefault,
            StyleList styles = {}
        ) : Box(parent, styles, "LockableNumberInput") {

            this->styles.prepend(&LockableInputStyle::Self);

            lockClosed = File("CAM/source/Gui/LeftPanel/Stages/Operations/Lock-Closed.svg");
            lockOpen   = File("CAM/source/Gui/LeftPanel/Stages/Operations/Lock-Open.svg");

            Box* labelRow = new Box(this, { &LockableInputStyle::LabelRow }, "LockableLabelRow");

            labelText = new Text(
                labelRow, label,
                Theme::layer({ &LockableInputStyle::LabelText }, { &Theme::Styles::Text })
            );

            lockButton = new Box(labelRow, { &LockableInputStyle::LockButton }, "LockableLockButton");
            lockIcon = new Svg(
                lockButton,
                lockedByDefault ? lockClosed : lockOpen,
                Theme::layer({ &LockableInputStyle::LockIcon }, { &Theme::Styles::Icon, &Theme::Styles::IconHover }),
                "LockableLockIcon"
            );

            lockButton->onClick([this](Event& e) {
                e.propagate = false;
                setLocked(!locked());
                if (onLockChanged) { onLockChanged(e); }
                refresh(e);
            });

            NumberInput::Params p = NumberInput::Params::Default();
            p.label = "";                 // our own label above; collapse the built-in one
            p.placeholder = "0";
            p.maxDecimalPlaces = 3;
            p.disabled = lockedByDefault;

            field = new NumberInput(this, p, { &LockableInputStyle::Field });
            if (field->label) { field->label->styles.add(&LockableInputStyle::CollapsedLabel); }

            field->onValueChange = [this](Event& e, std::optional<double> v) {
                if (onValueChange) { onValueChange(e, v); }
            };
        }

        // Lock state == NumberInput disabled.
        bool locked() const { return field && field->disabled; }
        void setLocked(bool b) { if (field) { field->setDisabled(b); } }

        void setValue(double v) { if (field) { field->setValue(v); } }
        double valueOr(double fallback) const { return field ? field->valueOr(fallback) : fallback; }
        std::optional<double> value() const { return field ? field->value() : std::nullopt; }

        void computeStyle(Event& e) override {
            if (lockIcon) { lockIcon->resource = locked() ? lockClosed : lockOpen; }
            Element::computeStyle(e);
        }
    };
}
