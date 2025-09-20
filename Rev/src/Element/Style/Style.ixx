module;

#include <bit>
#include <cstdint>
#include <cmath>
#include <vector>

export module Rev.Style;

import Resource;

export namespace Rev {

    // Returns if value is set
    inline bool set(float& f) {
        return std::bit_cast<uint32_t>(f) != 0x80000000;
    };

    inline bool equ(float& a, float& b) {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    }

    inline bool dif(float& a, float& b) {
        return std::bit_cast<std::uint32_t>(a) != std::bit_cast<std::uint32_t>(b);
    }

    // Transitions
    //--------------------------------------------------
    
    struct Transition {

        // Target
        float* subject;

        // Interp
        float startVal, endVal;
        uint64_t startTime, endTime;

        static float ease(float& a, float& b, float& t) {

            float sqr = t * t;
            float t_adj = sqr / (2.0f * (sqr - t) + 1.0f);

            return std::lerp(a, b, t_adj);
        }

        static void createNew(float& newVal, float& oldVal, std::vector<Transition>& transitions, uint64_t& time, int& ms) {

            // Search for redundant transitions
            for (Transition& transition : transitions) {

                if (transition.subject == &newVal) {

                    if (transition.endVal != newVal) {                        
                        transition.startVal = oldVal;
                        transition.endVal = newVal;
                        transition.startTime = time;
                        transition.endTime = time + ms;
                    }

                    return;
                }
            }

            // Create new transition
            transitions.push_back({
                &newVal,
                oldVal, newVal,
                time, time + ms
            });
        }
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

        int transition = -1;

        inline void animate(Dist& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {

            int transitionLength = transition > 0 ? transition : ms;
            if (transitionLength < 1) { return; }

            if (val != old.val) { Transition::createNew(val, old.val, transitions, time, transitionLength); }
        }

        inline void apply(Dist& other) {

            if (other.type) {
                *this = other;
            }
        }

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

        // Apply other color to this one
        inline void apply(Color& other) {
            if (other) { *this = other; }
        }

        void animate(Color& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {

            int transitionLength = transition > 1 ? transition : ms;
            if (transitionLength < 1) { return; }
            
            if (r != old.r) { Transition::createNew(r, old.r, transitions, time, transitionLength); }
            if (g != old.g) { Transition::createNew(g, old.g, transitions, time, transitionLength); }
            if (b != old.b) { Transition::createNew(b, old.b, transitions, time, transitionLength); }
            if (a != old.a) { Transition::createNew(a, old.a, transitions, time, transitionLength); }
        }

        explicit operator bool() {
            return (set(r) || set(g) || set(b) || set(a));
        }
    };

    Color rgba(float r, float g, float b, float a) {
        return { r / 255.0f, g / 255.0f, b / 255.0f, a };
    }

    Color rgb(float r, float g, float b) {
        return { r / 255.0f, g / 255.0f, b / 255.0f, 1.0 };
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

        // Apply other size to this one
        inline void apply(Size& size) {

            width.apply(size.width); height.apply(size.height);
            minWidth.apply(size.minWidth); maxWidth.apply(size.maxWidth);
            minHeight.apply(size.minHeight); maxHeight.apply(size.maxHeight);

            if (size.transition > 0) { transition = size.transition; }
        }

        inline void animate(Size& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {

            int transitionLength = transition > 1 ? transition : ms;

            // Animate nominal width/height
            width.animate(old.width, transitions, time, transitionLength);
            height.animate(old.height, transitions, time, transitionLength);

            // Animate min/max width
            minWidth.animate(old.minWidth, transitions, time, transitionLength);
            maxWidth.animate(old.maxWidth, transitions, time, transitionLength);

            // Animate min/max height
            minHeight.animate(old.minHeight, transitions, time, transitionLength);
            maxHeight.animate(old.maxHeight, transitions, time, transitionLength);
        }
    };

    // Left-right-top-bottom style
    //--------------------------------------------------

    struct LrtbStyle {

        Dist left, right, top, bottom;
        Dist minLeft, minRight, minTop, minBottom;
        Dist maxLeft, maxRight, maxTop, maxBottom;

        int transition = -1;

        // Apply other LRTB style (margin/padding) to this one
        inline void apply(LrtbStyle& lrtb) {

            // Apply nominal
            left.apply(lrtb.left); right.apply(lrtb.right);
            top.apply(lrtb.top); bottom.apply(lrtb.bottom);

            // Apply minima
            minLeft.apply(lrtb.minLeft); minRight.apply(lrtb.minRight);
            minTop.apply(lrtb.minTop); minBottom.apply(lrtb.minBottom);

            // Apply maxima
            maxLeft.apply(lrtb.maxLeft); maxRight.apply(lrtb.maxRight);
            maxTop.apply(lrtb.maxTop); maxBottom.apply(lrtb.maxBottom);

            if (lrtb.transition > 0) { transition = lrtb.transition; }
        }

        inline void animate(LrtbStyle& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {

            int transitionLength = transition > 1 ? transition : ms;

            // Animating the margins/paddings
            left.animate(old.left, transitions, time, transitionLength);
            right.animate(old.right, transitions, time, transitionLength);
            top.animate(old.top, transitions, time, transitionLength);
            bottom.animate(old.bottom, transitions, time, transitionLength);
        
            // Animating the minimum values
            minLeft.animate(old.minLeft, transitions, time, transitionLength);
            minRight.animate(old.minRight, transitions, time, transitionLength);
            minTop.animate(old.minTop, transitions, time, transitionLength);
            minBottom.animate(old.minBottom, transitions, time, transitionLength);
        
            // Animating the maximum values
            maxLeft.animate(old.maxLeft, transitions, time, transitionLength);
            maxRight.animate(old.maxRight, transitions, time, transitionLength);
            maxTop.animate(old.maxTop, transitions, time, transitionLength);
            maxBottom.animate(old.maxBottom, transitions, time, transitionLength);
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

    enum Break {
        NoBreak,
        False,
        True
    };

    struct Alignment {

        Axis direction = Axis::NoAxis;
        Align horizontal = Align::NoAlign;
        Align vertical = Align::NoAlign;
        Break breakWrap = Break::NoBreak;

        inline void apply(Alignment& other) {
            if (other.direction != Axis::NoAxis) { direction = other.direction; }
            if (other.horizontal != Align::NoAlign) { horizontal = other.horizontal; }
            if (other.vertical != Align::NoAlign) { vertical = other.vertical; }
            if (other.breakWrap != Break::NoBreak) { breakWrap = other.breakWrap; }
        }
    };

    // Background and shadow
    //--------------------------------------------------

    struct Background {

        Color color;

        int transition = -1;

        inline void apply(Background& background) {
            if (background.color) { color = background.color; }
            if (background.transition > 0) { transition = background.transition; }
        }

        inline void animate(Background& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {
            
            int transitionLength = transition > 0 ? transition : ms;

            color.animate(old.color, transitions, time, transitionLength);
        }
    };

    struct Shadow {

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

            int transition = -1;

            // Apply new style
            inline void apply(Corner& corner) {
                if (corner.color) { color = corner.color; }
                if (corner.radius) { radius = corner.radius; }
                if (corner.width) { radius = corner.width; }
            }

            inline void animate(Corner& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {

                int transitionLength = transition > 0 ? transition : ms;

                color.animate(old.color, transitions, time, transitionLength);
                radius.animate(old.radius, transitions, time, transitionLength);
                width.animate(old.radius, transitions, time, transitionLength);
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
        inline void apply(Border& border) {

            // Apply to self
            if (border.color) { color = border.color; }
            if (border.radius) { radius = border.radius; }
            if (border.width) { radius = border.width; }

            // Apply to corners
            tl.apply(border.tl); tr.apply(border.tr);
            bl.apply(border.bl); br.apply(border.br);
        }

        inline void animate(Border& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {
            
            int transitionLength = transition > 0 ? transition : ms;

            color.animate(old.color, transitions, time, transitionLength);
            radius.animate(old.radius, transitions, time, transitionLength);
            width.animate(old.radius, transitions, time, transitionLength);

            tl.animate(old.tl, transitions, time, transitionLength);
            tr.animate(old.tr, transitions, time, transitionLength);
            bl.animate(old.bl, transitions, time, transitionLength);
            br.animate(old.br, transitions, time, transitionLength);
        }
    };

    // Font
    //--------------------------------------------------

    struct TextStyle {

        Resource font;
        Dist size;
        int weight = -1;
        Color color;
        Dist lineHeight;
        Dist spacing;

        int transition = -1;

        inline void apply(TextStyle& other) {

            size.apply(other.size);
            color.apply(other.color);
            lineHeight.apply(other.lineHeight);
            spacing.apply(other.spacing);
        }

        inline void animate(TextStyle& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {

            int transitionLength = transition > 0 ? transition : ms;

            color.animate(old.color, transitions, time, transitionLength);
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
        TextStyle text;
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
            text.apply(style.text);
        }

        // Apply vector of styles
        void apply(std::vector<Style*>& styles) {

            for (Style* style : styles) {
                apply(*style);
            }
        }

        void animate(Style& old, std::vector<Transition>& transitions, uint64_t& time) {
            size.animate(old.size, transitions, time, transition);
            background.animate(old.background, transitions, time, transition);
            border.animate(old.border, transitions, time, transition);
            margin.animate(old.margin, transitions, time, transition);
            padding.animate(old.padding, transitions, time, transition);
            text.animate(old.text, transitions, time, transition);
        }
    };

    struct StyleList {

        std::vector<Style*> styles;
        bool dirty = true;

        bool isDirty() {
            
            if (dirty) { return true; }

            /*for (Style* style : styles) {
                if (style.dirty) { return dirty = true; }
            }*/

            return false;
        }
        
        // Add style if not present
        void add(Style* style) {
            
            auto it = std::find(styles.begin(), styles.end(), style);
            if (it != styles.end()) { return; }

            styles.push_back(style);
            dirty = true;
        }

        // Remove style if present
        void remove(Style* style) {

            auto it = std::find(styles.begin(), styles.end(), style);
            if (it == styles.end()) { return; }
            
            styles.erase(it);
            dirty = true;
        }

        // Cast as vector
        operator std::vector<Style*>&() {
            return styles;
        }

        // Assign from vector
        StyleList& operator=(std::vector<Style*> other) {

            styles = other;
            dirty = true;

            return *this;
        }

        // Assign from vector reference
        StyleList& operator=(std::vector<Style*>& other) {

            styles = other;
            dirty = true;

            return *this;
        }
    };

    struct StylePtr {

        Style* pStyle = nullptr;

        ~StylePtr() {
            if (pStyle) { delete pStyle; }
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