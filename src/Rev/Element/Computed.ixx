module;

#include <cstdint>

export module Rev.Computed;

import Rev.Style;

export namespace Rev {

    struct Computed {

        bool dirty = true;

        Style style;
    };
};