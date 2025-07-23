module;

#include <vector>

export module Rev.Style;

export namespace Rev {

    // Size: 6
    struct Dist {

        enum Type {
            None, Abs, Rel,
            Grow, Shrink
        };

        Type type = Type::None;
        float val = -0.0f;
        bool dirty = true;

        // Dist is true if type is set
        explicit operator bool() {
            return type != None;
        }
    };

    // Pixel distance
    export Dist Px(float value) {
        return { Dist::Type::Abs, value };
    }

    // Percent distance
    export Dist Pct(float value) {
        return { Dist::Type::Rel, value / 100.0f };
    }

    export Dist Grow() {
        return { Dist::Type::Grow, 0 };
    }

    export Dist Shrink() {
        return { Dist::Type::Shrink, 0 };
    }

    struct Color {

        float r = -0.0f, g = -0.0f, b = -0.0f, a = -0.0f;
        
        explicit operator bool() {
            return (r != -0.0f || g != -0.0f || b != -0.0f || a != -0.0f);
        }
    };

    Color rgba(float r, float g, float b, float a) {
        return { r, g, b, a };
    }

    Color rgb(float r, float g, float b) {
        return { r, g, b, 1.0 };
    }

    struct Size {

        // Nominal
        Dist width;
        Dist height;
        
        // Min / max
        Dist minWidth, maxWidth;
        Dist minHeight, maxHeight;
        
        void apply(Size& size) {
            if (size.width) { width = size.width; }
            if (size.height) { height = size.height; }
        }
    };

    struct LrtbStyle {

        Dist left, maxLeft, minLeft;
        Dist right, maxRight, minRight;
        Dist top, maxTop, minTop;
        Dist bottom, maxBottom, minBottom;
    };

    enum Axis {
        Horizontal,
        Vertical
    };

    enum Align {
        Start, End, Center,
        SpaceAround, SpaceBetween
    };

    struct Alignment {

        Axis direction = Axis::Vertical;
        Align horizontal = Align::Start;
        Align vertical = Align::Start;
    };

    struct Background {

        Color color;

        void apply(Background& background) {
            if (background.color) { color = background.color; }
        }
    };

    export struct Shadow {

        Color color;

        Dist size;
        Dist blur;
        Dist x, y;
    };

    struct Border {

        struct Corner {

            Color color;
            Dist radius;
            Dist width;

            // Apply new style
            void apply(Corner& corner) {
                if (corner.color) { color = corner.color; }
                if (corner.radius) { radius = corner.radius; }
                if (corner.width) { radius = corner.width; }
            }
        };

        // Top-level
        Color color;
        Dist radius;
        Dist width;

        // Corners
        Corner tl, tr, bl, br;

        // Apply new style
        void apply(Border& border) {

            // Apply to self
            if (border.color) { color = border.color; }
            if (border.radius) { radius = border.radius; }
            if (border.width) { radius = border.width; }

            // Apply to corners
            tl.apply(border.tl); tr.apply(border.tr);
            bl.apply(border.bl); br.apply(border.br);
        }
    };

    struct Style {

        Size size;
        LrtbStyle margin;
        LrtbStyle padding;
        Alignment alignment;
        Border border;
        Background background;
        Shadow shadow;

        int transition = 0; // Transition time

        // Apply single style
        void apply(Style& style) {

            if (style.transition) { transition = style.transition; }

            size.apply(style.size);
            background.apply(style.background);
            border.apply(style.border);
        }

        // Apply vector of styles
        void apply(std::vector<Style*>& styles) {

            for (Style* style : styles) {
                apply(*style);
            }
        }
    };

    // For floating-point literals (e.g. 10.5_px)
    constexpr Dist operator"" _px(long double value) {
        return { Dist::Type::Abs, static_cast<float>(value) };
    }

    constexpr Dist operator"" _px(unsigned long long value) {
        return { Dist::Type::Abs, static_cast<float>(value) };
    }

    constexpr Dist operator"" _pct(long double value) {
        return { Dist::Type::Rel, static_cast<float>(value) / 100.0f };
    }

    constexpr Dist operator"" _pct(unsigned long long value) {
        return { Dist::Type::Rel, static_cast<float>(value) / 100.0f };
    }

    constexpr Dist operator"" _grow(unsigned long long value) {
        return { Dist::Type::Grow, static_cast<float>(value) / 100.0f };
    }

    constexpr Dist operator"" _grow(long double value) {
        return { Dist::Type::Grow, static_cast<float>(value) / 100.0f };
    }

    constexpr Dist operator"" _shrink(unsigned long long value) {
        return { Dist::Type::Grow, static_cast<float>(value) / 100.0f };
    }

    constexpr Dist operator"" _shrink(long double value) {
        return { Dist::Type::Grow, static_cast<float>(value) / 100.0f };
    }
}