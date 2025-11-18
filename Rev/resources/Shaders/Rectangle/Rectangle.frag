#version 430 core

DEFINITIONS

in vec2 fragLocalPos;
in vec4 cornerMask;
in vec4 sideMask;

out vec4 FragColor;

layout(std140, binding = 1) uniform Data {
    float x, y, w, h;                                   // Rect
    float r, g, b, a;                                   // Fill color
    float tl, tr, bl, br;                               // Corner radii
    float l_width, r_width, t_width, b_width;           // Border widths
    vec4 l_color, r_color, t_color, b_color;            // Border colors
    float shadowX, shadowY, shadowSize, shadowBlur;
    vec4 shadowColor;
};

vec4 softMax(vec4 v, float sharpness) {

    // Stabilize by subtracting the maximum value
    float M = max(max(v.x, v.y), max(v.z, v.w));

    // Apply exponent with sharpness
    vec4 e = exp((v - vec4(M)) * sharpness);

    // Sum of all exponentiated values
    float s = e.x + e.y + e.z + e.w;

    // Normalize
    return e / max(s, 1e-6);
}

float roundedBoxSDF(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    return length(max(q, 0.0)) - radius;
}

void main() {

    // Compute basic dimensions
    vec2 rectCenter = vec2(x + w * 0.5, y + h * 0.5);
    vec2 localPos = fragLocalPos - rectCenter;

    // Choose side and corner
    //--------------------------------------------------

    
    vec4 mCorner = softMax(cornerMask, 1.0f);
    vec4 mSide   = step(max(sideMask.x, max(sideMask.y, max(sideMask.z, sideMask.w))) - 0.0001, sideMask);

    // Choose corner radius, border width, and color
    float cornerRadius = mCorner.x * tl + mCorner.y * tr + mCorner.z * bl + mCorner.w * br;
    float borderWidth = mSide.x * l_width + mSide.y * r_width + mSide.z * t_width + mSide.w * b_width;
    vec4 borderColor = mSide.x * l_color + mSide.y * r_color + mSide.z * t_color + mSide.w * b_color;

    // Calculate outer and inner half-size (accounting for border width)
    vec2 outerHalfSize = vec2(w, h) * 0.5;
    vec2 innerHalfSize = max(outerHalfSize - vec2(borderWidth), vec2(0.0));

    // Choose corner radius, calc inner and outer based on size and border width
    float outerRadius = clamp(cornerRadius, 0.0, min(outerHalfSize.x, outerHalfSize.y));
    float innerRadius = max(outerRadius - borderWidth, 0.0);

    // Compute box SDF with borders
    //--------------------------------------------------

    // Signed distance to inner (inside border) and outer (on border) boxes
    float distInner = roundedBoxSDF(localPos, innerHalfSize, innerRadius);
    float distOuter = roundedBoxSDF(localPos, outerHalfSize, outerRadius);

    // Hard-edge masks
    float outerMask  = float(distOuter <= 0.0);  // inside outer rounded box
    float innerMask  = float(distInner <= 0.0);  // inside inner (fill) box

    // Border = outer region minus inner region
    float borderMask = outerMask * (1.0 - innerMask);
    float fillMask   = innerMask;

    // If stencil mode is activated, we immediately discard if we're inside the inner mask
    #ifdef STENCIL

        if (innerMask == 0.0) { discard; }
        FragColor = vec4(0,0,0,0);

        return;
    #else

    // Chose color and compare SDF
    //--------------------------------------------------

    vec4 fillColor = vec4(r, g, b, a);
    vec4 color = fillColor * fillMask + borderColor * borderMask;

    // Fill + border
    vec4 shapeColor = mix(fillColor, borderColor, vec4(borderMask));
    float shapeAlpha = clamp(fillMask + borderMask, 0.0, 1.0);

    // --- Shadow ---

    vec2 shadowPos = localPos - vec2(shadowX, shadowY);

    // Signed distance field for the shadow's shape (expanded)
    float shadowDist = roundedBoxSDF(
        shadowPos,
        outerHalfSize + vec2(shadowSize),
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
