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
    Dist Px(float value) {
        return { Dist::Type::Abs, value };
    }

    // Percent distance
    Dist Pct(float value) {
        return { Dist::Type::Rel, value / 100.0f };
    }

     Dist Grow() {
        return { Dist::Type::Grow, 0 };
    }

     Dist Shrink() {
        return { Dist::Type::Shrink, 0 };
    }

    struct Color {

        float r = -0.0f, g = -0.0f, b = -0.0f, a = -0.0f;
        
        explicit operator bool() {
            return (r != -0.0f || g != -0.0f || b != -0.0f || a != -0.0f);
        }
    };

    struct Size {

        Dist width, height;
        
        void apply(Size& size) {
            if (size.width) { width = size.width; }
            if (size.height) { height = size.height; }
        }
    };

    struct Background {

        Color color;

        void apply(Background& background) {
            if (background.color) { color = background.color; }
        }
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
        Background background;
        Border border;

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
}