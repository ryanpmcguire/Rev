#version 430 core

// Antialiased stroke via a segment SDF. Round caps and joins fall out of the
// distance-to-segment math: any fragment within `half` of the segment is solid,
// with a `v_aa`-wide smoothstep fringe.

layout(std140, binding = 1) uniform Data {
    vec4  uColor;
    float uStrokeWidth;
    float uSmoothing;
    float uPointCount;
    float uPad;
};

in vec2 v_pos;
flat in vec2 v_p0;
flat in vec2 v_p1;
flat in float v_half;
flat in float v_aa;

out vec4 FragColor;

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {

    float d = sdSegment(v_pos, v_p0, v_p1) - v_half;
    float alpha = 1.0 - smoothstep(0.0, v_aa, d);

    if (alpha <= 0.0) { discard; }

    FragColor = vec4(uColor.rgb, uColor.a * alpha);
}
