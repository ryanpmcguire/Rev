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

        }        
    };
};