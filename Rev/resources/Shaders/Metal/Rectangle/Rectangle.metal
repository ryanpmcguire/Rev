#include <metal_stdlib>
using namespace metal;

struct Transform {
    float4x4 uProjection;
};

struct Data {
    float x, y, w, h;
    float r, g, b, a;
    float tl, tr, bl, br;
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut vertex_main(
    uint vid [[vertex_id]],
    constant Transform& transform [[buffer(0)]],
    constant Data& data [[buffer(1)]]
) {
    // The four logical corners
    const float2 corners[4] = {
        float2(0.0, 0.0), // top-left
        float2(1.0, 0.0), // top-right
        float2(1.0, 1.0), // bottom-right
        float2(0.0, 1.0)  // bottom-left
    };

    // Indices for two triangles (6 verts total)
    const ushort indices[6] = { 0, 1, 2, 0, 2, 3 };

    float2 cornerOffset = corners[ indices[vid] ];
    float2 position = float2(data.x, data.y) + cornerOffset * float2(data.w, data.h);

    VertexOut out;
    out.position = transform.uProjection * float4(position, 0.0, 1.0);
    out.color = float4(data.r, data.g, data.b, data.a);

    out.position = float4(0,0,0,1);
out.color    = float4(0,1,0,1); // bright green

    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]]) {
    return in.color;
}
