#version 430 core

in float vDist;
out vec4 FragColor;

layout(std140, binding = 1) uniform Data {
    float r, g, b, a;       // Color
    float strokeWidth, miterLimit;
};

void main() {

    // Final color (semi-transparent white)
    FragColor = vec4(r, g, b, a);
}
