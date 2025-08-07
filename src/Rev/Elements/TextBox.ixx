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

            text->layout(99999999.0f);

            style->size = {
                .width = Px(text->dims.width), .height = Px(text->dims.height)
            };

            Box::computeStyle(e);
        }

        void draw(Event& e) override {

            Box::draw(e);

            text->xPos = rect.x;
            text->yPos = rect.y;

            text->draw();
        }
    };
};