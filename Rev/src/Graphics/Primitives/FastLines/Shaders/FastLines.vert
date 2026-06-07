#version 430 core

// GPU polyline triangulation.
//
// One instance per segment: glDrawArraysInstanced(GL_TRIANGLES, 0, 12, N - 1).
// Each instance reads its four neighbouring points from a TBO and builds:
//   verts 0-5  : the segment body quad. Full width is preserved by using an
//                *unclamped* miter on the inner (concave) side -- which also
//                tiles exactly with the neighbour, so no overdraw -- and a butt
//                corner on the outer (convex) side.
//   verts 6-11 : a 2-triangle fan over the convex corner, flagged so the
//                fragment rounds it around the join point (a real round join,
//                matching the round caps). Collapses to a point on open ends.
//
// Because the miter is never clamped, joins never get thinner or flat-beveled.

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

layout(binding = 0) uniform samplerBuffer uPoints;

out vec2 v_pos;
flat out vec2 v_p0;
flat out vec2 v_p1;
flat out vec2 v_join;    // round-join centre
flat out float v_half;
flat out float v_aa;
flat out float v_round;  // 1 => round around v_join, 0 => segment SDF

vec2 fetch(int i) { return texelFetch(uPoints, i).xy; }

// Miter offset for the bisector of two unit directions, scaled so the offset
// edge sits `ext` from the centreline (perpendicular component == ext, hence no
// thinning). `nrm` is this segment's normal. Falls back to a plain butt offset
// for a near-hairpin where the bisector is undefined.
vec2 miterOffset(vec2 a, vec2 b, vec2 nrm, float ext) {

    vec2 sum = a + b;
    if (dot(sum, sum) < 1e-4) { return nrm * ext; }

    vec2 t = normalize(sum);
    vec2 m = vec2(-t.y, t.x);

    float d = dot(m, nrm);
    if (abs(d) < 1e-3) { return nrm * ext; }

    return m * (ext / d);
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

    vec2 segDir = p1 - p0;
    float segLen = length(segDir);
    segDir = (segLen > 1e-6) ? segDir / segLen : vec2(1.0, 0.0);
    vec2 nrm = vec2(-segDir.y, segDir.x);

    float aa  = uSmoothing + 1.0;
    float hw  = 0.5 * uStrokeWidth;
    float ext = hw + aa;

    vec2 dirIn  = hasPrev ? normalize(p0 - prev) : segDir;
    vec2 dirOut = hasNext ? normalize(next - p1) : segDir;

    // --- body quad corners (c?p on +nrm side, c?m on -nrm side) ---

    vec2 c0p, c0m;
    if (!hasPrev) {
        // Open start: butt + extend back so the round cap disc is covered.
        c0p = p0 + nrm * ext - segDir * ext;
        c0m = p0 - nrm * ext - segDir * ext;
    } else {
        float cross0 = dirIn.x * segDir.y - dirIn.y * segDir.x;
        vec2 o = miterOffset(dirIn, segDir, nrm, ext);
        // Left turn (cross0>0): inner (concave) side is +nrm -> mitered & shared;
        // outer side is -nrm -> butt + corner fan. Right turn mirrors.
        if (cross0 > 0.0) { c0p = p0 + o;         c0m = p0 - nrm * ext; }
        else              { c0p = p0 + nrm * ext; c0m = p0 - o; }
    }

    vec2 c1p, c1m;
    if (!hasNext) {
        c1p = p1 + nrm * ext + segDir * ext;
        c1m = p1 - nrm * ext + segDir * ext;
    } else {
        float cross1 = segDir.x * dirOut.y - segDir.y * dirOut.x;
        vec2 o = miterOffset(segDir, dirOut, nrm, ext);
        if (cross1 > 0.0) { c1p = p1 + o;         c1m = p1 - nrm * ext; }
        else              { c1p = p1 + nrm * ext; c1m = p1 - o; }
    }

    // --- convex round-join fan at p0 (owned by this segment) ---

    // The corner fill pivots at the inner intersection M (the same shared vertex
    // the body quads miter to), not at p0 -- otherwise it leaves the inner slice
    // between the two quads' start edges empty. From M it fans out to the two
    // segments' outer butt corners, filling the slice exactly.
    vec2 pivot = p0;
    vec2 j1 = p0, jm = p0, j2 = p0;
    if (hasPrev) {
        float cross0 = dirIn.x * segDir.y - dirIn.y * segDir.x;
        vec2 o = miterOffset(dirIn, segDir, nrm, ext);
        pivot = (cross0 > 0.0) ? (p0 + o) : (p0 - o);   // inner intersection M

        float os = (cross0 >= 0.0) ? -1.0 : 1.0;         // outer side
        vec2 nrmPrev = vec2(-dirIn.y, dirIn.x);
        vec2 thisOuter = p0 + nrm * (ext * os);
        vec2 prevOuter = p0 + nrmPrev * (ext * os);
        vec2 bis = (thisOuter - p0) + (prevOuter - p0);
        vec2 mid = (dot(bis, bis) > 1e-8) ? (p0 + normalize(bis) * ext) : thisOuter;
        j1 = thisOuter; jm = mid; j2 = prevOuter;
    }

    int v = gl_VertexID;
    vec2 pos = p0;
    float round = 0.0;

    if      (v == 0) { pos = c0p; }
    else if (v == 1) { pos = c0m; }
    else if (v == 2) { pos = c1m; }
    else if (v == 3) { pos = c0p; }
    else if (v == 4) { pos = c1m; }
    else if (v == 5) { pos = c1p; }
    else if (v == 6) { pos = pivot; round = 1.0; }
    else if (v == 7) { pos = j1;    round = 1.0; }
    else if (v == 8) { pos = jm;    round = 1.0; }
    else if (v == 9) { pos = pivot; round = 1.0; }
    else if (v == 10){ pos = jm;    round = 1.0; }
    else             { pos = j2;    round = 1.0; }

    v_pos   = pos;
    v_p0    = p0;
    v_p1    = p1;
    v_join  = p0;
    v_half  = hw;
    v_aa    = aa;
    v_round = round;

    gl_Position = uProjection * vec4(pos, 0.0, 1.0);
}
