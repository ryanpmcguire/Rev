module;

#include <string>
#include <vector>
#include <dbg.hpp>

export module Rev.Element.Box;
 
import Rev.Core.Rect;
import Rev.Core.Color;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Graphics.Canvas;
import Rev.Primitive.Rectangle;

export namespace Rev::Element {

    using namespace Rev::Primitive;

    struct Box : public Element {

        Primitive::Rectangle* rectangle = nullptr;

        // Create
        Box(Element* parent, StyleList styles = {}, std::string name = "Box") : Element(parent, styles, name) {

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

            // Assign rect, fill color
            data.rect = this->rect;
            data.color = styleRef.background.color;

            // Compute corner radii
            //--------------------------------------------------

            float tl, tr, bl, br;

            float mainRad = styleRef.border.radius.val;
            tl = tr = bl = br = mainRad;

            if (styleRef.border.tl.radius.val) { tl = styleRef.border.tl.radius.val; }
            if (styleRef.border.tr.radius.val) { tr = styleRef.border.tr.radius.val; }
            if (styleRef.border.bl.radius.val) { bl = styleRef.border.bl.radius.val; }
            if (styleRef.border.br.radius.val) { br = styleRef.border.br.radius.val; }

            data.corners = { tl, tr, bl, br };

            // Compute border widths
            //--------------------------------------------------

            float wl, wr, wt, wb;
            
            wl = wr = styleRef.border.width.resolve(rect.w);
            wt = wb = styleRef.border.width.resolve(rect.h);
            
            data.borderWidth = { wl, wr, wt, wb };

            // Compute border colors
            //--------------------------------------------------

            Core::Color cl, cr, ct, cb;
            cl = cr = styleRef.border.color;
            ct = cb = styleRef.border.color;

            data.borderColor = { cl, cr, ct, cb };

            Element::computePrimitives(e);
        }

        // Draw own rect
        void stencil(Event& e) override {
            rectangle->stencil();
            Element::stencil(e);
        }

        // Draw own rect
        void draw(Event& e) override {

            Graphics::Canvas& canvas = *(shared->canvas);
            std::vector<Element*>& stencilStack = shared->stencilStack;

            rectangle->draw();

            // Draw stencil only after drawing self
            if (computed.style.overflow == Overflow::Hide) {

                // Push element, set pre-draw stencil depth
                stencilStack.push_back(this);
                canvas.stencilPush(stencilStack.size() - 1);

                // Draw stencil, set post-draw stencil depth
                stencilStack.back()->stencil(e);
                canvas.stencilDepth(stencilStack.size());
            }

            Element::draw(e);
        }
    };
};