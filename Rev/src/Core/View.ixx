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

        void shift(Pos& pos) {
            l += pos.x; r += pos.x;
            t += pos.y; b += pos.y;
        }

        View shifted(Pos& pos) {
            return {
                l + pos.x, r + pos.x,
                t + pos.y, b + pos.y
            };
        }
    };
};