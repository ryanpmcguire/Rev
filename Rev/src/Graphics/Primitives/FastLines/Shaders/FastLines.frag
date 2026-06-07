#version 430 core

// Antialiased stroke. Body fragments use a segment SDF (round caps fall out of
// the distance math); convex join-fan fragments use distance to the join point,
// giving a true round join. Both fade over a `v_aa`-wide smoothstep.

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
flat in vec2 v_join;
flat in float v_half;
flat in float v_aa;
flat in float v_round;

out vec4 FragColor;

float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {

    float alpha;

    if (v_round > 0.5) {
        // Corner fan: fills the inner slice (M -> outer butts), all of which is
        // inside the stroke, so it is solid. Its outer edge meets the body quads
        // at the outer butt corners (a clean bevel join).
        alpha = 1.0;
    }
    else {
        float d = sdSegment(v_pos, v_p0, v_p1) - v_half;
        alpha = 1.0 - smoothstep(0.0, v_aa, d);
    }

    if (alpha <= 0.0) { discard; }

    FragColor = vec4(uColor.rgb, uColor.a * alpha);
}
