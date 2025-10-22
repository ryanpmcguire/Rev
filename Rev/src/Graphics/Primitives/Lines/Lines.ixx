module;

#include <cmath>
#include <vector>
#include "./TriangulatePolyline.hpp"

export module Rev.Primitive.Lines;

import Rev.Primitive;
import Rev.Core.Shared;
import Rev.Core.Color;
import Rev.Core.Vertex;
import Rev.Core.Rect;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

// Shader file resources
import Resources.Shaders.OpenGL.Lines.Lines_vert;
import Resources.Shaders.OpenGL.Lines.Lines_frag;
import Resources.Shaders.Metal.Lines.Lines_metal;

export namespace Rev::Primitive {

    struct Lines : public Primitive {

        // Instance-specific data
        struct Data {
            Color color = { 1, 1, 1, 1 };
        };

        inline static Shared shared;
        inline static Pipeline* pipeline;

        UniformBuffer* databuff = nullptr;
        VertexBuffer* vertices = nullptr;

        Data* data = nullptr;
        bool dirty = true;

        Color color = { 1, 1, 1, 1 };
        float strokeWidth = 1.0f;

        struct Line {

            std::vector<Vertex> points;
            std::vector<Vertex>* pPoints = nullptr;

            Color color = { 1, 1, 1, 1 };
            float strokeWidth = 0.0f;

            size_t segs, quads, joins, verts;

            // Allow valid pointer to override points
            std::vector<Vertex>& getPoints() {
                if (pPoints) { return *pPoints; }
                return points;
            }
        };

        std::vector<Line> lines;

        size_t numSegments, numQuads, numJoins, numVerts;

        // Create
        Lines(Canvas* canvas, std::vector<std::vector<Vertex>*> pLines = {}) : Primitive(canvas) {

            // Initialize lines with pLines if possible
            for (std::vector<Vertex>*& pPoints : pLines) {
                lines.push_back({ .pPoints = pPoints });
            }

            // Create shared pipeline
            shared.create([canvas]() {
                pipeline = new Pipeline(canvas->context, {
                    .openGlVert = Lines_vert,
                    .openGlFrag = Lines_frag,
                    .metalUniversal = Lines_metal
                }, 2, false);
            });

            vertices = new VertexBuffer(canvas->context, { .attribs = { 2, 4 } });
            databuff = new UniformBuffer(canvas->context, sizeof(Data));

            data = (Data*)databuff->data;
            *data = Data();
        }

        // Destroy
        ~Lines() {

            shared.destroy([]() {
                delete pipeline;
            });

            delete vertices;
            delete databuff;
        }

        void compute() override {

            // Reset
            numSegments = numQuads = numJoins = numVerts = 0;

            // Calculate needed quads/joins/verts
            for (Line& line : lines) {

                // Deref, disqualify if too small
                std::vector<Vertex>& points = line.getPoints();

                // Calculate for line
                line.segs = points.size() - 1;
                line.quads = line.segs;
                line.joins = line.segs - 1;
                line.verts = 6 * line.quads + 3 * line.joins;

                // Sum globally
                numVerts += line.verts;
            }

            // Resize vertex buffer to match needed/expected vertices
            vertices->resize(numVerts);

            size_t offset = 0;

            for (Line& line : lines) {

                std::vector<Vertex>& rPoints = line.getPoints();
                Vertex* pVerts = vertices->verts();

                int numTriangles = triangulatePolyline(
                    reinterpret_cast<float*>(rPoints.data()), rPoints.size(),
                    reinterpret_cast<float*>(pVerts + offset), line.verts,
                    strokeWidth ? strokeWidth : line.strokeWidth
                );

                offset += line.verts;
            }

            data->color = color;
        }

        void draw() override {

            shared.pipeline->bind();

            databuff->bind(1);
            vertices->bind();
            
            canvas->drawArrays(Pipeline::Topology::TriangleList, 0, numVerts);
        }
    };
};