module;

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

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
        Primitive::Lines* line = nullptr;

        Observable<std::string> content;

        enum WrapMode {
            None,
            BreakChar,
            BreakWord
        };
        
        WrapMode mode = WrapMode::None;
        std::string strContent;
        float fontSize = 12.0f;
        Font* font = nullptr;

        float width, height;
        float minWidth, minHeight;
        float maxWidth, maxHeight;

        // Create
        Text(Element* parent, std::string content = "Hello World", StyleList styles = {}) : Box(parent, styles, "Text") {

            text = new Primitive::Text(shared->canvas);
            line = new Primitive::Lines(shared->canvas);

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

        void insertAt(int pos, const std::string& toInsert) {
    
            // Read the current string out of the observable
            std::string str = content;
        
            // Clamp position (safety)
            pos = std::clamp(pos, 0, (int)str.size());
            str.insert(pos, toInsert);
        
            content = str;

            setCursorPos(cursor + (int)toInsert.size());
        }

        void deleteAt(int pos, int n) {

            // Nothing to delete
            if (n == 0) return;
        
            std::string str = content;
        
            int len = (int)str.size();
            if (len == 0) return;
        
            // Clamp pos inside string
            pos = std::clamp(pos, 0, len);
        
            int start = pos;
            int count = n;
        
            if (n < 0) {
                // Backspace-style deletion
                // delete characters BEFORE pos
                count = -n;
                start = pos - count;
            }
        
            // Clamp start and count to string boundaries
            start = std::clamp(start, 0, len);
            count = std::min(count, len - start);
        
            // Skip if nothing survives the clamps
            if (count <= 0) return;
        
            // Perform erase
            str.erase(start, count);
        
            // Write back
            content = str;
        
            // Update cursor position
            if (n < 0) {
                // Backspace → cursor moves backward
                cursor = start;
            }
            
            else {
                // Forward delete → cursor stays at pos (or nearest)
                cursor = std::clamp(pos, 0, (int)str.size());
            }
        }
        

        // Managing input
        //--------------------------------------------------

        int cursor = 0;

        void setCursorPos(int newCursor) {
            cursor = std::clamp(newCursor, 0, (int)strContent.size());
        }

        void mouseDown(Event& e) override {

            Core::Pos& selectPos = e.mouse.pos;

            for (Primitive::Text::Line& line : text->lines) {
                if (line.rect.contains(selectPos)) {

                    float left = line.rect.x;
                    int idx = line.start;

                    // Get char at x position
                    for (char c : line.content) {

                        float right = left + font->glyphs[c].advance;

                        if (selectPos.x >= left && selectPos.x <= right) {

                            float distLeft = selectPos.x - left;
                            float distRight = right - selectPos.x;

                            setCursorPos(distLeft < distRight ? idx : idx + 1);

                            break;
                        }

                        left = right;
                        idx += 1;
                    }
                }
            }

            this->refresh(e);
            Box::mouseDown(e);
        }

        void keyDown(Event& e) override {

            if (e.keyboard.arrows.left) { setCursorPos(cursor - 1); }
            if (e.keyboard.arrows.right) { setCursorPos(cursor + 1); }

            if (e.keyboard.backspace) { this->deleteAt(cursor, -1); }
            if (e.keyboard.del) { this->deleteAt(cursor, +1); }

            this->refresh(e);
            Box::keyDown(e);
        }

        void textInput(Event& e) override {

            dbg("[Text] Input: %s", e.keyboard.input.c_str());

            if (e.keyboard.input == "\n") { this->insertAt(cursor, "\n"); }
            else if (e.keyboard.input == "\b") { return Box::textInput(e); }
            else { this->insertAt(cursor, e.keyboard.input); }

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

        void resolveStyle(Event& e) override {

            Box::resolveStyle(e);

            strContent = content;
            fontSize = resolved.style.text.size.val;
            if (!fontSize) { fontSize = 12.0f; }
            if (resolved.style.layout.wrap == Wrap::BreakChar) { mode = WrapMode::BreakChar; }

            Core::Resource fontResource = resolved.style.text.font;
            if (!fontResource.data) { fontResource = File("Rev/resources/Fonts/Arial/Arial.ttf"); }

            font = text->fontAtlas->get(fontResource, fontSize, shared->canvas->details.scale);
            text->font = font;
            
            text->fontSize = fontSize;
            text->content = strContent;

            this->measureText();
            maxWidth = 99999999.0f;

            //this->layoutText();

            float minPaddingWidth = resolved.getMinPadding(Axis::Horizontal, Dist::Type::Abs);
            float minPaddingHeight = resolved.getMinPadding(Axis::Vertical, Dist::Type::Abs);

            resolved.minContentWidth = minWidth + minPaddingWidth;
            resolved.minContentHeight = minHeight + minPaddingHeight;
        }

        // Computing text layout
        //--------------------------------------------------

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

        void layoutText() {

            // Layout text
            //--------------------------------------------------

            text->lines.clear();

            size_t idx = 0;
            
            float x = 0;
            float y = font->ascent;

            Font& fontRef = *font;
            Primitive::Text::Line line = { "", idx, idx, { x, y, 0.0f, fontRef.lineHeight } };

            for (char c : strContent) {

                float charWidth = fontRef.glyphs[c].advance;
                float newWidth = line.rect.w + charWidth;

                // Reset line on overflow
                if (idx > 0 && (newWidth > maxWidth || c == '\r' || c == '\n' || c == '_')) {

                    text->lines.push_back(line);
                    line = { "", idx, idx, { x, y, 0.0f, fontRef.lineHeight } };
                }

                line.rect.w += fontRef.glyphs[c].advance;
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

            this->maxWidth = resolved.max.innerWidth;
            this->layoutText();

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

                line.rect.x = std::round(rect.x + resolved.pad.l.val);
                line.rect.y = std::round(runningY + rect.y + resolved.pad.t.val);

                runningY += line.rect.h;
            }

            text->compute();

            // Find cursor pos
            //--------------------------------------------------

            float cursorX, cursorY, cursorH;

            for (Primitive::Text::Line& line : text->lines) {
                if (cursor >= line.start && cursor <= line.end) {

                    // Cursor y is line rect
                    cursorY = line.rect.y;
                    cursorX = line.rect.x;
                    cursorH = line.rect.h;

                    int idx = line.start;

                    // Get x position at line
                    for (char c : line.content) {

                        if (idx >= cursor) { break; }

                        cursorX += font->glyphs[c].advance;
                        idx += 1;
                    }
                }
            }

            // Place line at cursor position
            line->lines = {
                {
                    .points = { { cursorX, cursorY }, { cursorX, cursorY + cursorH } }, 
                    .color = { 1, 0, 0, 1 }, .strokeWidth = 2
                }
            };

            line->compute();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            text->draw();
            if (targetFlags.focus) { line->draw(); }
        }
    };
};