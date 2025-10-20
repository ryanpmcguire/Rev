module;

export module Rev.Primitive;

import Rev.Graphics.Canvas;

export namespace Rev::Primitive {

    using namespace Rev::Graphics;

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