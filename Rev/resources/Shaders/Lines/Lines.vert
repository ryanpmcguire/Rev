#version 430 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

layout(std140, binding = 1) uniform Data {
    vec4 uColor;
    float depth, pad1, pad2, pad3;
};

out vec4 vColor;

void main() {

    vColor = (aColor.a != 0.0) ? aColor : uColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
