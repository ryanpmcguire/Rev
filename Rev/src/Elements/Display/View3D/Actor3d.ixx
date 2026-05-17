module;

#include <vector>

export module Rev.Element.View3d.Actor3d;

import Rev.Core.Color;
import Rev.Core.Vertex3;

import Rev.Primitive.Mesh;
import Rev.Graphics.Canvas;

export namespace Rev {

    struct Actor3D {

        Primitives::Mesh* mesh = nullptr;

        bool visible = true;

        // If true, Actor3D deletes mesh in destructor.
        bool ownsMesh = false;

        // If true, Actor3D owns the triangle data used by mesh.
        bool ownsTriangles = false;

        std::vector<Core::Vertex3> triangles;

        // Later:
        // glm::mat4 transform = glm::mat4(1.0f);
        // bool selected = false;
        // std::string name;
        // uint32_t id = 0;

        ~Actor3D() {

            if (ownsMesh) {
                delete mesh;
            }

            mesh = nullptr;
        }

        void compute() {

            if (!mesh) {
                return;
            }

            mesh->compute();
        }

        void draw() {

            if (!visible) {
                return;
            }

            if (!mesh) {
                return;
            }

            mesh->draw();
        }

        // Cube construction
        //--------------------------------------------------

        static Core::Vertex3 makeVertex(
            float x,
            float y,
            float z,
            Core::Color color,
            float nx,
            float ny,
            float nz
        ) {
            Core::Vertex3 v = {
                x,
                y,
                z,
                color
            };

            v.nx = nx;
            v.ny = ny;
            v.nz = nz;

            return v;
        }

        static void pushTriangle(
            std::vector<Core::Vertex3>& out,
            Core::Vertex3 a,
            Core::Vertex3 b,
            Core::Vertex3 c
        ) {
            out.push_back(a);
            out.push_back(b);
            out.push_back(c);
        }

        static void pushQuad(
            std::vector<Core::Vertex3>& out,
            Core::Vertex3 a,
            Core::Vertex3 b,
            Core::Vertex3 c,
            Core::Vertex3 d
        ) {
            pushTriangle(out, a, b, c);
            pushTriangle(out, a, c, d);
        }

        static void buildCubeTriangles(
            std::vector<Core::Vertex3>& out
        ) {
            out.clear();

            Core::Color color = {
                0.75f,
                0.75f,
                0.82f,
                1.0f
            };

            float s = 1.0f;

            // Front, +Z
            pushQuad(
                out,
                makeVertex(-s, -s,  s, color, 0, 0, 1),
                makeVertex( s, -s,  s, color, 0, 0, 1),
                makeVertex( s,  s,  s, color, 0, 0, 1),
                makeVertex(-s,  s,  s, color, 0, 0, 1)
            );

            // Back, -Z
            pushQuad(
                out,
                makeVertex( s, -s, -s, color, 0, 0, -1),
                makeVertex(-s, -s, -s, color, 0, 0, -1),
                makeVertex(-s,  s, -s, color, 0, 0, -1),
                makeVertex( s,  s, -s, color, 0, 0, -1)
            );

            // Left, -X
            pushQuad(
                out,
                makeVertex(-s, -s, -s, color, -1, 0, 0),
                makeVertex(-s, -s,  s, color, -1, 0, 0),
                makeVertex(-s,  s,  s, color, -1, 0, 0),
                makeVertex(-s,  s, -s, color, -1, 0, 0)
            );

            // Right, +X
            pushQuad(
                out,
                makeVertex( s, -s,  s, color, 1, 0, 0),
                makeVertex( s, -s, -s, color, 1, 0, 0),
                makeVertex( s,  s, -s, color, 1, 0, 0),
                makeVertex( s,  s,  s, color, 1, 0, 0)
            );

            // Top, +Y
            pushQuad(
                out,
                makeVertex(-s,  s,  s, color, 0, 1, 0),
                makeVertex( s,  s,  s, color, 0, 1, 0),
                makeVertex( s,  s, -s, color, 0, 1, 0),
                makeVertex(-s,  s, -s, color, 0, 1, 0)
            );

            // Bottom, -Y
            pushQuad(
                out,
                makeVertex(-s, -s, -s, color, 0, -1, 0),
                makeVertex( s, -s, -s, color, 0, -1, 0),
                makeVertex( s, -s,  s, color, 0, -1, 0),
                makeVertex(-s, -s,  s, color, 0, -1, 0)
            );
        }

        static Actor3D* Cube(
            Graphics::Canvas* canvas
        ) {
            Actor3D* actor = new Actor3D();

            actor->visible = true;
            actor->ownsMesh = true;
            actor->ownsTriangles = true;

            buildCubeTriangles(
                actor->triangles
            );

            actor->mesh = new Primitives::Mesh(canvas, {
                .triangles = &actor->triangles
            });

            actor->mesh->color = {
                0.75f,
                0.75f,
                0.82f,
                1.0f
            };

            return actor;
        }
    };
}