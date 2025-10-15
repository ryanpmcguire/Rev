#version 430 core

in float vDist;
out vec4 FragColor;

layout(std140, binding = 1) uniform Data {
    float r, g, b, a;       // Color
    float strokeWidth, miterLimit;
};

void main() {

    // Control parameters
    float lineHalfWidth = 0.1 * strokeWidth;   // defines where full opacity ends
    float edgeSmooth    = 0.5;   // range over which alpha fades

    // Compute signed distance from center (scaled to your geometry)
    float dist = abs(vDist);

    // Fade alpha from 1.0 at center to 0.0 near the edge
    float alpha = smoothstep(lineHalfWidth, lineHalfWidth - edgeSmooth, dist);

    // Final color (semi-transparent white)
    FragColor = vec4(r, g, b, a * alpha);
}
