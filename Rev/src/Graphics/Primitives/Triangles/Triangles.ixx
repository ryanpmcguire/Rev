module;

#include <cmath>
#include <vector>

export module Rev.Graphics.Triangles;

import Rev.Graphics.Canvas;
import Rev.Graphics.Primitive;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

// Shader file resources
import Resources.Shaders.OpenGL.Triangles.Triangles_vert;
import Resources.Shaders.OpenGL.Triangles.Triangles_frag;
import Resources.Shaders.Metal.Triangles.Triangles_metal;

import Rev.Pos;

export namespace Rev {

    struct Triangles : public Primitive {

        // Shared across instances
        struct Shared {

            size_t refCount = 0;

            Pipeline* pipeline = nullptr;

            Shared() {
                
            }

            void create(Canvas* canvas) {

                refCount++;

                if (refCount > 1) { return; }

                pipeline = new Pipeline(canvas->context, {
                    .openGlVert = Triangles_vert,
                    .openGlFrag = Triangles_frag,
                    .metalUniversal = Triangles_metal
                }, 2, false);
            }

            void destroy() {

                // Subtract refcount, return if remaining
                if (refCount--) { return; }

                // Delete resourcesfg
                delete pipeline;
            }
        };

        enum Topology {
            List, Fan, Strip
        };

        // Instance-specific data
        struct Data {
            struct Color { float r, g, b, a; };
            Color color = { 1, 1, 1, 1 };
        };

        inline static Shared shared;
        UniformBuffer* databuff = nullptr;
        VertexBuffer* vertices = nullptr;

        Topology topology;
        Data* data = nullptr;
        bool dirty = true;

        // We may own our points, or we may be given a pointer to some other points
        Pos center; std::vector<Pos> points, left, right;
        Pos* pCenter; std::vector<Pos>* pPoints, *pLeft, *pRight;

        size_t numFaces, numVerts;

        struct Props {

            Topology topology;

            Pos* center;
            std::vector<Pos>* points;

            std::vector<Pos>* left;
            std::vector<Pos>* right;
        };
        
        // Create
        Triangles(Canvas* canvas, Props props = {}) : Primitive(canvas) {

            // Set props
            //--------------------------------------------------

            topology = props.topology;

            if (props.center) { pCenter = props.center; }
            else { pCenter = &center; }

            if (props.points) { pPoints = props.points; }
            else { pPoints = &points; }

            if (props.left) { pLeft = props.left; }
            else { pLeft = &left; }

            if (props.right) { pRight = props.right; }
            else { pRight = &right; }

            // Initialize resources
            //--------------------------------------------------
            
            shared.create(canvas);

            vertices = new VertexBuffer(canvas->context);
            databuff = new UniformBuffer(canvas->context, sizeof(Data));

            data = (Data*)databuff->data;
            *data = Data();
        }

        // Destroy
        ~Triangles() {

            shared.destroy();

            delete vertices;
            delete databuff;
        }

        void doFan() {

            Pos& rCenter = *pCenter;
            std::vector<Pos>& rPoints = *pPoints;

            // Calculate number of faces and vertices
            numFaces = rPoints.size() - 1;
            numVerts = numFaces * 3;

            // Resize vertex buffer to match needed vertices
            vertices->resize(numVerts);
            VertexBuffer::Vertex* verts = vertices->verts();
        
            // Compute faces for triangle fan from center and perimeter (points)
            for (size_t i = 0; i < numFaces; i++) {
                
                size_t vertIndex = 3 * i;

                // Left and right points respectively
                Pos& left = rPoints[i];
                Pos& right = rPoints[i + 1];

                // References to the vertices which will form our triangle
                VertexBuffer::Vertex& a = verts[vertIndex];
                VertexBuffer::Vertex& b = verts[vertIndex + 1];
                VertexBuffer::Vertex& c = verts[vertIndex + 2];

                // Construct triangle
                a = { rCenter.x, rCenter.y };
                b = { left.x, left.y };
                c = { right.x, right.y };
            }
        }

        void doStrip() {

            // Dereference left and right line segments
            std::vector<Pos>& rLeft = *pLeft;
            std::vector<Pos>& rRight = *pRight;

            // Calculate number of faces and vertices
            size_t numQuads = (rLeft.size() - 1);
            numFaces = numQuads * 2;
            numVerts = numFaces * 3;

            // Resize vertex buffer to match needed vertices
            vertices->resize(numVerts);
            VertexBuffer::Vertex* verts = vertices->verts();

            for (size_t i = 0; i < numQuads; i++) {

                size_t vertIndex = 6 * i;

                // Points a and b on left side
                Pos& left_a = rLeft[i];
                Pos& left_b = rLeft[i + 1];

                // Points a and b on right side
                Pos& right_a = rRight[i];
                Pos& right_b = rRight[i + 1];

                // References to vertices for both triangles
                VertexBuffer::Vertex& a = verts[vertIndex];
                VertexBuffer::Vertex& b = verts[vertIndex + 1];
                VertexBuffer::Vertex& c = verts[vertIndex + 2];
                VertexBuffer::Vertex& d = verts[vertIndex + 3];
                VertexBuffer::Vertex& e = verts[vertIndex + 4];
                VertexBuffer::Vertex& f = verts[vertIndex + 5];

                // Construct first triangle
                a = { left_a.x, left_a.y };
                b = { left_b.x, left_b.y };
                c = { right_a.x, right_a.y};

                // Construct second triangle
                d = { right_a.x, right_a.y };
                e = { right_b.x, right_b.y };
                f = { left_b.x, left_b.y };
            }
        }

        void compute() override {

            switch (topology) {
                case (Topology::Fan): { this->doFan(); break; }
                case (Topology::Strip): { this->doStrip(); break; }
            }
        }

        void draw() override {

            shared.pipeline->bind();

            databuff->bind(1);
            vertices->bind();
            
            canvas->drawArrays(Pipeline::Topology::TriangleList, 0, numVerts);
        }
    };
};