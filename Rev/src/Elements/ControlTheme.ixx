module;

export module Rev.Element.ControlTheme;

import Rev.Element.Style;

export namespace Rev::Element::ControlTheme {

    // Shared tokens for TextInput, Dropdown, Checkbox, Slider, Button
    //--------------------------------------------------

    Shadow subtleShadow = {
        .color = rgba(15, 23, 42, 0.06),
        .size = Px(-2),
        .blur = 8_px,
        .y = 2_px
    };

    Shadow primaryButtonShadow = {
        .color = rgba(79, 99, 255, 0.28),
        .size = Px(-4),
        .blur = 12_px,
        .y = 2_px
    };

    Style Control = {
        .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
        .size = { Grow() },
        .margin = { 0_px, 0_px, 12_px, 0_px }
    };

    Style Label = {
        .margin = { .bottom = 8_px },
        .text = { .color = rgba(100, 116, 139, 1.0), .size = 12_px }
    };

    Style Field = {
        .layout = { Axis::Horizontal, Align::Start, Align::Center, Wrap::False },
        .size = { Grow() },
        .padding = { 10_px, 12_px, 8_px, 10_px },
        .background = { .color = rgba(255, 255, 255, 1.0) },
        .border = {
            .color = rgba(226, 232, 240, 1.0),
            .width = 1_px,
            .radius = 4_px
        },
        .shadow = subtleShadow
    };

    Style FieldFocus = {
        .applies = { .focus = true },
        .border = {
            .color = rgba(79, 99, 255, 1.0),
            .width = 1_px
        }
    };

    Style FieldInner = {
        .layout = { Axis::Horizontal, Align::Start, Align::Start, Wrap::False },
        .size = { Grow() }
    };

    Style FieldText = {
        .size = { Grow() },
        .text = {
            .color = rgba(30, 41, 59, 1.0),
            .size = 14_px,
            .wrap = Wrap::BreakWord
        }
    };

    Style Placeholder = {
        .layout = { .position = Position::Absolute },
        .position = { .left = 0_px, .top = 0_px },
        .size = { 100_pct, 100_pct },
        .text = { .color = rgba(148, 163, 184, 1.0), .size = 14_px }
    };

    Style DropdownArrow = {
        .size = { 14_px, 14_px },
        .background = { .color = rgba(100, 116, 139, 1.0) }
    };

    Style OptionsContainer = {
        .visibility = { Visibility::Hidden },
        .overflow = Overflow::Hide,
        .layout = {
            .direction = Axis::Vertical,
            .vertical = Align::End,
            .wrap = Wrap::False,
            .position = Position::Absolute
        },
        .position = { .top = 100_pct },
        .size = { .width = Grow(), .max = { .width = 100_pct } },
        .margin = { .top = 6_px },
        .padding = { 4_px, 4_px, 4_px, 4_px },
        .background = { .color = rgba(255, 255, 255, 1.0) },
        .border = {
            .color = rgba(226, 232, 240, 1.0),
            .width = 1_px,
            .radius = 8_px
        },
        .shadow = subtleShadow,
        .zIndex = +2
    };

    Style Option = {
        .size = { 100_pct },
        .padding = { 10_px, 12_px, 8_px, 10_px },
        .background = { .color = rgba(255, 255, 255, 0.0), .transition = 100_ms },
        .text = { .color = rgba(30, 41, 59, 1.0), .size = 14_px },
        .cursor = Cursor::Hand
    };

    Style OptionHover = {
        .applies = { .hover = true, .focus = true },
        .background = { .color = rgba(241, 245, 249, 1.0) }
    };

    Style OptionDisabled = {
        .applies = { .disabled = true },
        .text = { .color = rgba(148, 163, 184, 1.0) },
        .cursor = Cursor::Default
    };

    Style ButtonSecondary = {
        .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
        .padding = { 8_px, 12_px, 4_px, 4_px },
        .margin = { 4_px, 4_px, 4_px, 4_px },
        .background = { .color = rgba(255, 255, 255, 1.0), .transition = 120_ms },
        .border = {
            .color = rgba(203, 213, 225, 1.0),
            .width = 1_px,
            .radius = 4_px
        },
        .cursor = Cursor::Hand
    };

    Style ButtonSecondaryHover = {
        .applies = { .hover = true, .focus = true },
        .background = { .color = rgba(248, 250, 252, 1.0) },
        .border = { .color = rgba(148, 163, 184, 1.0) }
    };

    Style ButtonSecondaryLabel = {
        .text = { .color = rgba(51, 65, 85, 1.0), .size = 14_px }
    };

    Style ButtonPrimary = {
        .layout = { Axis::Horizontal, Align::Center, Align::Center, Wrap::False },
        .padding = { 8_px, 12_px, 4_px, 4_px },
        .margin = { 4_px, 4_px, 4_px, 4_px },
        .background = { .color = rgba(79, 99, 255, 1.0), .transition = 120_ms },
        .border = { .radius = 4_px },
        .shadow = primaryButtonShadow,
        .cursor = Cursor::Hand
    };

    Style ButtonPrimaryHover = {
        .applies = { .hover = true, .focus = true },
        .background = { .color = rgba(96, 117, 255, 1.0) }
    };

    Style ButtonPrimaryLabel = {
        .text = { .color = rgba(255, 255, 255, 0.98), .size = 14_px }
    };

    Style CheckboxBox = {
        .layout = { Axis::Horizontal, Align::Center, Align::Center },
        .size = { 20_px, 20_px },
        .padding = { 2_px, 2_px, 2_px, 2_px },
        .background = { .color = rgba(255, 255, 255, 1.0) },
        .border = {
            .color = rgba(226, 232, 240, 1.0),
            .width = 1_px,
            .radius = 6_px
        },
        .shadow = subtleShadow
    };

    Style CheckboxFocus = {
        .applies = { .focus = true },
        .border = { .color = rgba(79, 99, 255, 1.0), .width = 1_px }
    };

    Style CheckboxChecked = {
        .background = { .color = rgba(79, 99, 255, 1.0) },
        .border = { .color = rgba(79, 99, 255, 1.0) }
    };

    Style CheckboxPress = {
        .applies = { .press = true },
        .background = { .color = rgba(67, 85, 220, 1.0) }
    };

    Style CheckboxMark = {
        .size = { 14_px, 14_px },
        .text = { .color = rgba(255, 255, 255, 1.0) }
    };

    Style SliderTrack = {
        .layout = { Axis::Horizontal, Align::Start, Align::Center },
        .size = { .width = 100_pct, .height = 2_px },
        .background = { .color = rgba(203, 213, 225, 1.0) }
    };

    Style SliderThumb = {
        .size = { .width = 4_px, .height = 14_px },
        .background = { .color = rgba(79, 99, 255, 1.0) },
        .border = { .radius = 2_px }
    };

    Style SliderThumbHover = {
        .applies = { .hover = true, .drag = true },
        .size = { .width = 6_px, .height = 16_px }
    };
}
