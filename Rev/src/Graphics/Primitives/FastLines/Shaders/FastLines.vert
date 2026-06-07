#version 430 core

// GPU polyline triangulation.
//
// One instance per segment: glDrawArraysInstanced(GL_TRIANGLES, 0, 12, N - 1).
// Each instance reads its four neighbouring points from a TBO via texelFetch and
// builds a miter-joined quad (verts 0-5). Verts 6-11 are reserved for bevel
// fills at clamped miters; for now they collapse to a zero-area triangle (a free
// discard), so sharp corners fall back to a clamped miter.

layout(std140, binding = 0) uniform Transform {
    mat4 uProjection;
};

layout(std140, binding = 1) uniform Data {
    vec4  uColor;
    float uStrokeWidth;
    float uSmoothing;
    float uPointCount;
    float uPad;
};

// Packed point positions: GL_RG32F, one texel (xy) per point.
layout(binding = 0) uniform samplerBuffer uPoints;

out vec2 v_pos;          // this fragment's position
flat out vec2 v_p0;      // segment endpoints (for the fragment SDF)
flat out vec2 v_p1;
flat out float v_half;   // stroke half-width
flat out float v_aa;     // AA fringe width

vec2 fetch(int i) {
    return texelFetch(uPoints, i).xy;
}

void main() {

    int seg = gl_InstanceID;
    int N   = int(uPointCount + 0.5);

    int i0 = seg;
    int i1 = seg + 1;

    vec2 p0 = fetch(i0);
    vec2 p1 = fetch(i1);

    bool hasPrev = i0 > 0;
    bool hasNext = i1 < (N - 1);

    vec2 prev = hasPrev ? fetch(i0 - 1) : p0;
    vec2 next = hasNext ? fetch(i1 + 1) : p1;

    // Segment frame
    vec2 segDir = p1 - p0;
    float segLen = length(segDir);
    segDir = (segLen > 1e-6) ? segDir / segLen : vec2(1.0, 0.0);
    vec2 nrm = vec2(-segDir.y, segDir.x);

    float aa  = uSmoothing + 1.0;
    float hw  = 0.5 * uStrokeWidth;
    float ext = hw + aa;                 // expand for the AA fringe / cap disc

    // Miter offset at p0 (bisector of the incoming and this segment).
    vec2 dirIn = hasPrev ? normalize(p0 - prev) : segDir;
    vec2 tanS  = normalize(dirIn + segDir);
    vec2 miterS = vec2(-tanS.y, tanS.x);
    float denomS = dot(miterS, nrm);
    float offLenS = (abs(denomS) > 0.25) ? (ext / denomS) : ext;   // clamp spikes
    vec2 offS = miterS * offLenS;

    // Miter offset at p1 (bisector of this segment and the outgoing).
    vec2 dirOut = hasNext ? normalize(next - p1) : segDir;
    vec2 tanE  = normalize(segDir + dirOut);
    vec2 miterE = vec2(-tanE.y, tanE.x);
    float denomE = dot(miterE, nrm);
    float offLenE = (abs(denomE) > 0.25) ? (ext / denomE) : ext;
    vec2 offE = miterE * offLenE;

    // Extend the open ends of the polyline so the round cap disc is covered.
    vec2 capS = hasPrev ? vec2(0.0) : (-segDir * ext);
    vec2 capE = hasNext ? vec2(0.0) : ( segDir * ext);

    vec2 A0 = p0 + offS + capS;
    vec2 B0 = p0 - offS + capS;
    vec2 A1 = p1 + offE + capE;
    vec2 B1 = p1 - offE + capE;

    int v = gl_VertexID;
    vec2 pos;

    if      (v == 0) { pos = A0; }
    else if (v == 1) { pos = B0; }
    else if (v == 2) { pos = B1; }
    else if (v == 3) { pos = A0; }
    else if (v == 4) { pos = B1; }
    else if (v == 5) { pos = A1; }
    else             { pos = p0; }   // reserved bevel verts: degenerate

    v_pos  = pos;
    v_p0   = p0;
    v_p1   = p1;
    v_half = hw;
    v_aa   = aa;

    gl_Position = uProjection * vec4(pos, 0.0, 1.0);
}
