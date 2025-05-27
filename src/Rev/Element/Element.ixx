module;

#include <vector>

export module Element;

import WebGpu;

import Rect;
import Style;
import Computed;
import Event;

export namespace Rev {

    struct Element {

        WebGpu::Surface* surface;

        Element* parent = nullptr;
        std::vector<Element*> children;
        
        Rect rect;

        Style style;
        std::vector<Style*> styles;
        
        Computed computed;
        bool dirty = true;

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
        void computeStyle(Event& e) {

            // Apply other styles, then own style
            computed.style.apply(styles);
            computed.style.apply(style);

            for (Element* child : children) {
                child->computeStyle(e);
            }
        }
        
        // Compute attributes
        virtual void computePrimitives(Event& e) {

            for (Element* child : children) {
                child->computePrimitives(e);
            }
        }

        // Event
        //--------------------------------------------------

        virtual void refresh(Event& e) {
            this->dirty = true;
            e.causedRefresh = true;
        }

        virtual bool contains(Event& e) {
            return rect.contains(e.mouse.pos);
        }

        virtual void mouseDown(Event& e) {

            for (Element* child : children) {
                if (child->contains(e)) { child->mouseDown(e); }
                if (!e.propagate) { break; }
            }
        }
    };
}