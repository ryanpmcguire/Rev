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

    // --- Exclusive region selection using minimum distance ---

    // Distances to edges (in pixel space)
    float dL = abs(fragLocalPos.x - x);
    float dR = abs((x + w) - fragLocalPos.x);
    float dT = abs(fragLocalPos.y - y);
    float dB = abs((y + h) - fragLocalPos.y);

    // Distances to corners
    vec2 p_tl = vec2(x, y);
    vec2 p_tr = vec2(x + w, y);
    vec2 p_bl = vec2(x, y + h);
    vec2 p_br = vec2(x + w, y + h);

    float dc_tl = length(fragLocalPos - p_tl);
    float dc_tr = length(fragLocalPos - p_tr);
    float dc_bl = length(fragLocalPos - p_bl);
    float dc_br = length(fragLocalPos - p_br);

    float minCornerD = min(min(dc_tl, dc_tr), min(dc_bl, dc_br));
    float minSideD   = min(min(dL, dR), min(dT, dB));

    // Corners
    float is_tl = float(dc_tl == minCornerD);
    float is_tr = float(dc_tr == minCornerD);
    float is_bl = float(dc_bl == minCornerD);
    float is_br = float(dc_br == minCornerD);

    // Sides
    float is_left   = float(dL == minSideD);
    float is_right  = float(dR == minSideD);
    float is_top    = float(dT == minSideD);
    float is_bottom = float(dB == minSideD);

    vec2 q = sign(localPos);
    float cornerRadius =
        (q.x < 0.0 && q.y < 0.0) ? tl :
        (q.x > 0.0 && q.y < 0.0) ? tr :
        (q.x < 0.0 && q.y > 0.0) ? bl :
                                br;
                                
    cornerRadius = clamp(cornerRadius, 0.0, min(halfSize.x, halfSize.y));

    // Determine local border width based on which side we are on
    float localBorderW = 
          is_left   * b_l +
          is_right  * b_r +
          is_top    * b_t +
          is_bottom * b_b;

    // Compute distances
    float distOuter = roundedBoxSDF(localPos, halfSize, cornerRadius);
    vec2 innerHalfSize = max(halfSize - vec2(localBorderW), vec2(0.0));
    float distInner = roundedBoxSDF(localPos, innerHalfSize, max(cornerRadius - localBorderW, 0.0));

    // Smoothing (only for corners)
    float smoothingBase = 0.5 * fwidth(distOuter);

    // Corner detection: both x and y near edge
    vec2 edgeDist = abs(localPos) - (halfSize - vec2(cornerRadius));

    // Instead of a hard step, fade in as we approach the corner region
    float fade = 0.5 * cornerRadius; // pixels before corner where we start smoothing
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
      l_color * is_left +
      r_color * is_right +
      t_color * is_top +
      b_color * is_bottom;

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
