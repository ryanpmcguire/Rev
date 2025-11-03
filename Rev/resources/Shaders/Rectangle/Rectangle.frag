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
    float shadowX, shadowY, shadowSize, shadowBlur;
    vec4 shadowColor;
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

    // Smoothing (only for corners)
    float smoothingBase = 0.5 * fwidth(distOuter);

    // Corner detection: both x and y near edge
    vec2 edgeDist = abs(localPos) - (halfSize - vec2(cornerRadius));

    // Instead of a hard step, fade in as we approach the corner region
    float fade = cornerRadius; // pixels before corner where we start smoothing
    float cornerFactor =
        smoothstep(-fade, 0.0, edgeDist.x) *
        smoothstep(-fade, 0.0, edgeDist.y);

    // Blend smoothing — only apply at corners
    float smoothing = mix(0.0, smoothingBase, cornerFactor);

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

    vec4 fillColor = vec4(r, g, b, a);
    
    vec4 borderColor =
      l_color * isLeft +
      r_color * isRight +
      t_color * isTop +
      b_color * isBottom;

    // Fill + border
    vec4 shapeColor = mix(fillColor, borderColor, vec4(borderMask));
    float shapeAlpha = max(outerAlpha, borderMask);

    // --- Shadow ---

    vec2 shadowPos = localPos - vec2(shadowX, shadowY);

    // Signed distance field for the shadow's shape (expanded)
    float shadowDist = roundedBoxSDF(
        shadowPos,
        halfSize + vec2(shadowSize),
        cornerRadius + shadowSize
    );

    // Raw blur falloff
    float rawShadowAlpha = 1.0 - smoothstep(0.0, shadowBlur, shadowDist);

    // Scale shadow opacity inversely with blur radius
    // Ensures that large blurs appear softer, not darker
    float blurAttenuation = 1.0 / (1.0 + shadowBlur * 0.05);

    // Optionally, make attenuation weaker for small shadows (more perceptual)
    blurAttenuation = mix(1.0, blurAttenuation, clamp(shadowBlur / 50.0, 0.0, 1.0));

    // Final shadow alpha (never overlaps shape)
    float shadowAlpha = rawShadowAlpha * blurAttenuation * (1.0 - shapeAlpha);
    
    // --- Combine ---

    // Total alpha = union of shape + shadow
    float finalAlpha = clamp(shapeAlpha + shadowAlpha, 0.0, 1.0);

    // Decide which region contributes color
    // Use shadow when it's dominant, shape otherwise
    float shadowWeight = shadowAlpha / max(finalAlpha, 1e-5);
    float shapeWeight  = 1.0 - shadowWeight;

    // Mix the two explicitly (now including alpha)
    vec4 finalRGBA = shadowColor * shadowWeight + shapeColor * shapeWeight;

    // Output: color independent of alpha intensity
    FragColor = vec4(finalRGBA.rgb, finalRGBA.a * finalAlpha);

#endif
}
