module;

#include <cmath>
#include <vector>
#include <string>
#include <glew/glew.h>

export module Rev.OpenGL.Text;

import Rev.OpenGL.Primitive;
import Rev.OpenGL.UniformBuffer;
import Rev.OpenGL.VertexBuffer;
import Rev.OpenGL.Texture;
import Rev.OpenGL.Pipeline;
import Rev.OpenGL.Shader;
import Resources.Shaders.OpenGL.Text.Text_vert;
import Resources.Shaders.OpenGL.Text.Text_frag;

import Rev.Font;

export namespace Rev {

    export struct Text : public Primitive {

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
        
        struct CharVertex {
            float x, y;     // Screen coords
            float u, v;     // Texture coords
        };

        // Instance-specific data
        struct Data {

            struct Color { float r, g, b, a; };

            Color color;
        };

        UniformBuffer* databuff = nullptr;
        Texture* texture = nullptr;

        VertexBuffer* vertices = nullptr;
        size_t vertexCount = 0;

        Font* font = nullptr;
        Data* data = nullptr;

        std::string content = "This Is A Test Sentence";

        struct Line {
            std::string dbg;    // Debug
            size_t start, end;  // Start and end of line
            float x, y;         // x, y position of line
            float w, h;
        };

        std::vector<Line> lines;

        float xPos = 0;
        float yPos = 0;

        // Create
        Text() {

            shared.create();

            font = new Font();
            texture = new Texture(font->bitmap.data, font->bitmap.width, font->bitmap.height, 1);
            vertices = new VertexBuffer(250, sizeof(CharVertex));
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

        void layout(float maxWidth) {

            lines.clear();

            size_t idx = 0;
            float pos = 0;
            
            float x = xPos;
            float y = yPos + font->ascentPx;

            Font& fontRef = *font;
            Line line = { "", idx, idx, x, y, 0.0f, 0.0f };

            for (char c : content) {

                float charWidth = fontRef.glyphs[c].advance;
                float newWidth = line.w + charWidth;

                // Reset line on overflow
                if (idx > 0 && newWidth > maxWidth) {

                    lines.push_back(line);

                    y += fontRef.lineHeightPx;

                    line = { "", idx, idx, xPos, y, charWidth, 0 };
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
        }

        // Compute vertices
        void compute() {

            CharVertex* verts = (CharVertex*)vertices->data;
            size_t max = vertices->num; // total available vertex slots
            size_t count = 0;

            float x = xPos;
            float y = yPos + font->ascentPx;

            char prev = 0;

            for (Line& line : lines) {

                x = line.x; y = line.y;

                for (char c : line.dbg) {

                    if (c < 32 || c >= 128) continue;
    
                    // Ensure space for 6 vertices per character
                    if (count + 6 > max) break;
    
                    Font::Quad q = font->getQuad(c, x, y, prev);
                    prev = c;
    
                    // Triangle 1
                    verts[count++] = { q.x0, q.y0, q.s0, q.t0 };
                    verts[count++] = { q.x1, q.y0, q.s1, q.t0 };
                    verts[count++] = { q.x1, q.y1, q.s1, q.t1 };
    
                    // Triangle 2
                    verts[count++] = { q.x0, q.y0, q.s0, q.t0 };
                    verts[count++] = { q.x1, q.y1, q.s1, q.t1 };
                    verts[count++] = { q.x0, q.y1, q.s0, q.t1 };
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
            texture->bind(0);  // bind to GL_TEXTURE0
            vertices->bind();
            databuff->bind(1);

            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCount));
        }
    };
};