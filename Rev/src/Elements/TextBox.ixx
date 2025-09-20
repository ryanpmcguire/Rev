module;

#include <cstdio>
#include <string>

export module Rev.TextBox;

import Rev.Style;
import Rev.Event;
import Rev.Box;

import Rev.Graphics.Text;

export namespace Rev {

    struct TextBox : public Box {

        Text* text = nullptr;

        // Create
        TextBox(Element* parent, std::string content = "Test Kerning") : Box(parent, "TextBox") {

            measure = true;

            text = new Text();
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
            
            Text::MinMax minMax = text->measure();

            text->layout(99999999.0f);

            style->size = {
                .width = Px(text->dims.width), .height = Px(text->dims.height)
            };

            Box::computeStyle(e);
        }

        void computePrimitives(Event& e) override {

            text->fontSize = computed.style.text.size.val;
            if (!text->fontSize) { text->fontSize = 12.0f; }

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            text->xPos = rect.x;
            text->yPos = rect.y;

            text->draw(e.canvas);
        }
    };
};