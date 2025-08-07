#version 430 core

layout(location = 0) in float dummyVertexID; // just to enable draw call

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;       // Rect
    float r, g, b, a;       // Color
    float tl, tr, bl, br;   // Corner radii
};

out vec2 fragLocalPos;
out vec4 fragColor;

void main() {

    const vec2 offsets[4] = vec2[](
        vec2(0.0, 0.0), // Top-left
        vec2(1.0, 0.0), // Top-right
        vec2(1.0, 1.0), // Bottom-right
        vec2(0.0, 1.0)  // Bottom-left
    );

    vec2 cornerOffset = offsets[gl_VertexID];
    vec2 position = vec2(x, y) + cornerOffset * vec2(w, h);

    fragLocalPos = position; // local within rect
    fragColor = vec4(r, g, b, a);

    gl_Position = uProjection * vec4(position, 0.0, 1.0);
}
