module;

export module Rev.TextBox;

import Rev.Box;

import Rev.OpenGL.Text;

export namespace Rev {

    struct TextBox : public Box {

        inline static Style defaultStyle = {
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .background = { .color = Color(1, 1, 1, 0.1) }
        };

        Text* text = nullptr;

        // Create
        TextBox(Element* parent) : Box(parent, "TextBox") {

            style = defaultStyle;
            measure = true;

            text = new Text();
        }

        // Destroy
        ~TextBox() {

            delete text;
        }

        void computeStyle(Event& e) override {
            
            Text::MinMax minMax = text->measure();

            style.size = {
                .width = Grow(), .height = 100_px,
                .minWidth = Px(minMax.minWidth), .maxWidth = Px(minMax.maxWidth),
                .minHeight = Px(minMax.minHeight)
            };

            Box::computeStyle(e);
        }

        // Special function for telling layout engine what our height is
        void measureDims() override {

            text->layout(res.size.w.val);

            res.size.h.min = text->lines.size() * text->font->lineHeightPx;
        }

        void draw(Event& e) override {

            Box::draw(e);

            text->xPos = rect.x;
            text->yPos = rect.y;

            text->layout(rect.w);

            text->draw();
        }
    };
};