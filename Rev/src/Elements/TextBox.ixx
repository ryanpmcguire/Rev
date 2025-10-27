module;

#include <cstdio>
#include <string>

export module Rev.Element.TextBox;

import Rev.Element.Style;
import Rev.Element.Event;
import Rev.Element.Box;

import Rev.Primitive.Text;

export namespace Rev::Element {

    struct TextBox : public Box {

        Text* text = nullptr;

        // Create
        TextBox(Element* parent, std::string content = "Hello World") : Box(parent, "TextBox") {

            measure = true;

            text = new Text(shared->canvas);
            text->content = content;
        }

        // Destroy
        ~TextBox() {

            delete text;
        }

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

            text->content += buffer;
        }

        // Set content as a string
        void addContent(std::string content) {
            text->content += content;
        }

        void setContent(std::string content) {
            text->content = content;
        }

        void setContent(float val, int digits = 4) {
            text->content = "";
            addContent(val, digits);
        }

        void computeStyle(Event& e) override {

            Box::computeStyle(e);
            
            text->fontSize = computed.style.text.size.val;
            if (!text->fontSize) { text->fontSize = 12.0f; }

            Text::MinMax minMax = text->measure();
            text->layout(99999999.0f);

            style->size = {
                .width = Px(text->dims.width), .height = Px(text->dims.height)
            };

            Box::computeStyle(e);
        }

        void computePrimitives(Event& e) override {

            // Set font size
            text->fontSize = computed.style.text.size.val;
            if (!text->fontSize) { text->fontSize = 12.0f; }

            // Set font color
            text->data->color = {
                computed.style.text.color.r, computed.style.text.color.g,
                computed.style.text.color.b, computed.style.text.color.a
            };

            text->data->depth = (float)depth / 100.0f;

            text->xPos = rect.x;
            text->yPos = rect.y;

            text->compute();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            text->draw();
        }
    };
};