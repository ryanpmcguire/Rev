#version 430 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

out float vDist;

void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}