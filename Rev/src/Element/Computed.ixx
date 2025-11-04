module;

#include <cstdint>

export module Rev.Element.Computed;

import Rev.Element.Style;

export namespace Rev::Element {

    struct Computed {

        bool dirty = true;

        bool hasHoverStyle = false;
        bool hasPressStyle = false;
        bool hasDragStyle = false;
        bool hasFocusStyle = false;

        Style style;
    };
};