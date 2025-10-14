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

        virtual void compute() {

        }

        // Draw
        virtual void draw() {
            
        }
    };
};