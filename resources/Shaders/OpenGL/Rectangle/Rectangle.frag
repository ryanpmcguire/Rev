#version 430 core

in vec2 fragLocalPos;
out vec4 FragColor;

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;       // Rect
    float r, g, b, a;       // Color
    float tl, tr, bl, br;   // Corner radii
};

float roundedBoxSDF(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    return length(max(q, 0.0)) - radius;
}

void main() {
    vec2 rectCenter = vec2(x + w * 0.5, y + h * 0.5);
    vec2 localPos = fragLocalPos - rectCenter;
    vec2 halfSize = vec2(w, h) * 0.5;

    // Determine which corner we're in
    float cornerRadius = 0.0;
    if (localPos.x < 0.0 && localPos.y > 0.0) {
        cornerRadius = bl;
    } else if (localPos.x > 0.0 && localPos.y > 0.0) {
        cornerRadius = br;
    } else if (localPos.x < 0.0 && localPos.y < 0.0) {
        cornerRadius = tl;
    } else {
        cornerRadius = tr;
    }

    // Clamp radius to avoid going outside half extents
    cornerRadius = clamp(cornerRadius, 0.0, min(halfSize.x, halfSize.y));

    // Compute signed distance
    float dist = roundedBoxSDF(localPos, halfSize, cornerRadius);

    // Adaptive smoothing
    float smoothing = 0.5 * fwidth(dist);

    // Compute alpha from distance
    float alpha = 1.0 - smoothstep(-smoothing, smoothing, dist);

    FragColor = vec4(r, g, b, a * alpha);
}
