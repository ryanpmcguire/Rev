#version 430 core

layout(location = 0) in vec2 aPos;

// Uniform block (binding = 0 must match what C++ uses)
layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

out vec2 fragLocalPos;

void main() {
    fragLocalPos = aPos;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}