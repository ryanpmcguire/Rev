#version 430 core

layout(location = 0) in vec2 aPos;

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
