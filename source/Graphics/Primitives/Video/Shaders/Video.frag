#version 430 core

in vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D videoTexture;
layout(std140, binding = 1) uniform Data {
    float x, y, w, h;
    float u, v, uw, vh;
    float opacity, exposure, contrast, saturation;
    vec4 gamma;
    vec4 inputBlack;
    vec4 inputWhite;
    vec4 outputBlack;
    vec4 outputWhite;
    uint pixelFormat;
    float padding0, padding1, padding2;
};

void main() {
    outColor = texture(videoTexture, fragUV);
    if (pixelFormat == 5u || pixelFormat == 7u) {
        outColor.rgb = outColor.bgr;
    }
    if (pixelFormat == 6u || pixelFormat == 7u) {
        outColor.a = 1.0;
    }

    vec3 color = outColor.rgb;
    color = clamp((color - inputBlack.rgb) / max(inputWhite.rgb - inputBlack.rgb, vec3(0.00001)), 0.0, 1.0);
    color = pow(color, 1.0 / max(gamma.rgb, vec3(0.00001)));
    color = (color - vec3(0.5)) * contrast + vec3(0.5);
    color *= exp2(exposure);
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, saturation);
    outColor.rgb = clamp(outputBlack.rgb + color * (outputWhite.rgb - outputBlack.rgb), 0.0, 1.0);
    outColor.a *= opacity;
}
