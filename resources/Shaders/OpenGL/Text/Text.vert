#version 430 core

layout(location = 0) in vec4 aPacked; // xy = position, zw = UV

out vec2 fragUV;

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

void main() {
    vec2 aPos = aPacked.xy;
    fragUV = aPacked.zw;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
