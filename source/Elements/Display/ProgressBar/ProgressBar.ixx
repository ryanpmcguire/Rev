module;

#include <string>
#include <algorithm>
#include <utility>
#include <vector>

export module Rev.Element.ProgressBar;

import Rev.Element;
import Rev.Appearance;

import Rev.Element.Box;
import Rev.Element.Text;

export namespace Rev::Element {

    namespace ProgressBarStyle {

        Style Self = {
            .layout = { Axis::Vertical, Align::Start, Align::Start, Wrap::False },
            .size = { .width = Grow(), .min = { .width = 100_px } }
        };

        Style Title = {
            .margin = { .bottom = 4_px },
            .text = { .color = rgba(100, 116, 139, 1.0), .size = 11_px }
        };

        // The bar: border, radius, height, padding and the empty-region colour.
        Style Bar = {
            .overflow = Overflow::Hide,
            .layout = { .direction = Axis::Horizontal, .horizontal = Align::Center,
                        .vertical = Align::Center, .wrap = Wrap::False,
                        .position = Position::Relative },
            .size = { .width = 100_pct, .height = 18_px },
            .background = { .color = rgba(255, 255, 255, 0.0) },
            .border = { .color = rgba(203, 213, 225, 1.0), .radius = 3_px, .width = 1_px }
        };

        // The progressed region; its width is set per instance from the value.
        Style Fill = {
            .layout = { .position = Position::Absolute },
            .position = { .left = 0_px, .top = 0_px },
            .size = { .width = 0_pct, .height = 100_pct },
            .background = { .color = rgba(79, 99, 255, 1.0) }
        };

        // Drawn once, over both regions.
        Style Label = {
            .text = { .color = rgba(30, 41, 59, 1.0), .size = 11_px, .wrap = Wrap::False }
        };

    }

    struct ProgressBar : public Element {

        struct Theme {
            StyleList root;
            StyleList title;
            StyleList bar;
            StyleList fill;
            StyleList label;
        };

        static Theme DefaultTheme() {
            return {
                .root = {&ProgressBarStyle::Self},
                .title = {&ProgressBarStyle::Title},
                .bar = {&ProgressBarStyle::Bar},
                .fill = {&ProgressBarStyle::Fill},
                .label = {&ProgressBarStyle::Label}
            };
        }

        Text* title = nullptr;
        Box* bar = nullptr;
            Box* fill = nullptr;
            Text* label = nullptr;

        float value = 0.0f;              // progress in [0, 1]
        Style fillWidth;                 // this instance's fill width, layered last on the fill
        std::vector<Theme> themes;

        ProgressBar(
            Element* parent,
            std::vector<Theme> requestedThemes,
            std::string titleText = "",
            StyleList styles = {},
            std::string name = "ProgressBar"
        ) : Element(parent) {

            this->name = std::move(name);
            themes.push_back(DefaultTheme());

            for (Theme& requestedTheme : requestedThemes) {
                themes.push_back(std::move(requestedTheme));
            }

            append(this, &Theme::root);
            append(this, styles);

            title = new Text(this, std::move(titleText));
            append(title, &Theme::title);

            bar = new Box(this, {}, "Bar");
            append(bar, &Theme::bar);
                fill = new Box(bar, {}, "Fill");
                append(fill, &Theme::fill);
                fillWidth.size.width = 0_pct;
                fill->styles.add(&fillWidth);
                label = new Text(bar, "");
                append(label, &Theme::label);
        }

        ProgressBar(
            Element* parent,
            Theme requestedTheme,
            std::string titleText = "",
            StyleList styles = {},
            std::string name = "ProgressBar"
        ) : ProgressBar(
            parent,
            std::vector<Theme>{std::move(requestedTheme)},
            std::move(titleText),
            std::move(styles),
            std::move(name)
        ) {}

        ProgressBar(
            Element* parent,
            std::string titleText = "",
            StyleList styles = {},
            std::string name = "ProgressBar"
        ) : ProgressBar(
            parent,
            std::vector<Theme>{},
            std::move(titleText),
            std::move(styles),
            std::move(name)
        ) {}

        static void append(Element* element, const StyleList& styles) {
            for (Style* style : styles.styles) {
                element->styles.add(style);
            }
        }

        void append(Element* element, StyleList Theme::* member) {
            for (Theme& theme : themes) {
                append(element, theme.*member);
            }
        }

        // Easing comes from the theme: give the fill style a `.size.transition`.
        void setValue(float newValue) {

            float clamped = std::clamp(newValue, 0.0f, 1.0f);
            if (clamped == value) { return; }

            value = clamped;
            fillWidth.size.width = Pct(100.0f * value);
            fillWidth.dirty = true;
        }

        void setText(std::string text) {
            label->setContent(std::move(text));
        }

        void setTitle(std::string text) {
            title->setContent(std::move(text));
        }
    };
};
