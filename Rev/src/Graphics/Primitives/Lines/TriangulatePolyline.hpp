#pragma once
#include <cmath>
#include <cstdint>

struct Vec2 {
    float x, y;
};

struct Quad {
    Vec2 a, b, c, d; // a,b = inner; c,d = outer
};

struct Tri {
    Vec2 a, b, c;
};

int32_t triangulatePolyline(float* polylineFloats, int32_t polylineCount,
                            float* triangleFloats, int32_t triangleCount, float thickness) {

    if (polylineCount < 2) return 0;

    Vec2* polyline  = reinterpret_cast<Vec2*>(polylineFloats);
    Vec2* triangles = reinterpret_cast<Vec2*>(triangleFloats);

    const float halfT = thickness * 0.5f;
    int32_t triOut = 0;

    // --- Build initial segment quad ---
    Vec2 A = polyline[0];
    Vec2 B = polyline[1];

    Vec2 dir{ B.x - A.x, B.y - A.y };
    float len = std::hypot(dir.x, dir.y);
    if (len == 0) return 0;

    dir.x /= len;
    dir.y /= len;
    Vec2 perp{ -dir.y, dir.x };

    Quad thisQuad = {
        { A.x + perp.x * halfT, A.y + perp.y * halfT }, // A+
        { A.x - perp.x * halfT, A.y - perp.y * halfT }, // A-
        { B.x - perp.x * halfT, B.y - perp.y * halfT }, // B-
        { B.x + perp.x * halfT, B.y + perp.y * halfT }  // B+
    };

    // --- Iterate through remaining segments ---
    for (int32_t i = 1; i < polylineCount - 1; ++i) {

        Vec2 C = polyline[i + 1];

        Vec2 dirNext{ C.x - B.x, C.y - B.y };
        float lenNext = std::hypot(dirNext.x, dirNext.y);
        bool hasNext = (lenNext > 0);
        if (!hasNext) continue;

        dirNext.x /= lenNext;
        dirNext.y /= lenNext;
        Vec2 perpNext{ -dirNext.y, dirNext.x };

        // Build next quad for BC
        Quad nextQuad = {
            { B.x + perpNext.x * halfT, B.y + perpNext.y * halfT },
            { B.x - perpNext.x * halfT, B.y - perpNext.y * halfT },
            { C.x - perpNext.x * halfT, C.y - perpNext.y * halfT },
            { C.x + perpNext.x * halfT, C.y + perpNext.y * halfT }
        };

        // Compute join geometry
        float cross = dir.x * dirNext.y - dir.y * dirNext.x;

        Vec2 inner1, inner2, outer1, outer2;
        
        if (cross < 0) {
            inner1 = thisQuad.b;
            inner2 = nextQuad.b;
            outer1 = thisQuad.d;
            outer2 = nextQuad.a;
        } else {
            inner1 = thisQuad.a;
            inner2 = nextQuad.a;
            outer1 = thisQuad.c;
            outer2 = nextQuad.b;
        }

        float denom = dir.x * dirNext.y - dir.y * dirNext.x;
        Vec2 innerJoin = inner2;
        if (std::fabs(denom) > 1e-3f) {
            float t = ((inner2.x - inner1.x) * dirNext.y - (inner2.y - inner1.y) * dirNext.x) / denom;
            innerJoin = { inner1.x + dir.x * t, inner1.y + dir.y * t };
        }

        // Construct and emit join triangle
        Tri joinCap{ outer1, outer2, innerJoin };
        triangles[triOut + 0] = joinCap.a;
        triangles[triOut + 1] = joinCap.b;
        triangles[triOut + 2] = joinCap.c;
        triOut += 3;

        // Apply shared vertex correction
        if (cross < 0) {
            // Right turn → inside = right
            thisQuad.c = innerJoin;  // current segment’s end inner
            nextQuad.b = innerJoin;  // next segment’s start inner
        } else {
            // Left turn → inside = left
            thisQuad.d = innerJoin;  // current segment’s end inner
            nextQuad.a = innerJoin;  // next segment’s start inner
        }

        // ✅ Emit corrected current quad (after join adjustment)
        triangles[triOut + 0] = thisQuad.a;
        triangles[triOut + 1] = thisQuad.b;
        triangles[triOut + 2] = thisQuad.c;
        triangles[triOut + 3] = thisQuad.a;
        triangles[triOut + 4] = thisQuad.c;
        triangles[triOut + 5] = thisQuad.d;
        triOut += 6;

        // Move forward
        thisQuad = nextQuad;
        A = B;
        B = C;
        dir = dirNext;
    }

    // Emit final segment (no next)
    triangles[triOut + 0] = thisQuad.a;
    triangles[triOut + 1] = thisQuad.b;
    triangles[triOut + 2] = thisQuad.c;
    triangles[triOut + 3] = thisQuad.a;
    triangles[triOut + 4] = thisQuad.c;
    triangles[triOut + 5] = thisQuad.d;
    triOut += 6;

    return triOut / 3;
}
