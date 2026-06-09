#version 430 core

layout(location = 0) in float dummyVertexID;

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;
    float opacity;
    float tileCountX;
    float tileCountY;
    float pad;
};

out vec2 fragUV;

void main() {

    // TL, TR, BR, BL  (matches Svg convention — Y increases downward in UI space)
    const vec2 offsets[4] = vec2[](
        vec2(0, 1),
        vec2(1, 1),
        vec2(1, 0),
        vec2(0, 0)
    );

    int vid = gl_VertexID % 4;
    vec2 offset = offsets[vid];
    vec2 pos    = vec2(x, y) + offset * vec2(w, h);

    fragUV      = offset;
    gl_Position = uProjection * vec4(pos, 0.0, 1.0);
}
