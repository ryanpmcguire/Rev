module;

#include <glew/glew.h>

export module Rev.Graphics.FrameBuffer;

import Rev.Graphics.Texture;

export namespace Rev::Graphics {

    struct FrameBuffer {

        void* context = nullptr;    // Unused for OpenGL implementation

        GLuint fbo = 0;
        GLuint depthStencil = 0;
        GLuint colorTex = 0;  // optional: use your Texture abstraction later

        Texture* texture = nullptr;

        struct Params {
            size_t width = 0, height = 0;
            size_t colorChannels = 4;
            size_t stencilChannels = 1;
        };

        Params params;

        // Create
        FrameBuffer(void* context, Params params) {

            this->params = params;

            texture = new Texture(context, { 
                .width = params.width, .height = params.height,
                .channels = params.colorChannels
            });
        }

        // Destroy
    };
};