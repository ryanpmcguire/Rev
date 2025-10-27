module;

#include <string>
#include <vector>
#include <dbg.hpp>

export module Rev.Element.Box;
 
import Rev.Core.Rect;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Primitive.Rectangle;

export namespace Rev::Element {

    using namespace Rev::Primitive;

    struct Box : public Element {

        Primitive::Rectangle* rectangle = nullptr;

        // Create
        Box(Element* parent, std::string name = "Box") : Element(parent, name) {

            rectangle = new Rectangle(shared->canvas);
            scissor = true;
        }

        // Destroy
        ~Box() {

            //dbg("[Box] destroying");
            delete rectangle;
        }

        void computePrimitives(Event& e) override {

            Style& styleRef = computed.style;
            
            // Box data
            //--------------------------------------------------

            Rectangle::Data& data = *rectangle->data;

            float tl, tr, bl, br;

            float mainRad = styleRef.border.radius.val;
            tl = tr = bl = br = mainRad;

            if (styleRef.border.tl.radius.val) { tl = styleRef.border.tl.radius.val; }
            if (styleRef.border.tr.radius.val) { tr = styleRef.border.tr.radius.val; }
            if (styleRef.border.bl.radius.val) { bl = styleRef.border.bl.radius.val; }
            if (styleRef.border.br.radius.val) { br = styleRef.border.br.radius.val; }

            //roundedBox->boxDataBuffer->dirty = true;
            data = {

                .rect = {
                    rect.x, rect.y,
                    rect.w, rect.h
                },

                .color = {
                    styleRef.background.color.r, styleRef.background.color.g,
                    styleRef.background.color.b, styleRef.background.color.a
                },
                
                .corners = {
                    tl, tr, bl, br
                },
            };

            rectangle->compute();

            Element::computePrimitives(e);
        }

        // Draw own rect
        void stencil(Event& e) override {
            rectangle->draw();
            Element::stencil(e);
        }

        // Draw own rect
        void draw(Event& e) override {
            rectangle->draw();
            Element::draw(e);
        }
    };
};