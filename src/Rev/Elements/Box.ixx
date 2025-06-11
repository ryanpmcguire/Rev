module;

#include <vector>

#define DEBUG true
#include <dbg.hpp>

export module Rev.Box;

import Rev.Element;
import Rev.Style;
import Rev.Rect;

export namespace Rev {

    struct Box : public Element {

        // Test variable
        bool shouldRound = false;
        
        // Create
        Box(Element* parent) : Element(parent) {
        }

        // Destroy
        ~Box() {
        }

        void computePrimitives(Event& e) override {

            Style& styleRef = computed.style;

            // Test rect
            this->rect = {
                0, 0,
                styleRef.size.width.val, styleRef.size.height.val
            };
            

            Element::computePrimitives(e);
        }

        // Events
        //--------------------------------------------------

        Style roundedStyle = {
            .border = {
                .tl = { .radius = Px(20) },
                .tr = { .radius = Px(20) },
                .bl = { .radius = Px(20) },
                .br = { .radius = Px(20) }
            }
        };

        Style unRoundedStyle = {
            .border = {
                .tl = { .radius = Px(0) },
                .tr = { .radius = Px(0) },
                .bl = { .radius = Px(0) },
                .br = { .radius = Px(0) }
            }
        };

        void mouseDown(Event& e) override {

            // Toggle
            shouldRound = !shouldRound;
            style.apply(shouldRound ? roundedStyle : unRoundedStyle);

            refresh(e);

            dbg("[Box] MouseClick!");
        }
    };
};