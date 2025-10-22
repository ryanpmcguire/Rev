module;

#include <vector>

export module Rev.Core.Vertex;

import Rev.Core.Pos;
import Rev.Core.Color;

export namespace Rev::Core {

    struct Vertex : public Pos {

        inline static std::vector<float> attribs = {
            2, 4
        };

        Color color;

        Vertex() : Pos() {}
        Vertex(float x, float y) : Pos(x, y) {}
        Vertex(float x, float y, Color c) : Pos(x, y) { color = c; }

        // We preserve color when assigning from a pos
        Vertex& operator=(const Pos& other) {
            x = other.x; y = other.y;
            return *this;
        }
    };
};