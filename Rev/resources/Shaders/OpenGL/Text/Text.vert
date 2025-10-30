#version 430 core

layout(location = 0) in vec4 iPosition;  // xy = position, z = glyph index (float), w = unused

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

// Per-instance color data bound at UBO binding 1
layout(std140, binding = 1) uniform Data {
    vec4 color;
    vec2 pos;
    float depth, pad1;
};

struct CharVertex {
    vec2 pos;
    vec2 uv;
};

layout(std140, binding = 2) uniform GlyphData {
    CharVertex glyphs[128 * 6];
};

out vec2 fragUV;

void main() {

    int glyphIndex = int(iPosition.z);
    int corner = gl_VertexID % 6;

    int index = glyphIndex * 6 + corner;

    CharVertex v = glyphs[index];
    fragUV = v.uv;

    vec2 worldPos = iPosition.xy + v.pos + pos;
    gl_Position = uProjection * vec4(worldPos, 0.0, 1.0);
}