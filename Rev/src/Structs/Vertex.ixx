module;

#include <cmath>

export module Rev.Vertex;

import Rev.Pos;

export namespace Rev {

    struct Vertex : public Pos {

        float r, g, b, a;

        Vertex() {}

        Vertex(float x, float y) {
            this->x = x;
            this->y = y;
        }

        Vertex(Pos& pos) {
            x = pos.x; y = pos.y;
        }
    };
};