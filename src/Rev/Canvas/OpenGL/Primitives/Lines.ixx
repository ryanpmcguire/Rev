module;

#include <cmath>
#include <vector>
#include <glew/glew.h>

export module Rev.OpenGL.Lines;

import Rev.OpenGL.Primitive;
import Rev.OpenGL.VertexBuffer;
import Rev.OpenGL.Pipeline;
import Rev.OpenGL.Shader;
import Resources.Shaders.OpenGL.Triangles.Triangles_vert;
import Resources.Shaders.OpenGL.Triangles.Triangles_frag;

int32_t jvPolylineTriangulate(float const polyline[], int32_t polylineCount, float thickness,
    float miterLimit, float triangles[], int32_t triangleCapacity) {

    if (polylineCount <= 1)
        return 0; // Degenerate case: cannot trace a line from 0 or 1 point.
    if (miterLimit <= 0)
        miterLimit = 0;

    // Only compute these constants once.
    float halfThickness = thickness / 2;
    float sqMiterThreshold = (thickness * thickness) * (miterLimit * miterLimit) / 4;

    // Iteration logic in the input and output arrays.
    // Indices represent coordinates (2 per point, 6 per triangle).
    int32_t idxIn = 0;
    int32_t idxInEnd = 2 * polylineCount;
    int32_t idxOut = 0;
    int32_t idxOutEnd = triangleCapacity * 6;

    // First point.
    float xA = polyline[idxIn];
    float yA = polyline[idxIn + 1];
    idxIn += 2;

    float xB, yB, lenAB = 0;
    while (idxIn < idxInEnd && lenAB == 0) {
        // Get second point, such that A and B are distinct.
        xB = polyline[idxIn];
        yB = polyline[idxIn + 1];
        lenAB = hypotf(xB - xA, yB - yA);
        idxIn += 2;
    }

    if (lenAB == 0)
        return 0; // Degenerate case: all points are identical.

    for (; idxIn <= idxInEnd; idxIn += 2) {

        // Get next point C such that B and C are distinct.
        float xC, yC, lenBC;
        if (idxIn < idxInEnd) {
            xC = polyline[idxIn];
            yC = polyline[idxIn + 1];
            lenBC = hypotf(xC - xB, yC - yB);
        }

        else {
            // [AB] is the last segment to render, no join is needed at the end.
            // Making C = A such that (AB) and (BC) are aligned, thus no join will be generated.
            xC = xA;
            yC = yA;
            lenBC = lenAB;
        }
        if (lenBC == 0)
        continue; // Empty segment, do nothing.

    // Compute quad for segment AB.

    float xAB = xB - xA, yAB = yB - yA;
    float xAA1 = halfThickness / lenAB * -yAB;
    float yAA1 = halfThickness / lenAB * xAB;
    float xA1 = xA + xAA1, yA1 = yA + yAA1;
    float xA2 = xA - xAA1, yA2 = yA - yAA1;
    float xA1p = xB + xAA1, yA1p = yB + yAA1;
    float xA2p = xB - xAA1, yA2p = yB - yAA1;

    // Encode quad for segment AB as two triangles.
    if (idxOut < idxOutEnd) {
        triangles[idxOut + 0] = xA1;
        triangles[idxOut + 1] = yA1;
        triangles[idxOut + 2] = xA2;
        triangles[idxOut + 3] = yA2;
        triangles[idxOut + 4] = xA1p;
        triangles[idxOut + 5] = yA1p;
    }
    idxOut += 6;
        if (idxOut < idxOutEnd) {
        triangles[idxOut + 0] = xA2;
        triangles[idxOut + 1] = yA2;
        triangles[idxOut + 2] = xA1p;
        triangles[idxOut + 3] = yA1p;
        triangles[idxOut + 4] = xA2p;
        triangles[idxOut + 5] = yA2p;
    }

    idxOut += 6;

    // Determine the ABC angle's orientation.

    float xBC = xC - xB, yBC = yC - yB;
    float zAB_BC = xAB * yBC - yAB * xBC;

    // If zAB_BC == 0, A B C are aligned and no join is needed.
    if (zAB_BC != 0) {
    // Compute the endpoints of the next segment.

    float xBB1 = halfThickness / lenBC * -yBC;
    float yBB1 = halfThickness / lenBC * xBC;
    float xB1 = xB + xBB1, yB1 = yB + yBB1;
    float xB2 = xB - xBB1, yB2 = yB - yBB1;

    // Generate Bevel join triangle.

    if (idxOut < idxOutEnd) {
        if (zAB_BC < 0) {
            triangles[idxOut + 0] = xB;
            triangles[idxOut + 1] = yB;
            triangles[idxOut + 2] = xA1p;
            triangles[idxOut + 3] = yA1p;
            triangles[idxOut + 4] = xB1;
            triangles[idxOut + 5] = yB1;
        }
        else {
            triangles[idxOut + 0] = xB;
            triangles[idxOut + 1] = yB;
            triangles[idxOut + 2] = xA2p;
            triangles[idxOut + 3] = yA2p;
            triangles[idxOut + 4] = xB2;
            triangles[idxOut + 5] = yB2;
        }
    }
    idxOut += 6;

    // Generate Miter triangle.

    if (miterLimit > 0) {
        float xM, yM;
        if (zAB_BC < 0) {
            // Compute M, the intersection between (A1;AB) and (B1;BC)
            float alpha = (yBC * (xB1 - xA1) + xBC * (yA1 - yB1)) / zAB_BC;
            xM = xA1 + alpha * xAB;
            yM = yA1 + alpha * yAB;
        }
        else {
        // Compute M, the intersection between (A2;AB) and (B2;BC)
        float alpha = (yBC * (xB2 - xA2) + xBC * (yA2 - yB2)) / zAB_BC;
        xM = xA2 + alpha * xAB;
        yM = yA2 + alpha * yAB;
    }

    float sqLenBM = (xM - xB) * (xM - xB) + (yM - yB) * (yM - yB);
    if (sqLenBM <= sqMiterThreshold) {
        if (idxOut < idxOutEnd) {
            if (zAB_BC < 0) {
                triangles[idxOut + 0] = xM;
                triangles[idxOut + 1] = yM;
                triangles[idxOut + 2] = xA1p;
                triangles[idxOut + 3] = yA1p;
                triangles[idxOut + 4] = xB1;
                triangles[idxOut + 5] = yB1;
            }
            else {
                triangles[idxOut + 0] = xM;
                triangles[idxOut + 1] = yM;
                triangles[idxOut + 2] = xA2p;
                triangles[idxOut + 3] = yA2p;
                triangles[idxOut + 4] = xB2;
                triangles[idxOut + 5] = yB2;
            }
        }

        idxOut += 6;
    }
    }
    }
        // Prepare for next segment.
        xA = xB;
        yA = yB;
        xB = xC;
        yB = yC;
        lenAB = lenBC;
    }

    // Returns number of triangle
    return idxOut / 6;
}

export namespace Rev {

    export struct Lines : public Primitive {

        // Shared across instances
        struct Shared {

            size_t refCount = 0;

            Shader* vert = nullptr;
            Shader* frag = nullptr;
            Pipeline* pipeline = nullptr;
        };

        inline static Shared* shared = nullptr;
        VertexBuffer* vertices = nullptr;
        bool dirty = true;

        struct Point { float x, y; };

        size_t num, vertexCount, maxTriangles, maxVertices;
        std::vector<Point> points;

        // Create
        Lines(size_t num) {

            this->num = num;

            // If we're first
            if (!shared) {
                
                shared = new Shared();

                shared->vert = new Shader(Triangles_vert, GL_VERTEX_SHADER);
                shared->frag = new Shader(Triangles_frag, GL_FRAGMENT_SHADER);
                shared->pipeline = new Pipeline(*(shared->vert), *(shared->frag));
            }

            shared->refCount += 1;

            maxTriangles = 4 * num - 2;
            maxVertices = maxTriangles * 3;

            // Num points, 6 * num points for quads
            points.resize(num);
            vertices = new VertexBuffer(2 * 6 * (points.size() - 1));
        }

        // Destroy
        ~Lines() {

            shared->refCount -= 1;

            // If we're last
            if (!shared->refCount) {

                delete shared->pipeline;
                delete shared->vert;
                delete shared->frag;

                delete shared;
            }

            delete vertices;
        }

        void compute() {

            VertexBuffer::Vertex* verts = vertices->verts();
            size_t vtx = 0;
            const float strokeWidth = 10.f;       // total width (not half)
            const float miterLimit = 1.5f;
        
            size_t count = points.size();
            if (count < 2) return;
        
            int numTriangles = jvPolylineTriangulate(
                reinterpret_cast<float*>(points.data()),
                static_cast<int>(count),
                strokeWidth,
                miterLimit,
                reinterpret_cast<float*>(verts),
                maxTriangles
            );
            
            vertexCount = maxVertices;
        }
        

        void draw() override {

            if (dirty) {
                this->compute();
                dirty = false;
            }

            shared->pipeline->bind();
            vertices->bind();
            glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        }
    };
};