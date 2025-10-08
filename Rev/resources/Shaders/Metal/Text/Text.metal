#include <metal_stdlib>
using namespace metal;

// ----------------------------
// GLSL -> Metal struct mapping
// ----------------------------

// GLSL: layout(std140, binding = 0) uniform Transform { mat4 uProjection; };
struct Transform {
    float4x4 uProjection;
};

// GLSL: layout(std140, binding = 1) uniform Data { vec4 color; vec2 pos; };
struct Data {
    float4 color;
    float2 pos;
    float2 _padding; // std140 rules: pad to 16-byte boundary
};

// GLSL: struct CharVertex { vec2 pos; vec2 uv; };
struct CharVertex {
    float2 pos;
    float2 uv;
};

// GLSL: layout(std140, binding = 2) uniform GlyphData { CharVertex glyphs[128 * 6]; };
struct GlyphData {
    CharVertex glyphs[128 * 6];
};

// Vertex input
struct VertexIn {
    float4 iPosition [[attribute(0)]]; // xy = pos, z = glyph index, w = unused
};

// Vertex output
struct VertexOut {
    float4 position [[position]];
    float2 fragUV;
    float4 color;
};

// ----------------------------
// Vertex shader
// ----------------------------
vertex VertexOut vertex_main(
    VertexIn in [[stage_in]],
    constant Transform& transform [[buffer(10)]],
    constant Data& data [[buffer(11)]],
    constant GlyphData& glyphData [[buffer(12)]],
    uint vid [[vertex_id]]
) {
    VertexOut out;

    int glyphIndex = int(in.iPosition.z);
    int corner     = int(vid % 6);
    int index      = glyphIndex * 6 + corner;

    CharVertex v = glyphData.glyphs[index];

    out.fragUV = v.uv;
    out.color = data.color;

    float2 worldPos = in.iPosition.xy + v.pos + data.pos;
    out.position = transform.uProjection * float4(worldPos, 0.0, 1.0);

    return out;
}

// ----------------------------
// Fragment shader
// ----------------------------
fragment float4 fragment_main(VertexOut in [[stage_in]],
                              texture2d<float> tex [[texture(0)]],
                              sampler texSampler [[sampler(0)]],
                              constant Data& data [[buffer(11)]])
{
    float alpha = tex.sample(texSampler, in.fragUV).r;
    return float4(in.color.rgb, alpha);

    //return float4(1, 1, 1, 1);
}
