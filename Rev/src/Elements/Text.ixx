module;

#include <cstdio>
#include <string>

export module Rev.Element.Text;

import Rev.Element.Style;
import Rev.Element.Event;
import Rev.Element.Box;

import Rev.Core.Observable;
import Rev.Primitive.Text;

import Resources.Fonts.Arial.Arial_ttf;

export namespace Rev::Element {

    namespace TextStyles {
        
        Style TextDefaults = {
            .text = {
                .font = Arial_ttf,
                .size = 12_px,
                .color = rgba(0, 0, 0, 1),
            }
        };
    };

    using namespace TextStyles;

    struct Text : public Box {

        Primitive::Text* text = nullptr;

        Observable<std::string> content;

        // Create
        Text(Element* parent, std::string content = "Hello World", StyleList styles = {}) : Box(parent, styles, "Text") {

            text = new Primitive::Text(shared->canvas);
            this->styles.prepend(&TextStyles::TextDefaults);
            this->content = content;
        }

        // Destroy
        ~Text() {

            delete text;
        }

        // Managing content
        //--------------------------------------------------

        // Set content as a value
        void addContent(float val, int digits = 4) {

            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", 10, val);

            size_t count = 0;
            size_t point = 0;

            for (char& c : buffer) {
                
                if (c != '.') { count += 1; }
                else if (count ==  digits) { c = '\0'; break; }

                if (count > digits) { c = '\0'; break; }
            }

            content += buffer;
        }

        // Set content as a string
        void addContent(std::string content) {
            content += content;
        }

        void setContent(std::string content) {
            content = content;
        }

        void setContent(float val, int digits = 4) {
            content = "";
            addContent(val, digits);
        }

        // Compute style/primitive/etc
        //--------------------------------------------------

        void computeStyle(Event& e) override {

            if (content.changed()) {
                this->dirty.style = true;
            }
            
            Box::computeStyle(e);
        }

        void resolveStyle(Event& e) override {

            Box::resolveStyle(e);

            text->fontSize = resolved.style.text.size.val;
            text->content = content;

            Primitive::Text::MinMax minMax = text->measure();
            text->layout(99999999.0f);

            float minPaddingWidth = this->getMinPadding(Axis::Horizontal, Dist::Type::Abs);
            float minPaddingHeight = this->getMinPadding(Axis::Vertical, Dist::Type::Abs);

            resolved.style.size.min.width = Px(text->dims.width + minPaddingWidth);
            resolved.style.size.min.height = Px(text->dims.height + minPaddingHeight);
        }

        // Here we compute the layout ourselves
        void computeLayout() override {

            layout = Layout();
            layout.size.w = { .val = text->dims.width, .min = text->dims.width };
            layout.size.h = { .val = text->dims.height, .min = text->dims.height };
        }

        void computePrimitives(Event& e) override {

            // Set font size
            text->fontSize = resolved.style.text.size.val;
            if (!text->fontSize) { text->fontSize = 12.0f; }

            // Set font color
            text->data->color = {
                resolved.style.text.color.r, resolved.style.text.color.g,
                resolved.style.text.color.b, resolved.style.text.color.a
            };

            text->xPos = rect.x + resolved.pad.l.val;
            text->yPos = rect.y + resolved.pad.t.val;

            text->compute();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            text->draw();
        }
    };
};