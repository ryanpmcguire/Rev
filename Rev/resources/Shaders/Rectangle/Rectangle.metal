#include <metal_stdlib>
using namespace metal;

struct Transform {
    float4x4 uProjection;
};

struct Data {
    float x, y, w, h;       // Rect
    float r, g, b, a;       // Color
    float tl, tr, bl, br;   // Corner radii
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 localPos;        // position in local rect space
};

vertex VertexOut vertex_main(
    uint vid [[vertex_id]],
    constant Transform& transform [[buffer(10)]],
    constant Data& data [[buffer(11)]]
) {
    const float2 corners[4] = {
        float2(0.0, 0.0), // top-left
        float2(1.0, 0.0), // top-right
        float2(1.0, 1.0), // bottom-right
        float2(0.0, 1.0)  // bottom-left
    };
    const ushort indices[6] = { 0, 1, 2, 0, 2, 3 };

    float2 cornerOffset = corners[ indices[vid] ];
    float2 pos = float2(data.x, data.y) + cornerOffset * float2(data.w, data.h);

    VertexOut out;
    out.position = transform.uProjection * float4(pos, 0.0, 1.0);

    // Local coords relative to rect center
    float2 rectCenter = float2(data.x + data.w * 0.5, data.y + data.h * 0.5);
    out.localPos = pos - rectCenter;

    out.color = float4(data.r, data.g, data.b, data.a);
    return out;
}

// --- Helper function: signed distance for rounded box ---
inline float roundedBoxSDF(float2 p, float2 halfSize, float radius) {
    float2 q = abs(p) - halfSize + float2(radius);
    return length(max(q, float2(0.0))) - radius;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              constant Data& data [[buffer(11)]])
{
    float2 halfSize = float2(data.w, data.h) * 0.5;

    // Branchless corner selection
    float isLeft   = 1.0 - step(0.0, in.localPos.x);
    float isTop    = 1.0 - step(0.0, in.localPos.y);
    float isRight  = 1.0 - isLeft;
    float isBottom = 1.0 - isTop;

    float w_tl = isLeft  * isTop;
    float w_tr = isRight * isTop;
    float w_bl = isLeft  * isBottom;
    float w_br = isRight * isBottom;

    float cornerRadius = 
          w_tl * data.tl +
          w_tr * data.tr +
          w_bl * data.bl +
          w_br * data.br;

    cornerRadius = clamp(cornerRadius, 0.0, min(halfSize.x, halfSize.y));

    float dist = roundedBoxSDF(in.localPos, halfSize, cornerRadius);

    // Anti-aliasing: fwidth works in Metal, too
    float smoothing = 0.5 * fwidth(dist);
    float alpha = 1.0 - smoothstep(-smoothing, smoothing, dist);

    return float4(data.r, data.g, data.b, data.a * alpha);
}
