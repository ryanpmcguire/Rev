module;

#include <cmath>
#include <vector>
#include <string>

export module Rev.Primitive.Text;

import Rev.Primitive;
import Rev.Core.Shared;
import Rev.Core.Font;
import Rev.Core.Pos;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

// Resources
import Resources.Fonts.Arial.Arial_ttf;
import Resources.Shaders.Text.Text_vert;
import Resources.Shaders.Text.Text_frag;
import Resources.Shaders.Text.Text_metal;

export namespace Rev::Primitive {

    struct Text : public Primitive {

        // Position and texture coords
        struct CharVertex {
            float x, y;
            float u, v;
        };

        // Instance-specific data
        struct Data {

            struct Pos { float x, y; };
            struct Color { float r, g, b, a; };

            Color color;
            Pos pos;
        };

        inline static Shared shared;
        inline static Pipeline* pipeline;

        UniformBuffer* databuff = nullptr;
        VertexBuffer* vertices = nullptr;

        Font* font = nullptr;
        Data* data = nullptr;

        std::string content = "Hello World";
        float fontSize = 12.0f;

        struct Line {
            std::string dbg;    // Debug
            size_t start, end;  // Start and end of line
            float x, y;         // x, y position of line
            float w, h;
        };

        struct Dims {
            float width, height;
        };

        std::vector<Line> lines;
        Dims dims;

        float xPos = 0;
        float yPos = 0;

        // Create
        Text(Canvas* canvas) : Primitive(canvas) {

            // Create shared pipeline
            shared.create([canvas]() {
                
                pipeline = new Pipeline(canvas->context, {

                    .attribs = { 4 },
                    
                    .openGlVert = Text_vert,
                    .openGlFrag = Text_frag,

                    .metalUniversal = Text_metal
                });
            });

            this->content = content;

            font = new Font(canvas);
            vertices = new VertexBuffer(canvas->context, { .divisor = 1, .attribs = { 4 } });
            databuff = new UniformBuffer(canvas->context, sizeof(Data));

            data = static_cast<Data*>(databuff->data);
            *data = {
                .color = { 1, 1, 1, 1 }
            };
        }

        // Destroy
        ~Text() {

            // Destroy shared pipeline
            shared.destroy([]() {
                delete pipeline;
            });

            delete vertices;
            delete databuff;
            delete font;
        }

        // Measure / layout
        //--------------------------------------------------

        enum WrapMode {
            None,
            BreakChar,
            BreakWord
        };

        // Struct for measuring hypothetical dimensions
        struct MinMax {
            float minWidth = 0; float maxWidth = 0;
            float maxHeight = 0; float minHeight = 0;
        };
        
        WrapMode mode = WrapMode::BreakChar;
        MinMax minMax;
        
        MinMax measure() {

            // Ensure font size matches
            if (font->size != fontSize || font->scale != canvas->details.scale) {
                delete font;
                font = new Font(canvas, Arial_ttf, fontSize, canvas->details.scale);
            }

            float xl = 0, yl = 0;
            float xr = 0, yr = 0;

            struct Tracked {
                float current = 0;
                float max = 0;
                float min = 99999999;
            };

            Font& fontRef = *font;
            Tracked letter, word, line;
        
            // Iterate through each character in the content
            for (char c : content) {

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

            switch (mode) {

                case (WrapMode::None): {
                    minMax.minWidth = line.min;
                    minMax.maxWidth = line.max;
                    break;
                }

                case (WrapMode::BreakChar): {
                    minMax.minWidth = letter.max;
                    minMax.maxWidth = line.max;
                    break;
                }

                case (WrapMode::BreakWord): {
                    minMax.minWidth = word.max;
                    minMax.maxWidth = line.max;
                    break;
                }
            }
        
            return minMax;
        }

        Dims layout(float maxWidth) {

            // Ensure font size matches
            if (font->size != fontSize || font->scale != canvas->details.scale) {
                delete font;
                font = new Font(canvas, Arial_ttf, fontSize, canvas->details.scale);
            }

            // Layout text
            //--------------------------------------------------

            lines.clear();

            size_t idx = 0;
            float pos = 0;
            
            float x = 0;
            float y = font->ascent;

            Font& fontRef = *font;
            Line line = { "", idx, idx, x, y, 0.0f, fontRef.lineHeight };

            for (char c : content) {

                float charWidth = fontRef.glyphs[c].advance;
                float newWidth = line.w + charWidth;

                // Reset line on overflow
                if (idx > 0 && (newWidth > maxWidth || c == '\r')) {

                    lines.push_back(line);

                    y += fontRef.lineHeight;

                    line = { "", idx, idx, x, y, charWidth, fontRef.lineHeight };
                }

                // Continue line
                else {
                    line.w += fontRef.glyphs[c].advance;
                }

                line.dbg += c;
                line.end = idx;
                idx += 1;
            }

            lines.push_back(line);

            // Measure dims
            //--------------------------------------------------

            dims = { 0, 0 };

            for (Line& line : lines) {
                dims.height += line.h;
                dims.width = std::max(dims.width, line.w);
            }

            return dims;
        }

        // Compute vertices
        void compute() override {

            // Ensure font size matches
            if (font->size != fontSize || font->scale != canvas->details.scale) {
                delete font;
                font = new Font(canvas, Arial_ttf, fontSize, canvas->details.scale);
            }

            // Prepare vertices
            //--------------------------------------------------

            vertices->resize(content.size());
            CharVertex* verts = (CharVertex*)vertices->data;

            data->pos = { std::round(xPos), std::round(yPos) };

            float x = xPos;
            float y = yPos + font->ascent;
            size_t count = 0;

            for (Line& line : lines) {

                char prev = 0;
                x = line.x; y = line.y;

                for (char c : line.dbg) {

                    // Continue / break conditions
                    if (c < 32 || c >= 128) { continue; }

                    Font::Glyph& glyph = font->glyphs[c];
                    Font::Glyph& prevGlyph = font->glyphs[prev];

                    float index = float(c);
                    x += prevGlyph.advance + glyph.kerning[prevGlyph.index];
                    verts[count] = {x, y, index, index };

                    count += 1;
                    prev = c;
                }
            }
        }

        // Draw vertices
        void draw() override {
        
            pipeline->bind();
            font->texture->bind(0);
            font->glyphData->bind(2);

            vertices->bind();
            databuff->bind(1);

            canvas->drawArraysInstanced(Pipeline::Topology::TriangleList, 0, 6, content.size());
        }
    };
};