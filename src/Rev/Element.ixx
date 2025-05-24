module;

#include <vector>

export module Element;

import Rev;
import WebGpu;

export namespace Rev {

    struct Element {

        WebGpu::Surface* surface;

        Element* parent = nullptr;
        std::vector<Element*> children;
        
        Rect rect;

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
    };
}