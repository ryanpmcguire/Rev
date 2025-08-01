#version 430 core

layout(location = 0) in vec4 iPosition;  // xy = position, z = glyph index (float), w = unused

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
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
    int glyphIndex = int(round(iPosition.z));  // ← use rounding here
    int corner = gl_VertexID % 6;

    int baseIndex = glyphIndex * 6 + corner;
    CharVertex v = glyphs[baseIndex];

    fragUV = v.uv;

    vec2 worldPos = iPosition.xy + v.pos;
    gl_Position = uProjection * vec4(worldPos, 0.0, 1.0);
}
