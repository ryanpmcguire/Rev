module;

export module Computed;

import Style;

export namespace Rev {

    struct Computed {

        bool dirty = true;

        Style style;
    };
};