#include <metal_stdlib>
using namespace metal;

// --- Uniform Buffers ---

// Projection matrix (matches std140 binding = 0)
struct Transform
{
    float4x4 uProjection;
};

// Stroke color + parameters (matches std140 binding = 1)
struct Data
{
    float4 color;        // r,g,b,a
    float strokeWidth;
    float miterLimit;
};

// --- Vertex Input / Output ---

struct VertexIn
{
    float2 aPos   [[attribute(0)]];
    float  aDist  [[attribute(1)]];
};

struct VertexOut
{
    float4 position [[position]];
    float  vDist;
};

// --- Vertex Shader ---

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Transform& transform [[buffer(0)]])
{
    VertexOut out;
    out.position = transform.uProjection * float4(in.aPos, 0.0, 1.0);
    out.vDist = in.aDist;
    return out;
}

// --- Fragment Shader ---

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              constant Data& data [[buffer(1)]])
{
    // Output color directly (same as GLSL version)
    return data.color;
}
