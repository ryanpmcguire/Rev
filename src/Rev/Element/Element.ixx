module;

#include <vector>

export module Element;

import WebGpu;

import Rect;
import Style;
import Computed;

export namespace Rev {

    struct Element {

        WebGpu::Surface* surface;

        Element* parent = nullptr;
        std::vector<Element*> children;
        
        Rect rect;
        Style style;
        Computed computed;

        // Create
        Element(Element* parent = nullptr) {

            if (parent) {
                parent->children.push_back(this);
                surface = parent->surface;
            }
        }
        
        // Destroy
        ~Element() {

            // Remove all children
            for (Element* child : children) { delete child; }
            children.clear();
        }

        // Comptue style
        void computeStyle() {

            // For now, simply set
            computed.style = style;

            for (Element* child : children) {
                child->computeStyle();
            }
        }
        
        // Compute attributes
        virtual void computePrimitives() {

            for (Element* child : children) {
                child->computePrimitives();
            }
        }
    };
}