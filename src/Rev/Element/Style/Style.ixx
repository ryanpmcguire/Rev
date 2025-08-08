module;

#include <bit>
#include <cstdint>
#include <vector>

export module Rev.Style;

export namespace Rev {
      
    // Returns if value is set
    bool set(float& f) {

        bool isSet = std::bit_cast<uint32_t>(f) != 0x80000000;

        return isSet;
    };

    // Transitions
    //--------------------------------------------------
    
    struct Transition {

        // Target
        float* subject; int time;

        // Interp
        float startVal, endVal;
        uint64_t startTime, endTime;
    };

    // Distance
    //--------------------------------------------------

    // Size: 6
    struct Dist {

        enum Type {
            None, Abs, Rel,
            Grow, Shrink
        };

        Type type = Type::None;
        float val = -0.0f;
        bool dirty = true;

        int transition = -1;

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

    // Color
    //--------------------------------------------------

    struct Color {

        float r = -0.0f, g = -0.0f, b = -0.0f, a = -0.0f;
        int transition = -1;

        explicit operator bool() {
            return (set(r) || set(g) || set(b) || set(a));
        }
    };

    Color rgba(float r, float g, float b, float a) {
        return { r, g, b, a };
    }

    Color rgb(float r, float g, float b) {
        return { r, g, b, 1.0 };
    }
    
    // Size
    //--------------------------------------------------

    struct Size {

        // Nominal
        Dist width;
        Dist height;
        
        // Min / max
        Dist minWidth, maxWidth;
        Dist minHeight, maxHeight;
        
        int transition = -1;

        void apply(Size& size) {
            if (size.width) { width = size.width; }
            if (size.height) { height = size.height; }
            if (size.minWidth) { minWidth = size.minWidth; }
            if (size.minHeight) { minHeight = size.minHeight; }
            if (size.maxHeight) { maxHeight = size.maxHeight; }
        }
    };

    // Left-right-top-bottom style
    //--------------------------------------------------

    struct LrtbStyle {

        Dist left, right, top, bottom;
        Dist minLeft, minRight, minTop, minBottom;
        Dist maxLeft, maxRight, maxTop, maxBottom;

        int transittion = 0;

        void apply(LrtbStyle& lrtb) {
            if (lrtb.left) { left = lrtb.left; }
            if (lrtb.right) { right = lrtb.right; }
            if (lrtb.top) { top = lrtb.top; }
            if (lrtb.bottom) { bottom = lrtb.bottom; }
        }
    };

    // Alignment
    //--------------------------------------------------

    enum Axis {
        NoAxis,
        Horizontal,
        Vertical
    };

    enum Align {
        NoAlign,
        Start, End, Center,
        SpaceAorund, SpaceBetween
    };

    struct Alignment {

        Axis direction = Axis::NoAxis;
        Align horizontal = Align::NoAlign;
        Align vertical = Align::NoAlign;

        void apply(Alignment& other) {
            if (other.direction != Axis::NoAxis) { direction = other.direction; }
            if (other.horizontal != Align::NoAlign) { horizontal = other.horizontal; }
            if (other.vertical != Align::NoAlign) { vertical = other.vertical; }
        }
    };

    // Background and shadow
    //--------------------------------------------------

    struct Background {

        Color color;

        int transition = -1;

        void apply(Background& background) {
            if (background.color) { color = background.color; }
        }
    };

    export struct Shadow {

        Color color;

        Dist size;
        Dist blur;
        Dist x, y;

        int transition = -1;
    };

    // Border
    //--------------------------------------------------

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

        int transition = -1;

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

    // Cursor
    //--------------------------------------------------

    enum Cursor {
        Unset,
        Arrow,
        Caret,
        Crosshair,
        Hand,
        NotAllowed,
        ArrowsHorizontal,
        ArrowsVertical,
        ArrowsDiagonalUp,
        ArrowsDiagonalDown,
        ArrowsOmni
    };

    // Style per-se
    //--------------------------------------------------

    struct Style {

        LrtbStyle position; bool absolute = false;
        Size size;
        LrtbStyle margin;
        LrtbStyle padding;
        Alignment alignment;
        Border border;
        Background background;
        Shadow shadow;
        Cursor cursor;

        int transition = -1; // Transition time

        // Apply single style
        void apply(Style& style) {

            if (style.transition) { transition = style.transition; }

            size.apply(style.size);
            background.apply(style.background);
            border.apply(style.border);
            margin.apply(style.margin);
            padding.apply(style.padding);
            alignment.apply(style.alignment);
        }

        // Apply vector of styles
        void apply(std::vector<Style*>& styles) {

            for (Style* style : styles) {
                apply(*style);
            }
        }
    };

    struct StylePtr {

        Style* pStyle = nullptr;

        ~StylePtr() {
            if (pStyle) { delete pStyle; };
            pStyle = nullptr;
        }

        explicit operator bool() const {
            return pStyle != nullptr;
        }

        Style& operator*() {
            if (!pStyle) { pStyle = new Style(); }
            return *pStyle;
        }

        Style* operator->() {
            if (!pStyle) { pStyle = new Style(); }
            return pStyle;
        }

        // Assigning from a style
        StylePtr& operator=(const Style& other) {
            if (!pStyle) { pStyle = new Style(); }
            *pStyle = other;
            return *this;
        }

        // Assigning from a pointer to a style
        StylePtr& operator=(Style* pOther) {
            pStyle = pOther;
            return *this;
        }

        // Assigning from a style pointer
        StylePtr& operator=(StylePtr& other) {
            pStyle = other.pStyle;
            return *this;
        }

        // Casting to a style pointer
        operator Style*() {
            if (!pStyle) { pStyle = new Style(); }
            return pStyle;
        }

        // Casting to a style reference
        operator Style&() {
            if (!pStyle) { pStyle = new Style(); }
            return *pStyle;
        }

        // Applying from a style
        void apply(Style& other) {
            if (!pStyle) { pStyle = new Style(); }
            pStyle->apply(other);
        }

        // Applying from a style pointer
        void apply(StylePtr& other) {
            if (!pStyle) { pStyle = new Style(); }
            if (other.pStyle) { pStyle->apply(*(other.pStyle)); }
        }
    };
}