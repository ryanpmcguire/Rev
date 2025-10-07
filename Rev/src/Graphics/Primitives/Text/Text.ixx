module;

#include <cmath>
#include <vector>
#include <string>

export module Rev.Graphics.Text;

import Rev.Graphics.Canvas;
import Rev.Graphics.Primitive;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

import Rev.Font;
import Resources.Fonts.Arial.Arial_ttf;

// Shader resources
import Resources.Shaders.OpenGL.Text.Text_vert;
import Resources.Shaders.OpenGL.Text.Text_frag;
import Resources.Shaders.Metal.Text.Text_metal;

export namespace Rev {

    struct Text : public Primitive {

        // Fonts
        //--------------------------------------------------

        // Shared resources
        //--------------------------------------------------

        struct Shared {

            size_t refCount = 0;

            Shader* vert = nullptr;
            Shader* frag = nullptr;
            Pipeline* pipeline = nullptr;

            Shared() {
                
            }

            void create(Canvas* canvas) {

                refCount++;

                if (refCount > 1) { return; }

                pipeline = new Pipeline(canvas->context, {
                    
                    .openGlVert = Text_vert,
                    .openGlFrag = Text_frag,

                    .metalUniversal = Text_metal
                }, 4);
            }

            void destroy() {

                // Subtract refcount, return if remaining
                if (refCount--) { return; }

                // Delete resources
                delete pipeline;
            }
        };

        inline static Shared shared;

        // Own resources
        //--------------------------------------------------

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

        UniformBuffer* databuff = nullptr;

        VertexBuffer* vertices = nullptr;
        size_t vertexCount = 0;

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

            shared.create(canvas);

            this->content = content;

            font = new Font(canvas);
            vertices = new VertexBuffer(canvas->context, 100, sizeof(CharVertex), 1);
            databuff = new UniformBuffer(canvas->context, sizeof(Data));

            data = static_cast<Data*>(databuff->data);
            *data = {
                .color = { 1, 1, 1, 1 }
            };
        }

        // Destroy
        ~Text() {

            shared.destroy();

            delete vertices;
            
            if (font) { delete font; }
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
            if (font->size != fontSize) {
                delete font;
                font = new Font(canvas, Arial_ttf, fontSize);
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
        void compute() {

            // Ensure font size matches
            if (font->size != fontSize) {
                delete font;
                font = new Font(canvas, Arial_ttf, fontSize);
            }

            // Prepare vertices
            //--------------------------------------------------

            CharVertex* verts = (CharVertex*)vertices->data;
            size_t max = vertices->num; // total available vertex slots
            size_t count = 0;

            data->pos = { xPos, yPos };

            float x = xPos;
            float y = yPos + font->ascent;

            for (Line& line : lines) {

                char prev = 0;
                x = line.x; y = line.y;

                for (char c : line.dbg) {

                    // Continue / break conditions
                    if (c < 32 || c >= 128) { continue; }
                    if (count + 6 > max) { break; }

                    Font::Glyph& glyph = font->glyphs[c];
                    Font::Glyph& prevGlyph = font->glyphs[prev];

                    float index = float(c);
                    x += prevGlyph.advance + glyph.kerning[prevGlyph.index];
                    verts[count] = {x, y, index, index };

                    count += 1;
                    prev = c;
                }
            }

            vertexCount = count;
        }

        // Draw vertices
        void draw(Canvas* canvas) override {

            if (true) {
                this->compute();
            }
        
            shared.pipeline->bind(canvas->context);
            font->texture->bind(canvas->context, 0);
            font->glyphData->bind(canvas->context, 2);

            vertices->bind(canvas->context);
            databuff->bind(canvas->context, 1);

            canvas->drawArraysInstanced(Pipeline::Topology::TriangleList, 0, 6, vertexCount);
        }
    };
};