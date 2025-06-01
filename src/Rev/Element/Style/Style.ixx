module;

#include <vector>

export module Rev.Style;

import Dirty;

using namespace Dirty;

export namespace Rev {

    // Size: 6
    struct Dist {

        enum Type {
            None, Abs, Rel,
            Grow, Shrink
        };

        Type type = Type::None;
        float val = -0.0f;
        bool dirty = false;

        bool set(const Dist& other) {
            
            if (!other) { return false; }
            dirty = dirty | *this != other;
            
            type = other.type;
            val = other.val;

            return dirty;
        }

        // Dist is true if type is set
        explicit operator bool() const {
            return type != None;
        }

        bool operator == (const Dist& other) const {
            return type == other.type && val == other.val;
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

        bool dirty = false;
        float r = -0.0f, g = -0.0f, b = -0.0f, a = -0.0f;

        explicit operator bool() const {
            return (r != -0.0f || g != -0.0f || b != -0.0f || a != -0.0f);
        }

        bool operator == (const Color& other) const {
            return r == other.r && g == other.g && b == other.b && a == other.a;
        }

        bool set(const Color& other) {
            
            if (!other) { return false; }
            dirty = dirty | *this != other;
            
            r = other.r; g = other.g;
            b = other.b; a = other.a;

            return dirty;
        }
    };

    struct Size {

        bool dirty = false;
        Dist width, height;
        
        bool apply(Size& other) {

            if (other.width) { dirty = dirty | width.set(other.width); }
            if (other.height) { dirty = dirty | height.set(other.height); }

            return dirty;
        }
    };

    struct Background {

        bool dirty = false;
        Color color;

        bool apply(Background& other) {

            if (other.color) { dirty = dirty | color.set(other.color); }

            return dirty;
        }
    };

    struct Border {

        struct Corner {

            bool dirty = false;

            Color color;
            Dist radius;
            Dist width;

            // Apply new style
            bool apply(Corner& other) {

                if (other.color) { dirty = dirty | color.set(other.color); }
                if (other.radius) { dirty = dirty | radius.set(other.radius); }
                if (other.width) { dirty = dirty | width.set(other.width); }

                return dirty;
            }
        };

        bool dirty = false;

        // Top-level
        Color color;
        Dist radius;
        Dist width;

        // Corners
        Corner tl, tr, bl, br;

        // Apply new style
        bool apply(Border& other) {

            // Apply to self
            if (other.color) { dirty = dirty | color.set(other.color); }
            if (other.radius) { dirty = dirty | radius.set(other.radius); }
            if (other.width) { dirty = dirty | width.set(other.width); }

            // Apply to corners
            dirty = dirty | tl.apply(other.tl) | tr.apply(other.tr) |
                             bl.apply(other.bl)| br.apply(other.br);

            return dirty;
        }
    };

    struct Style {

        DirtyFlag dirty;

        Size size;
        Background background;
        Border border;

        int transition = 0; // Transition time

        // Apply single style
        bool apply(Style& style) {

            if (style.transition) { transition = style.transition; }

            dirty = dirty | size.apply(style.size);
            dirty = dirty | background.apply(style.background);
            dirty = dirty | border.apply(style.border);

            return dirty;
        }

        // Apply vector of styles
        bool apply(std::vector<Style*>& styles) {

            for (Style* style : styles) {
                dirty = dirty | apply(*style);
            }

            return dirty;
        }
    };
}