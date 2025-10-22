module;

#include <cmath>
#include <vector>
#include <algorithm>

export module Rev.Core.View;

import Rev.Core.Pos;
import Rev.Core.Vertex;
import Rev.Core.Lrtb;

export namespace Rev::Core {

    struct View : public Lrtb {
        
        using Lrtb::Lrtb;

        void shift(Pos pos) {
            l += pos.x; r += pos.x;
            t += pos.y; b += pos.y;
        }

        void zoom(Pos focus, Pos factor) {

            // Shrink distance to left and right equally
            l = focus.x - factor.x * (focus.x - l);
            r = focus.x + factor.x * (r - focus.x);

            // Shrink distance to top and bottom equally
            t = focus.y - factor.y * (focus.y - t);
            b = focus.y + factor.y * (b - focus.y);
        }

        View shifted(Pos pos) {
            return {
                l + pos.x, r + pos.x,
                t + pos.y, b + pos.y
            };
        }
    };
};