export module Rev.Core.Vertex;

import Rev.Core.Pos;
import Rev.Core.Color;

export namespace Rev::Core {

    struct Vertex : public Pos {

        Color color;

        Vertex() : Pos() {}
        Vertex(float x, float y) : Pos(x, y) {}
        Vertex(float x, float y, Color c) : Pos(x, y) { color = c; }
    };
};