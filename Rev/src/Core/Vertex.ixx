export module Rev.Core.Vertex;

import Rev.Core.Pos;

export namespace Rev::Core {

    struct Vertex : Pos {
        float r{}, g{}, b{}, a{};

        Vertex() = default;
        Vertex(float x, float y) : Pos{x, y} {}
        Vertex(float x, float y, float r, float g, float b, float a)
            : Pos{x, y}, r{r}, g{g}, b{b}, a{a} {}
        Vertex(const Pos& pos) : Pos{pos} {}
    };
}
