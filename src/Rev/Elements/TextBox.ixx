module;

export module Rev.TextBox;

import Rev.Box;

import Rev.OpenGL.Text;

export namespace Rev {

    struct TextBox : public Box {

        inline static Style defaultStyle = {

            .size = { .width = Grow(), .height = 20_px, .minWidth = 200_px },
            .margin = { 4_px, 4_px, 4_px, 4_px },
            .background = { .color = Color(1, 1, 1, 0.1) }
        };

        Text* text = nullptr;

        // Create
        TextBox(Element* parent) : Box(parent, "TextBox") {

            style = defaultStyle;

            text = new Text();
        }

        // Destroy
        ~TextBox() {

            delete text;
        }

        void draw(Event& e) override {

            Box::draw(e);

            text->xPos = rect.x;
            text->yPos = rect.y;

            text->draw();
        }
    };
};