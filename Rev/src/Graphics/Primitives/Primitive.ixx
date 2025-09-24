module;

export module Rev.Graphics.Primitive;

import Rev.Graphics.Canvas;

export namespace Rev {

    struct Primitive {

        Canvas* canvas;

        // Create
        Primitive(Canvas* canvas) {
            this->canvas = canvas;
        }

        // Destroy
        ~Primitive() {
            
        }

        // Draw
        virtual void draw(Canvas* canvas) {
            
        }
    };
};