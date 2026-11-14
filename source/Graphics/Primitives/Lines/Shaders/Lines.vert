#version 430 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aEdgeDist;

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

layout(std140, binding = 1) uniform Data {
    vec4 uColor;
    float depth, opacity, pad2, pad3;
};

// The group transform (see Group.ixx). Identity outside any group.
// Lines triangulates on the CPU in px, so a scaled group scales its stroke too.
layout(std140, binding = 4) uniform Group { vec4 r0; vec4 r1; };
vec2 place(vec2 p) {
    return vec2(dot(r0.xy, p) + r0.w, dot(r1.xy, p) + r1.w);
}

out vec4 vColor;
out float vEdgeDist;

void main() {

    vColor = (aColor.a != 0.0) ? aColor : uColor;
    vEdgeDist = aEdgeDist;
    
    gl_Position = uProjection * vec4(place(aPos), 0.0, 1.0);
}
