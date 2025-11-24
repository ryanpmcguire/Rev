module;

#include <cstdio>
#include <string>
#include <vector>

#include <managed.hpp>
#include <dbg.hpp>

export module Rev.Element.Text;

import Rev.Element.Style;
import Rev.Element.Event;
import Rev.Element.Box;

import Rev.Core.Pos;
import Rev.Core.Resource;
import Rev.Core.Observable;
import Rev.Core.Font;

import Rev.Primitive.Text;
import Rev.Primitive.Lines;

export namespace Rev::Element {

    namespace TextStyles {
        
        Style TextDefaults = {
            .text = {
                .font = File("Rev/resources/Fonts/Arial/Arial.ttf"),
                .color = rgba(0, 0, 0, 1),
                .size = 12_px,
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

        // Managing input
        //--------------------------------------------------

        void mouseDown(Event& e) override {

            Core::Pos& selectPos = e.mouse.pos;

            for (Primitive::Text::Line& line : text->lines) {
                if (line.rect.contains(selectPos)) {
                    dbg("[Text] rect intersects!");
                }
            }

            Box::mouseDown(e);
        }

        void textInput(Event& e) override {

            dbg("[Text] Input: %s", e.keyboard.input.c_str());

            content += e.keyboard.input;

            this->refresh(e);
            Box::textInput(e);
        }

        // Compute style/primitive/etc
        //--------------------------------------------------

        void computeStyle(Event& e) override {

            if (content.changed()) {
                this->dirty.style = true;
            }
            
            Box::computeStyle(e);
        }

        // Resolve style (requires measuring text)
        //--------------------------------------------------

        enum WrapMode {
            None,
            BreakChar,
            BreakWord
        };
        
        WrapMode mode = WrapMode::BreakChar;
        std::string strContent;
        float fontSize = 12.0f;
        Font* font = nullptr;

        float width, height;
        float minWidth, minHeight;
        float maxWidth, maxHeight;

        void measureText() {

            struct Tracked {
                float current = 0;
                float max = 0;
                float min = 99999999;
            };

            Font& fontRef = *font;
            Tracked letter, word, line;

            // Track theoretical min/max letter, word, and line
            //--------------------------------------------------
            
            // Iterate through each character in the content
            for (char c : strContent) {

                // Track current
                letter.current = fontRef.glyphs[c].advance;
                word.current += letter.current;
                line.current += letter.current;

                // Always track max char
                letter.min = std::min(letter.min, letter.current);
                letter.max = std::max(letter.max, letter.current);

                if (c == ' ') {
                    word.min = std::min(word.min, word.current);
                    word.max = std::max(word.max, word.current);
                    word.current = 0;
                }

                // End of line
                if (c == '\n') {
                    line.min = std::min(line.min, line.current);
                    line.max = std::max(line.max, line.current);
                    line.current = 0;
                }
            }

            // Min/max any that weren't caught in the loop
            //--------------------------------------------------

            letter.min = std::min(letter.min, letter.current);
            letter.max = std::max(letter.max, letter.current);

            word.min = std::min(word.min, word.current);
            word.max = std::max(word.max, word.current);

            line.min = std::min(line.min, line.current);
            line.max = std::max(line.max, line.current);

            // We set our actual min/max depending on the wrap mode
            switch (mode) {

                case (WrapMode::None): {
                    minWidth = line.min;
                    maxWidth = line.max;
                    break;
                }

                case (WrapMode::BreakChar): {
                    minWidth = letter.max;
                    maxWidth = line.max;
                    break;
                }

                case (WrapMode::BreakWord): {
                    minWidth = word.max;
                    maxWidth = line.max;
                    break;
                }
            }

            // Add line height as min width
            minHeight = fontRef.lineHeight;
        }

        void resolveStyle(Event& e) override {

            Box::resolveStyle(e);

            strContent = content;
            fontSize = resolved.style.text.size.val;
            if (!fontSize) { fontSize = 12.0f; }

            Core::Resource fontResource = resolved.style.text.font;
            if (!fontResource.data) { fontResource = File("Rev/resources/Fonts/Arial/Arial.ttf"); }

            font = text->fontAtlas->get(fontResource, fontSize, shared->canvas->details.scale);
            text->font = font;
            
            text->fontSize = fontSize;
            text->content = strContent;

            this->measureText();
            maxWidth = 99999999.0f;

            this->layoutText();

            float minPaddingWidth = resolved.getMinPadding(Axis::Horizontal, Dist::Type::Abs);
            float minPaddingHeight = resolved.getMinPadding(Axis::Vertical, Dist::Type::Abs);

            resolved.style.size.min.width = Px(width + minPaddingWidth);
            resolved.style.size.min.height = Px(height + minPaddingHeight);
        }

        // Computing text layout
        //--------------------------------------------------

        void layoutText() {

            // Layout text
            //--------------------------------------------------

            text->lines.clear();

            size_t idx = 0;
            float pos = 0;
            
            float x = 0;
            float y = font->ascent;

            Font& fontRef = *font;
            Primitive::Text::Line line = { "", idx, idx, { x, y, 0.0f, fontRef.lineHeight } };

            for (char c : strContent) {

                float charWidth = fontRef.glyphs[c].advance;
                float newWidth = line.rect.w + charWidth;

                // Reset line on overflow
                if (idx > 0 && (newWidth > maxWidth || c == '\r')) {

                    text->lines.push_back(line);

                    line = { "", idx, idx, { x, y, charWidth, fontRef.lineHeight } };
                }

                // Continue line
                else {
                    line.rect.w += fontRef.glyphs[c].advance;
                }

                line.content += c;
                line.end = idx;
                idx += 1;
            }

            text->lines.push_back(line);

            // Measure dims
            //--------------------------------------------------

            width = 0;
            height = 0;

            for (Primitive::Text::Line& line : text->lines) {
                height += line.rect.h;
                width = std::max(width, line.rect.w);
            }
        }

        // Here we compute the layout ourselves
        void computeLayout() override {

            layout = Layout();
            layout.size.w = { .val = width, .min = width };
            layout.size.h = { .val = height, .min = height };
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

            float runningY = 0;

            for (Primitive::Text::Line& line : text->lines) {
                line.rect.x = rect.x + resolved.pad.l.val;
                line.rect.y = rect.y + resolved.pad.t.val;
            }

            text->compute();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            text->draw();
        }
    };
};