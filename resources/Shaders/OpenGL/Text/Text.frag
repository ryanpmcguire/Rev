#version 430 core

in vec2 fragUV;
out vec4 FragColor;

// Texture sampler bound at texture unit 0
layout(binding = 0) uniform sampler2D tex;

// Per-instance color data bound at UBO binding 1
layout(std140, binding = 1) uniform Data {
    vec4 color;
    vec2 pos;
};

void main() {
    float alpha = texture(tex, fragUV).r;
    FragColor = vec4(color.rgb, alpha);
}