module;

export module Box;

import Rev;
import Element;

import WebGpu;
import Topology;
import Primitive;
import Triangles;

export namespace Rev {

    using namespace WebGpu;

    struct Box : public Element {

        Triangles* triangles = nullptr;
        
        struct Rect { float x, y, w, h; };
        struct Color { float r, g, b, a; };
        struct Radii { float tl, tr, bl, br; };

        Rect rect;
        Color color;
        Radii radii;
        
        // Create
        Box(Element* parent) : Element(parent) {

            triangles = new Triangles(surface, Topology::TriangleList);

        }

        // Destroy
        ~Box() {

            delete triangles;
        }

        void compute() override {

            triangles->vertices->dirty = true;
            triangles->vertices->members = {
                { -0.5f, -0.5f },
                {  0.5f, -0.5f },
                {  0.0f,  0.5f }
            };

            triangles->colors->dirty = true;
            triangles->colors->members = {
                { 1, 0, 0, 1 },
                { 0, 1, 0, 1 },
                { 0, 0, 1, 1 }
            };

            Element::compute();
        }
    };
};