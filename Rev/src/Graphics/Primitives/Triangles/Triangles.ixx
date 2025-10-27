module;

#include <cmath>
#include <vector>

export module Rev.Primitive.Triangles;

import Rev.Primitive;
import Rev.Core.Shared;
import Rev.Core.Color;
import Rev.Core.Vertex;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

// Shader file resources
import Resources.Shaders.OpenGL.Triangles.Triangles_vert;
import Resources.Shaders.OpenGL.Triangles.Triangles_frag;
import Resources.Shaders.Metal.Triangles.Triangles_metal;

export namespace Rev::Primitive {

    struct Triangles : public Primitive {

        enum Topology {
            List, Fan, Strip
        };

        // Instance-specific data
        struct Data {
            Color color = { 1, 1, 1, 1 };
        };

        inline static Shared shared;
        inline static Pipeline* pipeline;

        UniformBuffer* databuff = nullptr;
        VertexBuffer* vertices = nullptr;

        Topology topology;
        Data* data = nullptr;
        bool dirty = true;

        Color color = { 1, 1, 1, 1 };

        // We may own our points, or we may be given a pointer to some other points
        Vertex center; std::vector<Vertex> points, left, right;
        Vertex* pCenter; std::vector<Vertex>* pPoints, *pLeft, *pRight;

        size_t numFaces, numVerts;

        struct Params {

            Topology topology;

            Vertex* center;
            std::vector<Vertex>* points;

            std::vector<Vertex>* left;
            std::vector<Vertex>* right;
        };
        
        // Create
        Triangles(Canvas* canvas, Params params = {}) : Primitive(canvas) {

            // Set params
            //--------------------------------------------------

            topology = params.topology;

            if (params.center) { pCenter = params.center; }
            else { pCenter = &center; }

            if (params.points) { pPoints = params.points; }
            else { pPoints = &points; }

            if (params.left) { pLeft = params.left; }
            else { pLeft = &left; }

            if (params.right) { pRight = params.right; }
            else { pRight = &right; }

            // Initialize resources
            //--------------------------------------------------
            
            // Create shared pipeline
            shared.create([canvas]() {

                pipeline = new Pipeline(canvas->context, {

                    .instanced = false,
                    .attribs = { 2, 4 },

                    .openGlVert = Triangles_vert,
                    .openGlFrag = Triangles_frag,
                    .metalUniversal = Triangles_metal
                });
            });

            vertices = new VertexBuffer(canvas->context, { .attribs = { 2, 4 } });
            databuff = new UniformBuffer(canvas->context, sizeof(Data));
            data = (Data*)databuff->data;
        }

        // Destroy
        ~Triangles() {

            // Destroy shared pipeline
            shared.destroy([]() {
                delete pipeline;
            });

            delete vertices;
            delete databuff;
        }

        void doFan() {

            Vertex& rCenter = *pCenter;
            std::vector<Vertex>& rPoints = *pPoints;

            // Calculate number of faces and vertices
            numFaces = rPoints.size() - 1;
            numVerts = numFaces * 3;

            // Resize vertex buffer to match needed vertices
            vertices->resize(numVerts);
            Vertex* verts = vertices->verts();
        
            // Compute faces for triangle fan from center and perimeter (points)
            for (size_t i = 0; i < numFaces; i++) {
                
                size_t vertIndex = 3 * i;

                // Left and right points respectively
                Vertex& left = rPoints[i];
                Vertex& right = rPoints[i + 1];

                // References to the vertices which will form our triangle
                Vertex& a = verts[vertIndex];
                Vertex& b = verts[vertIndex + 1];
                Vertex& c = verts[vertIndex + 2];

                a = rCenter;
                b = left;
                c = right;
            }
        }

        void doStrip() {

            // Dereference left and right line segments
            std::vector<Vertex>& rLeft = *pLeft;
            std::vector<Vertex>& rRight = *pRight;

            // Calculate number of faces and vertices
            size_t numQuads = (rLeft.size() - 1);
            numFaces = numQuads * 2;
            numVerts = numFaces * 3;

            // Resize vertex buffer to match needed vertices
            vertices->resize(numVerts);
            Vertex* verts = vertices->verts();

            for (size_t i = 0; i < numQuads; i++) {

                size_t vertIndex = 6 * i;

                // Points a and b on left side
                Vertex& left_a = rLeft[i];
                Vertex& left_b = rLeft[i + 1];

                // Points a and b on right side
                Vertex& right_a = rRight[i];
                Vertex& right_b = rRight[i + 1];

                // References to vertices for both triangles
                Vertex& a = verts[vertIndex];
                Vertex& b = verts[vertIndex + 1];
                Vertex& c = verts[vertIndex + 2];
                Vertex& d = verts[vertIndex + 3];
                Vertex& e = verts[vertIndex + 4];
                Vertex& f = verts[vertIndex + 5];

                // Construct first triangle
                a = left_a;
                b = left_b;
                c = right_a;

                // Construct second triangle
                d = right_a;
                e = right_b;
                f = left_b;
            }
        }

        void compute() override {

            data->color = color;

            switch (topology) {
                case (Topology::Fan): { this->doFan(); break; }
                case (Topology::Strip): { this->doStrip(); break; }
            }
        }

        void draw() override {

            pipeline->bind();

            databuff->bind(1);
            vertices->bind();
            
            canvas->drawArrays(Pipeline::Topology::TriangleList, 0, numVerts);
        }
    };
};