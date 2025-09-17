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

    // Branchless corner selector
    float isLeft   = 1.0 - step(0.0, localPos.x); // 1 if x < 0
    float isTop    = 1.0 - step(0.0, localPos.y); // 1 if y < 0
    float isRight  = 1.0 - isLeft;
    float isBottom = 1.0 - isTop;

    // Compute weights
    float w_tl = isLeft  * isTop;
    float w_tr = isRight * isTop;
    float w_bl = isLeft  * isBottom;
    float w_br = isRight * isBottom;

    float cornerRadius = 
          w_tl * tl +
          w_tr * tr +
          w_bl * bl +
          w_br * br;

    // Clamp radius to avoid overflow
    cornerRadius = clamp(cornerRadius, 0.0, min(halfSize.x, halfSize.y));

    // Compute signed distance
    float dist = roundedBoxSDF(localPos, halfSize, cornerRadius);

    // Adaptive smoothing
    float smoothing = 0.5 * fwidth(dist);

    // Compute alpha
    float alpha = 1.0 - smoothstep(-smoothing, smoothing, dist);

    FragColor = vec4(r, g, b, a * alpha);
}