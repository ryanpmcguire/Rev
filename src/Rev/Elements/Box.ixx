module;

#include <vector>

export module Box;

import Element;

import WebGpu;
import Topology;
import Primitive;
import UniformBuffer;
import RoundedBox;
import Vertex;
import Style;
import Shader;
import Color;
import Rect;

export namespace Rev {

    using namespace WebGpu;

    struct Box : public Element {

        RoundedBox* roundedBox = nullptr;
        
        // Create
        Box(Element* parent) : Element(parent) {

            roundedBox = new RoundedBox(surface);
        }

        // Destroy
        ~Box() {
            delete roundedBox;
        }

        void computePrimitives() override {

            Style& styleRef = computed.style;

            // Test rect
            this->rect = {
                100, 100,
                styleRef.size.width.val, styleRef.size.height.val
            };

            // Compute left, right, top bottom
            float l = rect.x, r = rect.x + rect.w;
            float t = rect.y, b = rect.y + rect.h;

            // Corner vertices (nominal)
            Vertex tlv = { l, t }, trv = { r, t };
            Vertex blv = { l, b }, brv = { r, b };

            // Set vertex positions
            roundedBox->vertices->dirty = true;
            roundedBox->vertices->members = {
                tlv, trv, blv,
                blv, trv, brv
            };

            Element::computePrimitives();
        }
    };
};