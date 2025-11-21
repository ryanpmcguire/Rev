#version 430 core

in vec2 fragUV;
out vec4 outColor;

layout(binding = 0) uniform sampler2D svgTex;

void main() {
    outColor = texture(svgTex, fragUV);
}
