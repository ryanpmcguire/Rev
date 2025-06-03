module;

#include <vector>

export module Rev.Element;

import WebGpu;

import Rev.Rect;
import Rev.Style;
import Rev.Computed;
import Rev.Event;

import Dirty;

using namespace Dirty;

export namespace Rev {

    struct Element {

        WebGpu::Surface* surface;

        Element* parent = nullptr;
        std::vector<Element*> children;
        
        Rect rect;

        Style style;
        std::vector<Style*> styles;
        
        Computed computed;
        
        DirtyFlag dirty;

        // Create
        Element(Element* parent = nullptr) {

            if (parent) {

                parent->children.push_back(this);
                surface = parent->surface;

                dirty.tells(&parent->dirty);
            }

            style.dirty.tells({ &dirty, &computed.style.dirty });

            computed.style.dirty.beforeClean([this]() {
                this->computeStyle();
                this->computePrimitives();
            });
        }
        
        // Destroy
        ~Element() {

            // Remove all children
            for (Element* child : children) { delete child; }
            children.clear();
        }

        // Comptue style
        void computeStyle() {

            // Apply other styles, then own style
            computed.style.apply(styles);
            computed.style.apply(style);
        }
        
        // Compute attributes
        virtual void computePrimitives() {
        }

        // Event
        //--------------------------------------------------

        virtual void refresh(Event& e) {
            //this->dirty = true;
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