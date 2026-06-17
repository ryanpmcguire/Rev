module;

export module Cam.Gui.Form;

import Rev.Element;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.NumberInput;

import Cam.Gui.Theme;

// ------------------------------------------------------------------
// Cam::Gui::Form
//
// Composable form primitives shared by every tool-related window -- the tool
// settings window, the probe-calibration pop-up, the cutting-profile editor.
// Each is a thin builder over a Rev Element so a window COMPOSES sections, rows
// and fields instead of re-deriving the same layout.  Rev rewards composition:
// keep these dumb and reusable; windows own behaviour (binding, validation).
// ------------------------------------------------------------------

export namespace Cam::Gui::Form {

    using namespace Rev;
    using namespace Rev::Element;

    // A vertical menu pane.
    inline Style Column = {
        .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
        .size   = { .width = Grow() },
        .margin = { 0_px, 16_px, 0_px, 0_px }
    };

    // A horizontal row that lays fields side by side.
    inline Style Row = {
        .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
        .size   = { 100_pct }
    };

    // A field that grows to share its row evenly with siblings.
    inline Style Field = {
        .size   = { Grow() },
        .margin = { .left = 4_px, .right = 4_px }
    };

    // -- Builders ---------------------------------------------------

    inline Box* column(Box* parent, const char* name) {
        return new Box(parent, { &Column }, name);
    }

    inline Box* row(Box* parent, const char* name) {
        return new Box(parent, { &Row }, name);
    }

    // An uppercase section divider label (themed to match the settings panels).
    inline void section(Box* parent, const char* text) {
        new Text(parent, text, Theme::layer({}, { &Theme::Styles::SettingsSectionLabel }));
    }

    // The standard numeric-field params for mm / degree inputs.
    inline NumberInput::Params numberParams(const char* label, const char* placeholder) {
        NumberInput::Params p;
        p.label            = label;
        p.placeholder      = placeholder;
        p.maxLength        = 32;
        p.selectAllOnFocus = true;
        p.allowNegative    = false;
        p.allowDecimal     = true;
        p.allowEmpty       = false;
        p.maxDecimalPlaces = 4;
        return p;
    }

    // A labeled numeric field that grows to share its row.
    inline NumberInput* numberField(Box* parent, const char* label, const char* placeholder) {
        return new NumberInput(parent, numberParams(label, placeholder), { &Field });
    }
}
