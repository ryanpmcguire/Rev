module;

#include <cstdint>

export module Rev.Element.Computed;

import Rev.Element.Style;

export namespace Rev::Element {

    struct Computed {

        bool dirty = true;

        Style style;
    };
};