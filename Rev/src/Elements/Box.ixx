module;

#include <string>
#include <vector>
#include <dbg.hpp>

export module Rev.Box;

import Rev.Element;
import Rev.Event;
import Rev.Style;
import Rev.Rect;

import Rev.Graphics.Rectangle;
import Rev.Graphics.Lines;

export namespace Rev {

    //using namespace WebGpu;

    struct Box : public Element {

        Rectangle* rectangle = nullptr;
        Lines* lines = nullptr;

        // Create
        Box(Element* parent, std::string name = "Box") : Element(parent, name) {

            rectangle = new Rectangle();
            lines = new Lines(10);
        }

        // Destroy
        ~Box() {
            delete rectangle;
            delete lines;
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
                }
            };

            rectangle->dirty = true;

            Element::computePrimitives(e);
        }

        void draw(Event& e) override {
            
            rectangle->draw();

            Element::draw(e);
        }
    };
};