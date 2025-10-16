#version 430 core

out vec4 FragColor;

layout(std140, binding = 1) uniform Data {
    float r, g, b, a;       // Color
};

void main() {

    // Final color (semi-transparent white)
    FragColor = vec4(r, g, b, a);
}