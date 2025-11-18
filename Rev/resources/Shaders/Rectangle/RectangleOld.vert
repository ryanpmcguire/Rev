#version 430 core

layout(location = 0) in float dummyVertexID;

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;                           // Rect
    float r, g, b, a;                           // Fill color
    float tl, tr, bl, br;                       // Corner radii
    float b_l, b_r, b_t, b_b;                   // Border widths
    vec4 l_color, r_color, t_color, b_color;    // Border colors
    float shadowX, shadowY, shadowSize, shadowBlur;
    vec4 shadowColor;
};

out vec2 fragLocalPos;
out vec4 fragColor;

void main() {
    const vec2 offsets[4] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );

    vec2 cornerOffset = offsets[gl_VertexID];

    // --- Original rect (for fragLocalPos) ---
    vec2 rectPos = vec2(x, y) + cornerOffset * vec2(w, h);
    fragLocalPos = rectPos;

    // --- Expanded rect (for actual rasterization) ---
    float shadowExtent = shadowSize + shadowBlur;
    vec2 expandedOrigin = vec2(x, y) - vec2(shadowExtent);
    vec2 expandedSize   = vec2(w, h) + vec2(shadowExtent * 2.0);
    vec2 expandedPos    = expandedOrigin + cornerOffset * expandedSize;

    // Apply shadow offset globally — moves shadow outward, not the shape
    expandedPos += vec2(shadowX, shadowY);
    fragLocalPos = expandedPos;

    // Send color to fragment shader
    fragColor = vec4(r, g, b, a);

    // Final projection: expanded position, not original
    gl_Position = uProjection * vec4(expandedPos, 0.0, 1.0);
}
