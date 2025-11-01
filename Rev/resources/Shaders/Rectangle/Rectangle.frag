#version 430 core

DEFINITIONS

in vec2 fragLocalPos;
out vec4 FragColor;

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;                           // Rect
    float r, g, b, a;                           // Fill color
    float tl, tr, bl, br;                       // Corner radii
    float b_l, b_r, b_t, b_b;                   // Border widths
    vec4 l_color, r_color, t_color, b_color;    // Border colors
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
    float isLeft   = 1.0 - step(0.0, localPos.x);
    float isTop    = 1.0 - step(0.0, localPos.y);
    float isRight  = 1.0 - isLeft;
    float isBottom = 1.0 - isTop;

    // Corner weights
    float w_tl = isLeft  * isTop;
    float w_tr = isRight * isTop;
    float w_bl = isLeft  * isBottom;
    float w_br = isRight * isBottom;

    float cornerRadius =
          w_tl * tl +
          w_tr * tr +
          w_bl * bl +
          w_br * br;

    cornerRadius = clamp(cornerRadius, 0.0, min(halfSize.x, halfSize.y));

    // Determine local border width based on which side we are on
    float localBorderW = 
          isLeft   * b_l +
          isRight  * b_r +
          isTop    * b_t +
          isBottom * b_b;

    // Compute distances
    float distOuter = roundedBoxSDF(localPos, halfSize, cornerRadius);
    vec2 innerHalfSize = max(halfSize - vec2(localBorderW * 0.5), vec2(0.0));
    float distInner = roundedBoxSDF(localPos, innerHalfSize, max(cornerRadius - localBorderW * 0.5, 0.0));

    // Smoothing
    float smoothing = 0.5 * fwidth(distOuter);

    // Alpha for outer shape (to clip)
    float outerAlpha = 1.0 - smoothstep(-smoothing, smoothing, distOuter);

    // Border mask: inside outer shape but outside inner
    float innerMask = 1.0 - smoothstep(-smoothing, smoothing, distInner);
    float borderMask = clamp(outerAlpha - innerMask, 0.0, 1.0);

#ifdef STENCIL
    // Stencil mode: only keep interior pixels
    if (distInner > -smoothing) {
        discard;
    }
    FragColor = vec4(0.0, 0.0, 0.0, 0.0);
#else
    // --- Normal rendering path ---

    // Base fill color
    vec4 fillColor = vec4(r, g, b, a);

    // Compute directional blend for border colors
    // Blend left-right horizontally, top-bottom vertically
    float sideX = smoothstep(-halfSize.x, halfSize.x, localPos.x);
    float sideY = smoothstep(-halfSize.y, halfSize.y, localPos.y);

    vec4 horizColor = mix(l_color, r_color, sideX);
    vec4 vertColor  = mix(t_color, b_color, sideY);
    vec4 borderColor = mix(horizColor, vertColor, 0.5); // combined blend

    // Mix fill and border
    vec4 color = mix(fillColor, borderColor, vec4(borderMask));

    float alpha = max(outerAlpha, borderMask);
    FragColor = vec4(color.rgb, color.a * alpha);
#endif
}
