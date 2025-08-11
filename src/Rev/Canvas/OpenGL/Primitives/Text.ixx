module;

#include <cmath>
#include <vector>
#include <string>
#include <glew/glew.h>

export module Rev.OpenGL.Text;

import Rev.OpenGL.Primitive;
import Rev.OpenGL.UniformBuffer;
import Rev.OpenGL.VertexBuffer;
import Rev.OpenGL.Pipeline;
import Rev.OpenGL.Shader;
import Resources.Shaders.OpenGL.Text.Text_vert;
import Resources.Shaders.OpenGL.Text.Text_frag;

import Rev.Font;
import Resources.Fonts.Arial.Arial_ttf;

export namespace Rev {

    export struct Text : public Primitive {

        // Fonts
        //--------------------------------------------------

        // Shared resources
        //--------------------------------------------------

        struct Shared {

            size_t refCount = 0;

            Shader* vert = nullptr;
            Shader* frag = nullptr;
            Pipeline* pipeline = nullptr;

            void create() {

                refCount++;

                // Create resources
                vert = new Shader(Text_vert, GL_VERTEX_SHADER);
                frag = new Shader(Text_frag, GL_FRAGMENT_SHADER);
                pipeline = new Pipeline(*(vert), *(frag));
            }

            void destroy() {

                // Subtract refcount, return if remaining
                if (refCount--) { return; }

                // Delete resources
                delete pipeline;
                delete vert;
                delete frag;
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
        Text() {

            shared.create();

            font = new Font();
            vertices = new VertexBuffer(100, sizeof(CharVertex), 1);
            databuff = new UniformBuffer(sizeof(Data));

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
                font = new Font(Arial_ttf, fontSize);
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
                font = new Font(Arial_ttf, fontSize);
            }

            // Prepare vertices
            //--------------------------------------------------

            CharVertex* verts = (CharVertex*)vertices->data;
            size_t max = vertices->num; // total available vertex slots
            size_t count = 0;

            data->pos = { xPos, yPos };

            float x = xPos;
            float y = yPos + font->ascent;

            char prev = 0;

            for (Line& line : lines) {

                x = line.x; y = line.y;

                for (char c : line.dbg) {

                    if (c < 32 || c >= 128) continue;
    
                    // Ensure space for 6 vertices per character
                    if (count + 6 > max) break;

                    float index = float(c);

                    verts[count++] = { x, y, index, index };

                    x += font->glyphs[c].advance;
                }
            }

            vertexCount = count;
        }

        // Draw vertices
        void draw() override {

            if (true) {
                this->compute();
            }
        
            shared.pipeline->bind();
            font->texture->bind(0);  // bind to GL_TEXTURE0
            font->glyphData->bind(2);

            vertices->bind();
            databuff->bind(1);

            glDrawArraysInstanced(
                GL_TRIANGLES,
                0,          // start vertex
                6,          // 6 vertices per quad
                vertexCount   // one instance per glyph
            );
        }
    };
};