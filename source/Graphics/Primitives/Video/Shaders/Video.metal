#include <metal_stdlib>

DEFINITIONS

using namespace metal;

struct Transform { float4x4 uProjection; };
struct Data {
    float x, y, w, h;
    float u, v, uw, vh;
    float opacity, exposure, contrast, saturation;
    float4 gamma;
    float4 inputBlack;
    float4 inputWhite;
    float4 outputBlack;
    float4 outputWhite;
    uint pixelFormat;
    float padding0, padding1, padding2;
};
struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut vertex_main(
    uint vid [[vertex_id]],
    constant Transform& transform [[buffer(10)]],
    constant Data& data [[buffer(11)]]) {

    const float2 corners[4] = {
        float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 1)
    };
    const ushort indices[6] = { 0, 1, 2, 0, 2, 3 };
    float2 corner = corners[indices[vid]];

    VertexOut out;
    out.position = transform.uProjection * float4(float2(data.x, data.y) + corner * float2(data.w, data.h), 0, 1);
    out.uv = float2(data.u, data.v) + corner * float2(data.uw, data.vh);
    return out;
}

fragment float4 fragment_main(
    VertexOut in [[stage_in]],
    constant Data& data [[buffer(11)]],
    texture2d<float> videoTexture [[texture(0)]],
    sampler videoSampler [[sampler(0)]]) {

    float4 color = videoTexture.sample(videoSampler, in.uv);
    if (data.pixelFormat == 5u || data.pixelFormat == 7u) {
        color.rgb = color.bgr;
    }
    if (data.pixelFormat == 6u || data.pixelFormat == 7u) {
        color.a = 1.0;
    }
    color.rgb = clamp((color.rgb - data.inputBlack.rgb) / max(data.inputWhite.rgb - data.inputBlack.rgb, float3(0.00001)), 0.0, 1.0);
    color.rgb = pow(color.rgb, 1.0 / max(data.gamma.rgb, float3(0.00001)));
    color.rgb = (color.rgb - float3(0.5)) * data.contrast + float3(0.5);
    color.rgb *= exp2(data.exposure);
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    color.rgb = mix(float3(luminance), color.rgb, data.saturation);
    color.rgb = clamp(data.outputBlack.rgb + color.rgb * (data.outputWhite.rgb - data.outputBlack.rgb), 0.0, 1.0);
    color.a *= data.opacity;
    return color;
}
