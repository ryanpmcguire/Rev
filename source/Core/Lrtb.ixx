module;

#include <cmath>
#include <algorithm>

export module Rev.Core.Lrtb;

import Rev.Core.Pos;
import Rev.Core.Vertex;

export namespace Rev::Core {

    struct Lrtb {

        float l = 0; float r = 0;
        float t = 0; float b = 0;

        Lrtb() {}

        Lrtb(float l, float r, float t, float b) {
            this->l = l; this->r = r;
            this->t = t; this->b = b;
        }

        // Return dimensions of lrtb
        Pos span() {
            return {
                r - l,
                b - t
            };
        }
    };
};