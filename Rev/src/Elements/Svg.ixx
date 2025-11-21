module;

#include <string>
#include <vector>
#include <dbg.hpp>

export module Rev.Element.Svg;
 
import Rev.Core.Rect;
import Rev.Core.Color;
import Rev.Core.Resource;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;

import Rev.Graphics.Canvas;
import Rev.Primitive.Svg;

export namespace Rev::Element {

    using namespace Rev::Primitive;

    struct Svg : public Element {

        Primitive::Svg* svg = nullptr;
        Resource resource;

        // Create
        Svg(Element* parent, Resource resource, StyleList styles = {}, std::string name = "Svg") : Element(parent, styles, name) {

            svg = new Primitive::Svg(shared->canvas);
            this->resource = resource;
        }

        // Destroy
        ~Svg() {

            //dbg("[Box] destroying");
            delete svg;
        }

        void computePrimitives(Event& e) override {

            Style& styleRef = resolved.style;
            
            // Box data
            //--------------------------------------------------

            Primitive::Svg::Data& data = *svg->data;

            // Assign rect, fill color
            data.rect = this->rect.rounded().translate({ 0.0, 0.0 });
            data.color = styleRef.background.color;

            svg->resource = resource;
            svg->compute();

            Element::computePrimitives(e);
        }

        // Draw own rect
        void draw(Event& e) override {

            svg->draw();

            Element::draw(e);
        }
    };
};