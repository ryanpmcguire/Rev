module;

#include <vector>

export module Box;

import Rev;
import Element;

import WebGpu;
import Topology;
import Primitive;
import Triangles;
import Vertex;
import Color;

export namespace Rev {

    using namespace WebGpu;

    struct Box : public Element {

        Triangles* triangles = nullptr;
        
        struct Rect { float x, y, w, h; };
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

            // Test rect
            this->rect = {
                0, 0,
                50, 50
            };

            // Compute left, right, top bottom
            float l = rect.x, r = rect.x + rect.w;
            float t = rect.y, b = rect.y + rect.h;

            // Corner vertices
            Vertex tlv = { l, t }, trv = { r, t };
            Vertex blv = { l, b }, brv = { r, b };

            // Corner colors
            Color tlc = { 1, 0, 0, 1 }, trc = { 0, 1, 0, 1};
            Color blc = { 1, 0, 0, 1 }, brc = { 0, 1, 0, 1};

            // Set vertex positions
            triangles->vertices->dirty = true;
            triangles->vertices->members = {
                tlv, trv, blv,
                blv, trv, brv
            };

            // Set vertex colors
            triangles->colors->dirty = true;
            triangles->colors->members = {
                tlc, trc, blc,
                blc, trc, brc
            };

            Element::compute();
        }
    };
};