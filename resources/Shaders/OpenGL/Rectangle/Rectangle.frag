#version 430 core

in vec2 fragLocalPos;
out vec4 FragColor;

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;       // Rect
    float r, g, b, a;       // Color
    float tl, tr, bl, br;   // Corner radii
};

float sdfCircle(vec2 p, float radius) {
    return length(p) - radius;
}

void main() {
    vec2 boxCenter = vec2(x + w * 0.5, y + h * 0.5);
    vec2 localPos = fragLocalPos - boxCenter;
    vec2 halfSize = vec2(w, h) * 0.5;

    float radius = (tl + tr + bl + br) * 0.25;

    // Start with fully opaque rect
    float alpha = 1.0;

    // Map fragment to one of four corner regions
    vec2 absPos = abs(localPos); // map all corners to top-right
    vec2 cornerOrigin = halfSize - vec2(radius); // center of the quarter-circle
    vec2 cornerLocal = absPos - cornerOrigin;

    // If fragment is inside corner radius
    if (cornerLocal.x > 0.0 && cornerLocal.y > 0.0) {
        float d = length(cornerLocal);
        float edgeSmoothness = 1.0;
        alpha *= 1.0 - smoothstep(radius - edgeSmoothness, radius, d - 0.5);
    }

    FragColor = vec4(r, g, b, a * alpha);
}