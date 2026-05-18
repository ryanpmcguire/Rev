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

        std::vector<Core::Vertex3>* pTriangles = nullptr;
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

        std::vector<Core::Vertex3>* getTriangles() {

            if (mesh->pTriangles) {
                return mesh->pTriangles;
            }

            return &(mesh->triangles);
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
    };
}