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
    float4 aColor [[attribute(1)]];
};

struct VertexOut
{
    float4 position [[position]];
    float4 vColor;
};

// --- Vertex Shader ---

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Transform& transform [[buffer(10)]],
                             constant Data& data [[buffer(11)]])
{
    VertexOut out;

    // Apply projection
    out.position = transform.uProjection * float4(in.aPos, 0.0, 1.0);

    // Proper override: if vertex color is "empty" (alpha == 0), use uniform color
    out.vColor = (in.aColor.a != 0.0) ? in.aColor : data.color;

    return out;
}

// --- Fragment Shader ---

fragment float4 fragment_main(VertexOut in [[stage_in]])
{
    // Just output the color decided in the vertex shader
    return in.vColor;
}
