module;

#include <vector>

export module Rev.Core.Vertex3;

import Rev.Core.Color;

export namespace Rev::Core {

    struct Vertex3 {

        inline static std::vector<size_t> attribs = {
            3, // position
            4, // color
            3, // normal
            1, // a
            1  // b
        };

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Color color = { 1, 1, 1, 1 };

        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 1.0f;

        float a = 0.0f;
        float b = 0.0f;

        Vertex3() = default;

        Vertex3(float x, float y, float z)
            : x(x), y(y), z(z) {}

        Vertex3(float x, float y, float z, Color color)
            : x(x), y(y), z(z), color(color) {}
    };
}