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

    using namespace Rev::Primitives;

    struct Box : public Element {

        Primitives::Rectangle* rectangle = nullptr;

        // Create
        Box(Element* parent, StyleList styles = {}, std::string name = "Box") : Element(parent, styles, name) {
            rectangle = new Rectangle(shared->canvas);
        }

        // Destroy
        ~Box() {

            //dbg("[Box] destroying");
            delete rectangle;
        }

        void computePrimitives(Event& e) override {

            Style& styleRef = resolved.style;
            
            // Box data
            //--------------------------------------------------

            Rectangle::Data& data = *rectangle->data;

            // Assign rect, fill color
            data.rect = this->rect.rounded().translate({ 0.0, 0.0 });
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
            
            if (styleRef.border.left.width) { wl = styleRef.border.left.width.resolve(rect.w); }
            if (styleRef.border.right.width) { wr = styleRef.border.right.width.resolve(rect.w); }
            if (styleRef.border.top.width) { wt = styleRef.border.top.width.resolve(rect.h); }
            if (styleRef.border.bottom.width) { wb = styleRef.border.bottom.width.resolve(rect.h); }

            data.borderWidth = { wl, wr, wt, wb };

            // Compute border colors
            //--------------------------------------------------

            Core::Color cl, cr, ct, cb;
            cl = cr = styleRef.border.color;
            ct = cb = styleRef.border.color;

            if (styleRef.border.left.color) { cl = styleRef.border.left.color; }
            if (styleRef.border.right.color) { cr = styleRef.border.right.color; }
            if (styleRef.border.top.color) { ct = styleRef.border.top.color; }
            if (styleRef.border.bottom.color) { cb = styleRef.border.bottom.color; }

            data.borderColor = { cl, cr, ct, cb };

            // Compute shadow
            //--------------------------------------------------

            data.shadow = {
                .x = styleRef.shadow.x.resolve(rect.w),
                .y = styleRef.shadow.y.resolve(rect.h),
                .size = styleRef.shadow.size.resolve(0),
                .blur = styleRef.shadow.blur.resolve(0),
                .color = styleRef.shadow.color
            };

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
            if (resolved.style.overflow == Overflow::Hide) {

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