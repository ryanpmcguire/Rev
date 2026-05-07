module;

#include <cmath>
#include <vector>
#include <string>

#include <managed.hpp>

export module Rev.Primitive.Text;

import Rev.Primitive;
import Rev.Core.Shared;
import Rev.Core.Resource;
import Rev.Core.Font;
import Rev.Core.FontAtlas;
import Rev.Core.Pos;
import Rev.Core.Rect;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

export namespace Rev::Primitives {

    Core::Resource Arial_ttf = File("Rev/resources/Fonts/Arial/Arial.ttf");

    struct Text : public Primitive {

        // Shared
        //--------------------------------------------------
        
        inline static Shared shared;
        inline static Pipeline* pipeline;
        inline static Core::FontAtlas* fontAtlas;

        void createShared() {

            fontAtlas = new Core::FontAtlas(canvas);
            
            pipeline = new Pipeline(canvas->context, {

                .attribs = { 4 },
                
                .openGlVert = File("./Shaders/Text.vert"),
                .openGlFrag = File("./Shaders/Text.frag"),

                .metalUniversal = File("./Shaders/Text.metal")
            });
        }

        void destroyShared() {

            delete fontAtlas;
            delete pipeline;
        }

        // Instance
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

        Font* font = nullptr;
        Data* data = nullptr;

        std::string content = "Hello World";
        float fontSize = 12.0f;

        struct Line {

            std::string content;            // Line content
            size_t start, end;
            Core::Rect rect;
        };

        std::vector<Line> lines;

        float xPos = 0;
        float yPos = 0;
        size_t glyphCount = 0;

        // Create
        Text(Canvas* canvas) : Primitive(canvas) {

            // Create shared pipeline
            shared.create([this]() { this->createShared(); });

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
            shared.destroy([this]() { this->destroyShared(); });

            delete vertices;
            delete databuff;
        }

        // Compute vertices
        void compute() override {

            // Ensure font size matches
            font = fontAtlas->get(Arial_ttf, fontSize, canvas->details.scale);

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
                x = line.rect.x; y = line.rect.y + font->ascent;

                for (char c : line.content) {

                    // Continue / break conditions
                    if (c < 32 || c >= 128) { continue; }

                    Font::Glyph& glyph = font->glyphs[c];
                    Font::Glyph& prevGlyph = font->glyphs[prev];

                    float index = float(c);
                    x += prevGlyph.advance + glyph.kerning[prevGlyph.index];
                    verts[count] = { x, y, index, index };

                    count += 1;
                    prev = c;
                }
            }

            this->glyphCount = count;
        }

        // Draw vertices
        void draw() override {
        
            pipeline->bind();
            font->texture->bind(0);
            font->glyphData->bind(2);

            vertices->bind();
            databuff->bind(1);

            canvas->drawArraysInstanced(Pipeline::Topology::TriangleList, 0, 6, glyphCount);
        }
    };
};