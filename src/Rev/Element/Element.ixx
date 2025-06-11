module;

#include <vector>

export module Rev.Element;

import Rev.Rect;
import Rev.Style;
import Rev.Computed;
import Rev.Event;

export namespace Rev {

    struct Element {

        struct TopLevelDetails {
            std::vector<Element*> dirtyElements;
        };

        TopLevelDetails* topLevelDetails = nullptr;

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
                topLevelDetails = parent->topLevelDetails;
                topLevelDetails->dirtyElements.push_back(this);
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

            // Set self
            this->dirty = true;
            e.causedRefresh = true;
            topLevelDetails->dirtyElements.push_back(this);

            // Propagate upwards
            if (parent && !parent->dirty) { parent->refresh(e); }
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