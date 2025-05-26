module;

export module Style;

export namespace Rev {

    struct Dist {

        enum Type {
            None, Abs, Rel,
            Grow, Shrink
        };

        Type type;
        float val = -0.0f;
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
        float r, g, b, a;
    };

    struct Size {
        Dist width, height;
    };

    struct Background {
        Color color;
    };

    struct Border {

        struct Corner {
            Color color;
            Dist radius;
            Dist width;
        };

        // Top-level
        Color color;
        Dist radius;
        Dist width;

        // Corners
        Corner tl, tr, bl, br;
    };

    struct Style {

        Size size;
        Background background;
        Border border;

        int transition = 0; // Transition time
    };
}