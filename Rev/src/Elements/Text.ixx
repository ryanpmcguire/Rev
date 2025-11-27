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
        Observable<bool> editable;

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
            selectEnd = selectAnchor = cursor;
        }

        void replaceAt(int posStart, int posEnd, const std::string& toInsert) {
    
            // Canonical ordering
            int left = std::min(posStart, posEnd);
            int right = std::max(posStart, posEnd);
        
            std::string str = content;
            int len = (int)str.size();
        
            // Clamp boundaries
            left = std::clamp(left, 0, len);
            right = std::clamp(right, 0, len);
        
            // Erase the range
            str.erase(left, right - left);
        
            // Insert new text
            str.insert(left, toInsert);
        
            // Update observable string
            content = str;
        
            // Update cursor position: at end of inserted text
            cursor = left + (int)toInsert.size();
        
            // Clear selection
            selectAnchor = cursor;
            selectEnd = cursor;
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
        int selectAnchor = 0, selectEnd = 0;

        void setCursorPos(int newCursor) {
            cursor = std::clamp(newCursor, 0, (int)strContent.size() + 1);
        }

        int getCursorPos(Core::Pos pos) {

            // Find intersecting line
            for (Primitive::Text::Line& line : text->lines) {

                if (line.rect.y > pos.y) { continue; }
                if (line.rect.y + line.rect.h < pos.y) { continue; }

                int idx = line.start;
                float left = line.rect.x;

                // Get char at x position
                for (char c : line.content) {

                    float right = left + font->glyphs[c].advance;

                    // Find intersecting glyph
                    if (pos.x >= left && pos.x <= right) {

                        float distLeft = pos.x - left;
                        float distRight = right - pos.x;

                        // Return left or right depending on which is closest
                        return distLeft < distRight ? idx : idx + 1;
                    }

                    left = right;
                    idx += 1;
                }

                // Return end of line if we did not reach a char
                return line.end + 1;
            }

            // Return start of content
            return 0;
        }

        void mouseDown(Event& e) override {

            // Interactions require editable
            if (!editable) { return Box::mouseDown(e); }

            // Shift + click sets end of selection, normal click sets start/cursor
            if (e.keyboard.shift) { cursor = selectEnd = this->getCursorPos(e.mouse.pos); }
            else { cursor = selectAnchor = selectEnd = this->getCursorPos(e.mouse.pos); }
            
            this->refresh(e);
            Box::mouseDown(e);
        }

        void mouseDrag(Event& e) override {

            if (!editable) { return Box::mouseDrag(e); }

            cursor = selectEnd = this->getCursorPos(e.mouse.pos);

            this->refresh(e);
            Box::mouseDrag(e);
        }

        void keyDown(Event& e) override {

            // Interactions require editable
            if (!editable) { return Box::keyDown(e); }

            // Avoid ugly long names
            bool left = e.keyboard.arrows.left; bool right = e.keyboard.arrows.right;
            bool up = e.keyboard.arrows.up; bool down = e.keyboard.arrows.down;

            if (left || right || up || down) {

                if (left) { cursor -= 1; }
                else if (right) { cursor += 1; }

                if (e.keyboard.shift) { selectEnd = cursor; }
                else { selectAnchor = selectEnd = cursor; }
            }

            // Delete back or forward
            if (selectEnd == selectAnchor) {
                if (e.keyboard.backspace) { this->deleteAt(cursor, -1); }
                if (e.keyboard.del) { this->deleteAt(cursor, +1); }    
            }

            // Replace in region
            else {
                if (e.keyboard.backspace) { this->replaceAt(selectAnchor, selectEnd, ""); }
                if (e.keyboard.del) { this->replaceAt(selectAnchor, selectEnd, ""); }
            }

            this->refresh(e);
            Box::keyDown(e);
        }

        void textInput(Event& e) override {

            // Interactions require editable
            if (!editable) { return Box::textInput(e); }

            dbg("[Text] Input: %s", e.keyboard.input.c_str());

            if (e.keyboard.input == "\b") { return Box::textInput(e); }

            if (selectEnd == selectAnchor) { this->insertAt(cursor, e.keyboard.input); }
            else { this->replaceAt(selectAnchor, selectEnd, e.keyboard.input); }

            this->refresh(e);
            Box::textInput(e);
        }

        // Compute style/primitive/etc
        //--------------------------------------------------

        void computeStyle(Event& e) override {

            if (content.changed() || editable.changed()) {
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

            Core::Resource fontResource = resolved.style.text.font;
            if (!fontResource.data) { fontResource = File("Rev/resources/Fonts/Arial/Arial.ttf"); }

            font = text->fontAtlas->get(fontResource, fontSize, shared->canvas->details.scale);
            
            text->font = font;
            text->fontSize = fontSize;
            text->content = strContent;

            this->measureText();
            maxWidth = 99999999.0f;

            float minPaddingWidth = resolved.getMinPadding(Axis::Horizontal, Dist::Type::Abs);
            float minPaddingHeight = resolved.getMinPadding(Axis::Vertical, Dist::Type::Abs);

            resolved.minContentWidth = minWidth + minPaddingWidth;
            resolved.minContentHeight = minHeight + minPaddingHeight;
            
            // Cursor
            if (editable) { resolved.style.cursor = Cursor::Caret; }
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

                bool wordBreak = (c == ' ');
                bool lineBreak = (c == '\n' || c == '\r');

                // End of word (lines also count)
                if (wordBreak || lineBreak) {
                    word.min = std::min(word.min, word.current);
                    word.max = std::max(word.max, word.current);
                    word.current = 0;
                }

                // End of line
                if (lineBreak) {
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
            switch (resolved.style.text.wrap) {

                case (Wrap::BreakChar): {
                    minWidth = letter.max;
                    maxWidth = line.max;
                    break;
                }

                case (Wrap::BreakWord): {
                    minWidth = word.max;
                    maxWidth = line.max;
                    break;
                }

                default: {
                    minWidth = line.min;
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

            int last = strContent.size() - 1;

            std::string chunk = "";
            float chunkWidth = 0.0f;
            size_t chunkStart = 0;

            for (char c : strContent) {

                bool isWordEnd;

                switch (resolved.style.text.wrap) {
                    case (Wrap::BreakChar): { isWordEnd = true; break; }
                    case (Wrap::BreakWord): { isWordEnd = (c == ' '); break; }
                    case (Wrap::BreakLine): { isWordEnd = (c == '.'); break; }
                    default: { isWordEnd = false; }
                }

                bool isNewLine = (c == '\n' || c == '\r');

                bool checkWrap = isWordEnd || idx == last;
                bool forceWrap = isNewLine;

                // If we are being forced to make a new line
                if (forceWrap) {

                    // If current chunk would fit, add it first
                    if (line.rect.w + chunkWidth <= maxWidth || chunkStart == 0) {

                        // Add chunk to line
                        line.rect.w += chunkWidth;
                        line.content += chunk;
                        line.end = idx - 1;

                        // Reset chunk
                        chunk = "";
                        chunkWidth = 0.0f;
                        chunkStart = idx;

                        text->lines.push_back(line);
                        line = { "", chunkStart, idx, { x, y, 0.0f, fontRef.lineHeight } };
                    }

                    // If current chunk would not fit, add it last
                    else {

                        text->lines.push_back(line);
                        line = { "", chunkStart, idx, { x, y, 0.0f, fontRef.lineHeight } };

                        // Add chunk to line
                        line.rect.w += chunkWidth;
                        line.content += chunk;
                        line.end = idx - 1;

                        // Reset chunk
                        chunk = "";
                        chunkWidth = 0.0f;
                        chunkStart = idx;
                    }

                    chunk += c;
                }

                // If we should check to see if this chunk fits
                else if (checkWrap) {

                    // Add char to chunk
                    chunk += c;
                    chunkWidth += fontRef.glyphs[c].advance;

                    // If current chunk would not fit, start a new line
                    if (line.rect.w + chunkWidth > maxWidth && chunkStart != 0) {
                        text->lines.push_back(line);
                        line = { "", chunkStart, idx, { x, y, 0.0f, fontRef.lineHeight } };
                    }

                    // Add chunk to new line
                    line.rect.w += chunkWidth;
                    line.content += chunk;
                    line.end = idx;

                    // Reset chunk
                    chunk = "";
                    chunkWidth = 0.0f;
                    chunkStart = idx + 1;
                }

                else {

                    // Add char to chunk
                    chunk += c;
                    chunkWidth += fontRef.glyphs[c].advance;
                }

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

            // Skip cursor/region calculations if not editable
            if (!editable) { return Box::computePrimitives(e); }

            // Place cursor at cursor pos
            //--------------------------------------------------

            line->lines.clear();

            for (Primitive::Text::Line& line : text->lines) {

                if (line.end + 1 < cursor) { continue; }
                if (line.start > cursor) { continue; }
            
                int idx = line.start;
                float cursor_x = line.rect.x;

                // Get x position at line56
                for (char c : line.content) {

                    if (idx == cursor) {
                        break;
                    }

                    cursor_x += font->glyphs[c].advance;
                    idx += 1;
                }
                
                // Place line at cursor position
                this->line->lines.push_back({
                    .points = { { cursor_x, line.rect.y }, { cursor_x, line.rect.y + line.rect.h } }, 
                    .color = { 0, 0, 0, 1 }, .strokeWidth = 1.0f, .smoothing = 0.0f
                });

                break;
            }

            // Highlight selected region(s)
            //--------------------------------------------------

            if (selectEnd != selectAnchor) {

                int leftMost = std::min(selectAnchor, selectEnd);
                int rightMost = std::max(selectAnchor, selectEnd);

                // Add select lines
                for (Primitive::Text::Line& line : text->lines) {

                    // Skip if this line would not contain what we're looking for
                    if (line.end + 1 < leftMost) { continue; }
                    if (line.start + 1 > rightMost) { continue; }

                    int idx = line.start;
                    float left_x = line.rect.x;
                    float right_x = line.rect.x;

                    for (char c : line.content) {

                        if (idx < leftMost) { left_x += font->glyphs[c].advance; }
                        if (idx < rightMost) { right_x += font->glyphs[c].advance; }

                        idx += 1;
                    }

                    if (right_x - left_x < 0.1) { right_x = left_x + 5.0f; }

                    float line_y = line.rect.y + 0.5f * line.rect.h;
                    float line_width = line.rect.h;

                    // Place line at cursor position
                    this->line->lines.push_back({
                        .points = { { left_x, line_y }, { right_x, line_y } }, 
                        .color = { 0, 0, 1, 0.2 }, .strokeWidth = line_width, .smoothing = 0.0f
                    });
                }
            }

            line->compute();

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            // Always draw text
            text->draw();

            // Draw line only if editable
            if (targetFlags.focus && editable) { line->draw(); }
        }
    };
};