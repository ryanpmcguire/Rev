module;

export module Rev.Graphics.Primitive;

import Rev.Graphics.Canvas;

export namespace Rev {

    struct Primitive {

        // Create
        Primitive() {

        }

        // Destroy
        ~Primitive() {
            
        }

        // Draw
        virtual void draw(Canvas* canvas) {
            
        }
    };
};