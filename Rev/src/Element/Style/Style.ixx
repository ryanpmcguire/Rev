module;

#include <bit>
#include <cstdint>
#include <cmath>
#include <vector>

export module Rev.Element.Style;

import Rev.Core.DirtyFlag;
import Rev.Core.Resource;
import Rev.Core.Color;

export namespace Rev::Element {

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

            // Search for or modify redundant transitions
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

    constexpr int operator"" _sec(long double value) { return static_cast<int>(value * 1000.0); }
    constexpr int operator"" _sec(unsigned long long value) { return static_cast<int>(value * 1000.0); }
    constexpr int operator"" _ms(unsigned long long value) { return static_cast<int>(value); }

    // Distance
    //--------------------------------------------------

    // Size: 6
    struct Dist {

        enum Type {
            Unset, Abs, Rel,
            Grow, Shrink,
            Inherit
        };

        Type type = Type::Unset;
        float val = -0.0f;

        int transition = -1;
        Core::DirtyFlag* dirty = nullptr;

        static inline Dist Null() {
            return { Type::Unset, -0.0f, -1, nullptr };
        }

        inline void linkDirtyFlag(Core::DirtyFlag* dirty) {
            this->dirty = dirty;
        }

        // Directly resolve by comparing to value
        inline float resolve(float compare) {

            if  (type == Type::Abs) { return val; }
            else if (type == Type::Rel) { return compare * val; }

            else return val;
        }

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
            return type != Unset;
        }

        bool operator==(const Dist& other) {
            return (
                type == other.type &&
                val == other.val
            );
        }

        // Custom assignment operator
        // Copy everything except the "dirty" flag pointer
        Dist& operator=(const Dist& other) {

            if (this == &other) { return *this; }
            if (*this == other) { return *this; }
            if (dirty) { *dirty = true; }

            type = other.type;
            val = other.val;
            transition = other.transition;

            return *this;
        }
    };

    Dist Px(float value) { return { Dist::Type::Abs, value }; }
    Dist Pct(float value) { return { Dist::Type::Rel, value / 100.0f }; }
    Dist Grow() { return { Dist::Type::Grow, -0.0f }; }
    Dist Shrink() { return { Dist::Type::Shrink, -0.0f }; }

    constexpr Dist operator"" _px(unsigned long long value) { return { Dist::Type::Abs, static_cast<float>(value) }; }
    constexpr Dist operator"" _pct(unsigned long long value) { return { Dist::Type::Rel, static_cast<float>(value) / 100.0f }; }
    constexpr Dist operator"" _grow(unsigned long long value) { return { Dist::Type::Grow, static_cast<float>(value) / 100.0f }; }
    constexpr Dist operator"" _shrink(unsigned long long value) { return { Dist::Type::Grow, static_cast<float>(value) / 100.0f }; }

    // Color
    //--------------------------------------------------

    struct Color {

        enum Type {
            Unset,
            Rgb,  fRgb,
            Rgba, fRgba,
            Hex,
            Tint,
            Inherit
        };

        Type type = Unset;

        float r = -0.0f, g = -0.0f, b = -0.0f, a = -0.0f;
        
        int transition = -1;
        Core::DirtyFlag* dirty = nullptr;

        static inline Color Null() {
            return { Type::Unset, -0.0f, -0.0f, -0.0f, -0.0f, -1, nullptr };
        }

        [[nodiscard]] inline operator Core::Color() const noexcept {
            return { r, g, b, a };
        }

        inline void linkDirtyFlag(Core::DirtyFlag* dirty) {
            this->dirty = dirty;
        }

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

        bool operator==(const Color& other) {
            return (
                type == other.type &&
                r == other.r && g == other.g && b == other.b && a == other.a
            );
        }

        // Custom assignment operator
        // Copy everything except the "dirty" flag pointer
        Color& operator=(const Color& other) {

            // Check if assignment *should* occur, set dirty if so
            if (this == &other) { return *this; }
            if (*this == other) { return *this; }
            if (dirty) { *dirty = true; }

            type = other.type;
            r = other.r; g = other.g; b = other.b; a = other.a;
            transition = other.transition;

            return *this;
        }

        explicit operator bool() {
            return (type != Unset);
        }
    };

    Color rgba(float r, float g, float b, float a) {
        return { Color::Type::Rgba, r / 255.0f, g / 255.0f, b / 255.0f, a };
    }

    Color rgb(float r, float g, float b) {
        return { Color::Type::Rgba, r / 255.0f, g / 255.0f, b / 255.0f, 1.0 };
    }

    Color Tint(Color color) {
        color.type = Color::Type::Tint;
        return color;
    }
    
    // Size
    //--------------------------------------------------

    struct Size {

        struct MinMax {
            Dist width, height;
        };

        Dist width, height;        
        MinMax min, max;
        
        int transition = -1;
        Core::DirtyFlag* dirty = nullptr;

        static inline Size Null() {

            return {
                Dist::Null(), Dist::Null(),
                { Dist::Null(), Dist::Null() },
                { Dist::Null(), Dist::Null() },
                -1, nullptr
            };
        }

        inline void linkDirtyFlag(Core::DirtyFlag* dirty) {

            width.linkDirtyFlag(dirty); height.linkDirtyFlag(dirty);
            min.width.linkDirtyFlag(dirty); min.height.linkDirtyFlag(dirty);
            max.width.linkDirtyFlag(dirty); max.height.linkDirtyFlag(dirty);

            this->dirty = dirty;
        }

        // Apply other size to this one
        inline void apply(Size& size) {

            width.apply(size.width); height.apply(size.height);
            min.width.apply(size.min.width); max.width.apply(size.max.width);
            min.height.apply(size.min.height); max.height.apply(size.max.height);

            if (size.transition > 0) { transition = size.transition; }
        }

        inline void animate(Size& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {

            int transitionLength = transition > 1 ? transition : ms;

            // Animate nominal width/height
            width.animate(old.width, transitions, time, transitionLength);
            height.animate(old.height, transitions, time, transitionLength);

            // Animate min/max width
            min.width.animate(old.min.width, transitions, time, transitionLength);
            max.width.animate(old.max.width, transitions, time, transitionLength);

            // Animate min/max height
            min.height.animate(old.min.height, transitions, time, transitionLength);
            max.height.animate(old.max.height, transitions, time, transitionLength);
        }
    };

    // Left-right-top-bottom style
    //--------------------------------------------------

    struct LrtbStyle {

        struct MinMax {
            Dist left, right, top, bottom;
        };

        Dist left, right, top, bottom;
        MinMax min, max;

        int transition = -1;
        Core::DirtyFlag* dirty = nullptr;

        static inline LrtbStyle Null() {

            return {
                Dist::Null(), Dist::Null(), Dist::Null(), Dist::Null(),
                { Dist::Null(), Dist::Null(), Dist::Null(), Dist::Null() },
                { Dist::Null(), Dist::Null(), Dist::Null(), Dist::Null() },
                -1, nullptr
            };
        }

        // Assigning from dist (all are set to dist)
        LrtbStyle& operator=(const Dist& other) {

            left = other; right = other;
            top = other; bottom = other;

            return *this;
        }

        inline void linkDirtyFlag(Core::DirtyFlag* dirty) {
            
            left.linkDirtyFlag(dirty); right.linkDirtyFlag(dirty);
            top.linkDirtyFlag(dirty); bottom.linkDirtyFlag(dirty);

            min.left.linkDirtyFlag(dirty); min.right.linkDirtyFlag(dirty);
            min.top.linkDirtyFlag(dirty); min.bottom.linkDirtyFlag(dirty);

            max.left.linkDirtyFlag(dirty); max.right.linkDirtyFlag(dirty);
            max.top.linkDirtyFlag(dirty); max.bottom.linkDirtyFlag(dirty);

            this->dirty = dirty;
        }

        // Apply other LRTB style (margin/padding) to this one
        inline void apply(LrtbStyle& lrtb) {

            // Apply nominal
            left.apply(lrtb.left); right.apply(lrtb.right);
            top.apply(lrtb.top); bottom.apply(lrtb.bottom);

            // Apply minima
            min.left.apply(lrtb.min.left); min.right.apply(lrtb.min.right);
            min.top.apply(lrtb.min.top); min.bottom.apply(lrtb.min.bottom);

            // Apply maxima
            max.left.apply(lrtb.max.left); max.right.apply(lrtb.max.right);
            max.top.apply(lrtb.max.top); max.bottom.apply(lrtb.max.bottom);

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
            min.left.animate(old.min.left, transitions, time, transitionLength);
            min.right.animate(old.min.right, transitions, time, transitionLength);
            min.top.animate(old.min.top, transitions, time, transitionLength);
            min.bottom.animate(old.min.bottom, transitions, time, transitionLength);
        
            // Animating the maximum values
            max.left.animate(old.max.left, transitions, time, transitionLength);
            max.right.animate(old.max.right, transitions, time, transitionLength);
            max.top.animate(old.max.top, transitions, time, transitionLength);
            max.bottom.animate(old.max.bottom, transitions, time, transitionLength);
        }
    };

    // Alignment
    //--------------------------------------------------

    enum class Position {
        Unset,
        Absolute,
        Relative
    };

    enum class Axis {
        Unset,
        Horizontal,
        Vertical
    };

    enum class Align {
        Unset,
        Start, End, Center,
        SpaceAorund, SpaceBetween
    };

    enum class Wrap {
        Unset,
        True,
        False
    };

    struct LayoutStyle {

        Axis direction = Axis::Unset;
        Align horizontal = Align::Unset;
        Align vertical = Align::Unset;
        Wrap wrap = Wrap::Unset;
        Position position = Position::Unset;

        Core::DirtyFlag* dirty = nullptr;

        static inline LayoutStyle Null() {

            return {
                Axis::Unset,
                Align::Unset, Align::Unset,
                Wrap::Unset,
                Position::Unset,
                nullptr
            };
        }

        inline void linkDirtyFlag(Core::DirtyFlag* dirty) {
            this->dirty = dirty;
        }

        inline void apply(LayoutStyle& other) {
            if (other.direction != Axis::Unset) { direction = other.direction; }
            if (other.horizontal != Align::Unset) { horizontal = other.horizontal; }
            if (other.vertical != Align::Unset) { vertical = other.vertical; }
            if (other.wrap != Wrap::Unset) { wrap = other.wrap; }
            if (other.position != Position::Unset) { position = other.position; }
        }
    };

    // Background
    //--------------------------------------------------

    struct Background {

        Color color;
        int transition = -1;
        Core::DirtyFlag* dirty = nullptr;

        static inline Background Null() {

            return {
                Color::Null(),
                -1, nullptr
            };
        }

        inline void linkDirtyFlag(Core::DirtyFlag* dirty) {
            color.linkDirtyFlag(dirty);
            this->dirty = dirty;
        }

        inline void apply(Background& background) {
            if (background.color) { color = background.color; }
            if (background.transition > 0) { transition = background.transition; }
        }

        inline void animate(Background& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {
            
            int transitionLength = transition > 0 ? transition : ms;

            color.animate(old.color, transitions, time, transitionLength);
        }
    };

    // Border
    //--------------------------------------------------

    struct Border {

        struct Corner {

            Dist radius;

            int transition = -1;
            Core::DirtyFlag* dirty = nullptr;

            static inline Corner Null() {
                return {
                    Dist::Null(),
                    -1, nullptr
                };
            }

            inline void linkDirtyFlag(Core::DirtyFlag* dirty) {
                radius.linkDirtyFlag(dirty);
                this->dirty = dirty;
            }

            // Apply new style
            inline void apply(Corner& corner) {
                if (corner.radius) { radius = corner.radius; }
            }

            inline void animate(Corner& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {

                int transitionLength = transition > 0 ? transition : ms;

                radius.animate(old.radius, transitions, time, transitionLength);
            }
        };

        struct Side {

            Color color;
            Dist width;

            int transition = -1;
            Core::DirtyFlag* dirty = nullptr;

            static inline Side Null() {
                return {
                    Color::Null(), Dist::Null(),
                    -1, nullptr
                };
            }
            
            inline void linkDirtyFlag(Core::DirtyFlag* dirty) {

                color.linkDirtyFlag(dirty);
                width.linkDirtyFlag(dirty);

                this->dirty = dirty;
            }

            // Apply new style
            inline void apply(Side& corner) {
                if (corner.color) { color = corner.color; }
                if (corner.width) { width = corner.width; }
                if (corner.transition > 0) { transition = corner.transition; }
            }

            inline void animate(Side& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {

                int transitionLength = transition > 0 ? transition : ms;

                color.animate(old.color, transitions, time, transitionLength);
                width.animate(old.width, transitions, time, transitionLength);
            }
        };

        // Top-level
        Color color;
        Dist radius;
        Dist width;

        // Corners
        Corner tl, tr, bl, br;
        Side left, right, top, bottom;

        int transition = -1;
        Core::DirtyFlag* dirty = nullptr;

        static inline Border Null() {

            return {
                Color::Null(), Dist::Null(), Dist::Null(),
                Corner::Null(), Corner::Null(), Corner::Null(), Corner::Null(),
                Side::Null(), Side::Null(), Side::Null(), Side::Null(),
                -1, nullptr
            };
        }

        inline void linkDirtyFlag(Core::DirtyFlag* dirty) {

            color.linkDirtyFlag(dirty);
            radius.linkDirtyFlag(dirty);
            width.linkDirtyFlag(dirty);
            tl.linkDirtyFlag(dirty); tr.linkDirtyFlag(dirty);
            bl.linkDirtyFlag(dirty); br.linkDirtyFlag(dirty);

            left.linkDirtyFlag(dirty); right.linkDirtyFlag(dirty);
            top.linkDirtyFlag(dirty); bottom.linkDirtyFlag(dirty);

            this->dirty = dirty;
        }

        // Apply new style
        inline void apply(Border& border) {

            // Apply to self
            if (border.color) { color = border.color; }
            if (border.radius) { radius = border.radius; }
            if (border.width) { width = border.width; }
            if (border.transition > 0) { transition = border.transition; }

            // Apply to corners
            tl.apply(border.tl); tr.apply(border.tr);
            bl.apply(border.bl); br.apply(border.br);

            // Apply to sides
            left.apply(border.left); right.apply(border.right);
            top.apply(border.top); bottom.apply(border.bottom);
        }

        inline void animate(Border& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {
            
            int transitionLength = transition > 0 ? transition : ms;

            color.animate(old.color, transitions, time, transitionLength);
            radius.animate(old.radius, transitions, time, transitionLength);
            width.animate(old.width, transitions, time, transitionLength);

            tl.animate(old.tl, transitions, time, transitionLength);
            tr.animate(old.tr, transitions, time, transitionLength);
            bl.animate(old.bl, transitions, time, transitionLength);
            br.animate(old.br, transitions, time, transitionLength);

            left.animate(old.left, transitions, time, transitionLength);
            right.animate(old.right, transitions, time, transitionLength);
            top.animate(old.top, transitions, time, transitionLength);
            bottom.animate(old.bottom, transitions, time, transitionLength);
        }
    };

    // Shadow
    //--------------------------------------------------

    struct Shadow {

        Color color;

        Dist size;
        Dist blur;
        Dist x, y;

        int transition = -1;
        Core::DirtyFlag* dirty = nullptr;

        static inline Shadow Null() {

            return {
                Color::Null(),
                Dist::Null(), Dist::Null(),
                Dist::Null(), Dist::Null(),
                -1, nullptr
            };
        };

        inline void linkDirtyFlag(Core::DirtyFlag* dirty) {

            color.linkDirtyFlag(dirty);
            size.linkDirtyFlag(dirty);
            blur.linkDirtyFlag(dirty);
            x.linkDirtyFlag(dirty);
            y.linkDirtyFlag(dirty);
            
            this->dirty = dirty;
        }

        // Apply new style
        inline void apply(Shadow& shadow) {

            // Apply to self
            if (shadow.color) { color = shadow.color; }
            if (shadow.size) { size = shadow.size; }
            if (shadow.blur) { blur = shadow.blur; }
            if (shadow.x) { x = shadow.x; }
            if (shadow.y) { y = shadow.y; }

            if (shadow.transition > 0) { transition = shadow.transition; }
        }

        inline void animate(Shadow& old, std::vector<Transition>& transitions, uint64_t& time, int& ms) {
            
            int transitionLength = transition > 0 ? transition : ms;

            color.animate(old.color, transitions, time, transitionLength);
            size.animate(old.size, transitions, time, transitionLength);
            blur.animate(old.blur, transitions, time, transitionLength);
            x.animate(old.x, transitions, time, transitionLength);
            y.animate(old.y, transitions, time, transitionLength);
        }
    };

    // Font
    //--------------------------------------------------

    struct TextStyle {

        Core::Resource font;
        Color color;
        Dist size;
        int weight = -1;
        Dist lineHeight;
        Dist spacing;

        int transition = -1;
        Core::DirtyFlag* dirty = nullptr;

        static inline TextStyle Null() {

            return {
                Core::Resource(),
                Color::Null(), Dist::Null(),
                -1,
                Dist::Null(), Dist::Null(),
                -1, nullptr
            };
        }

        inline void linkDirtyFlag(Core::DirtyFlag* dirty) {

            color.linkDirtyFlag(dirty);
            size.linkDirtyFlag(dirty);
            lineHeight.linkDirtyFlag(dirty);
            spacing.linkDirtyFlag(dirty);

            this->dirty = dirty;
        }

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

    enum class Cursor : int {
        Unset,
        Default,
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

    // Display
    //--------------------------------------------------

    enum class Visibility {
        Unset,
        Visible, Hidden,
        Inherit
    };

    struct VisibilityStyle {
        
        Visibility mode;
        Core::DirtyFlag* dirty = nullptr;

        static inline VisibilityStyle Null() { return { Visibility::Unset, nullptr }; }
        inline void linkDirtyFlag(Core::DirtyFlag* dirty) { this->dirty = dirty; }
        inline void apply(VisibilityStyle& other) { if (other.mode != Visibility::Unset) { *this = other; } }
        explicit operator bool() { return mode != Visibility::Unset; }

        bool operator==(const VisibilityStyle& other) { return (mode == other.mode); }
        bool operator==(const Visibility& otherMode) { return (mode == otherMode); }

        // Assign from Visibility enum
        VisibilityStyle& operator=(const Visibility& newMode) {

            if (mode == newMode) { return *this; }

            mode = newMode;
            *dirty = true;

            return *this;
        }

        // Copy everything except the "dirty" flag pointer
        VisibilityStyle& operator=(const VisibilityStyle& other) {

            if (this == &other) { return *this; }
            if (*this == other) { return *this; }
            if (dirty) { *dirty = true; }

            mode = other.mode;

            return *this;
        }
    };

    // Overflow
    //--------------------------------------------------

    enum class Overflow {
        Unset,
        Show, Hide,
        Scroll, ScrollH, ScrollV,
        Inherit,
    };

    // Applies
    //--------------------------------------------------

    struct Applies {
        bool hover = false;
        bool press = false;
        bool drag = false;
        bool focus = false;
        bool disabled = false;
    };

    // Inheritance struct
    //--------------------------------------------------

    struct InheritCast {

        // Cast to Color
        [[nodiscard]] inline operator Color() const noexcept {
            return Color{ Color::Type::Inherit, 0.0f, 0.0f, 0.0f, 0.0f, -1 };
        }
    
        // Cast to Dist
        [[nodiscard]] inline operator Dist() const noexcept {
            return Dist{ Dist::Type::Inherit, -0.0f };
        }
    
        // Cast to Visibility
        [[nodiscard]] inline operator Visibility() const noexcept {
            return Visibility::Inherit;
        }
    
        // Cast to Overflow
        [[nodiscard]] inline operator Overflow() const noexcept {
            return Overflow::Inherit;
        }
    };

    constexpr InheritCast Inherit{};

    // Style per-se
    //--------------------------------------------------

    struct Style {

        Applies applies;

        VisibilityStyle visibility;
        Overflow overflow = Overflow::Unset;

        LayoutStyle layout;
        LrtbStyle position;

        Size size;
        LrtbStyle margin;
        LrtbStyle padding;

        Background background;
        Border border;
        Shadow shadow;

        TextStyle text;
        Cursor cursor;

        int zIndex = 0;

        int transition = -1; // Transition 
        Core::DirtyFlag dirty;

        static inline Style* CreateNull() {
            
            Style* pStyle = new Style();
            *pStyle = Style::Null();

            return pStyle;
        }

        static inline Style Null() {

            Style nullStyle = {
                .applies = { false, false, false, false, false },
                .visibility = VisibilityStyle::Null(),
                .overflow = Overflow::Unset,
                .position = LrtbStyle::Null(),
                .layout = LayoutStyle::Null(),
                .size = Size::Null(),
                .margin = LrtbStyle::Null(),
                .padding = LrtbStyle::Null(),
                .background = Background::Null(),
                .border = Border::Null(),
                .shadow = Shadow::Null(),
                .text = TextStyle::Null(),
                .cursor = Cursor::Unset,
                .zIndex = 0,
                .transition = -1,
                .dirty = true
            };

            nullStyle.linkDirtyFlag(&nullStyle.dirty);

            return nullStyle;
        }

        // Inherit from parent style if applicable
        void inherit(Style& style) {

            // Parent with visibility set to hidden will force children to also be hidden
            if (visibility == Visibility::Inherit || style.visibility == Visibility::Hidden) { visibility = style.visibility; }
        }

        // Apply single style
        void apply(Style& style, Applies flags = {}) {

            // If there are application rules for the style (needs hover, drag, etc)
            if (style.applies.hover || style.applies.press || style.applies.drag || style.applies.focus || style.applies.disabled) {

                // If there are no matching flags, return
                if (!(
                    (style.applies.hover && flags.hover) ||
                    (style.applies.press && flags.press) ||
                    (style.applies.drag && flags.drag) ||
                    (style.applies.focus && flags.focus) ||
                    (style.applies.disabled && flags.disabled)
                )) { return; }
            }

            visibility.apply(style.visibility);
            layout.apply(style.layout);
            position.apply(style.position);

            size.apply(style.size);
            margin.apply(style.margin);
            padding.apply(style.padding);
            
            background.apply(style.background);
            border.apply(style.border);
            shadow.apply(style.shadow);

            text.apply(style.text);

            // Apply transition, overflow, cursor
            if (style.zIndex != 0) { zIndex = style.zIndex; }
            if (style.transition) { transition = style.transition; }
            if (style.overflow != Overflow::Unset) { overflow = style.overflow; }
            if (style.cursor != Cursor::Unset) { cursor = style.cursor; }
        }

        // Apply vector of styles
        void apply(std::vector<Style*>& styles , Applies flags = {}) {

            for (Style* style : styles) {
                apply(*style, flags);
            }
        }

        void animate(Style& old, std::vector<Transition>& transitions, uint64_t& time) {

            position.animate(old.position, transitions, time, transition);

            size.animate(old.size, transitions, time, transition);
            margin.animate(old.margin, transitions, time, transition);
            padding.animate(old.padding, transitions, time, transition);

            background.animate(old.background, transitions, time, transition);
            border.animate(old.border, transitions, time, transition);
            shadow.animate(old.shadow, transitions, time, transition);

            text.animate(old.text, transitions, time, transition);
        }

        inline bool isDirty() {
            return this->dirty;
        }

        void linkDirtyFlag(Core::DirtyFlag* dirty) {

            visibility.linkDirtyFlag(dirty);
            layout.linkDirtyFlag(dirty);
            position.linkDirtyFlag(dirty);

            size.linkDirtyFlag(dirty);
            margin.linkDirtyFlag(dirty);
            padding.linkDirtyFlag(dirty);

            background.linkDirtyFlag(dirty);
            border.linkDirtyFlag(dirty);
            shadow.linkDirtyFlag(dirty);

            text.linkDirtyFlag(dirty);
        }

        // Custom assignment operator
        // Copy everything except the "dirty" flag pointer
        Style& operator=(const Style& other) {

            if (this == &other) { return *this; }

            applies = other.applies;
            visibility = other.visibility;
            overflow = other.overflow;
            position = other.position;
            layout = other.layout;
            size = other.size;
            margin = other.margin;
            padding = other.padding;
            background = other.background;
            border = other.border;
            shadow = other.shadow;
            text = other.text;
            cursor = other.cursor;

            zIndex = other.zIndex;

            this->linkDirtyFlag(&dirty);

            return *this;
        }
    };

    struct StyleList {

        std::vector<Style*> styles;
        Core::DirtyFlag dirty;

        bool hasHoverStyle = false;
        bool hasPressStyle = false;
        bool hasDragStyle = false;
        bool hasFocusStyle = false;

        StyleList() {}

        StyleList(std::initializer_list<Style*> init) : styles(init) {

        }

        StyleList(std::vector<Style*> styles) {
            this->styles = styles;
        }

        void prepend(Style* style) {

            auto it = std::find(styles.begin(), styles.end(), style);
            if (it != styles.end()) { return; }

            styles.insert(styles.begin(), style);
            dirty = true;
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

        // Assign from StyleList (copy all except dirty flag)
        StyleList& operator=(StyleList& other) {

            styles = other.styles;
            dirty = true;

            return *this;
        }

        // Assign from vector
        StyleList& operator=(std::vector<Style*> other) {

            styles = other;
            dirty = true;

            return *this;
        }
    };

    struct StylePtr {

        Style* pStyle = nullptr;
        Core::DirtyFlag dirty;

        ~StylePtr() {
            if (pStyle) { delete pStyle; }
            pStyle = nullptr;
        }

        Style* get() {

            // Create style if not yet initialized
            if (!pStyle) {
                pStyle = Style::CreateNull();
                dirty.drawsFrom(&(pStyle->dirty));
            }

            return pStyle;
        }

        explicit operator bool() const {
            return pStyle != nullptr;
        }

        // Ensure exists before deref
        Style& operator*() {
            if (!pStyle) { pStyle = get(); }
            return *pStyle;
        }

        // Ensure exists before access
        Style* operator->() {
            if (!pStyle) { pStyle = get(); }
            return pStyle;
        }

        // Assigning from Style
        StylePtr& operator=(const Style& other) {
            if (!pStyle) { pStyle = get(); }
            *pStyle = other;
            return *this;
        }

        // Assigning from Style*
        StylePtr& operator=(Style* pOther) {
            if (!pStyle) { pStyle = get(); }
            *pStyle = *pOther;
            return *this;
        }

        // Assigning from StylePtr
        StylePtr& operator=(StylePtr& other) {
            if (!pStyle) { pStyle = get(); }
            *pStyle = *(other.pStyle);
            return *this;
        }

        // Casting to a style pointer
        operator Style*() {
            if (!pStyle) { pStyle = get(); }
            return pStyle;
        }

        // Casting to a style reference
        operator Style&() {
            if (!pStyle) { pStyle = get(); }
            return *pStyle;
        }

        // Applying from a style
        void apply(Style& other, Applies flags = {}) {
            if (!pStyle) { pStyle = get(); }
            pStyle->apply(other, flags);
        }

        // Applying from a style pointer
        void apply(StylePtr& other, Applies flags = {}) {
            if (!pStyle) { pStyle = get(); }
            if (other.pStyle) { pStyle->apply(*(other.pStyle), flags); }
        }
    };
}