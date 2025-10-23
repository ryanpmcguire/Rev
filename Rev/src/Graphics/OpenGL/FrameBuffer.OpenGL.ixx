module;

#include <stdexcept>

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
            glGenFramebuffers(1, &fbo);

            this->resize(params.width, params.height);
        }

        // Destroy
        ~FrameBuffer() {

            if (depthStencil) { glDeleteRenderbuffers(1, &depthStencil); }
            if (fbo) { glDeleteFramebuffers(1, &fbo); }

            delete texture;
        }

        void resize(size_t width, size_t height) {

            // Reject invalid size
            if (!width || !height) {
                throw std::runtime_error("[FrameBuffer] Invalid size");
            }

            // Update params
            params.width = width;
            params.height = height;

            // Delete old attachments
            if (texture) { delete texture; texture = nullptr; }
            if (depthStencil) { glDeleteRenderbuffers(1, &depthStencil); depthStencil = 0; }

            // Create color texture
            texture = new Texture(context, {
                .width = width,
                .height = height,
                .channels = params.colorChannels
            });

            // Create depth and stencil buffers
            glGenRenderbuffers(1, &depthStencil);
            glBindRenderbuffer(GL_RENDERBUFFER, depthStencil);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

            // Attach to framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->id, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthStencil);

            // Check completeness
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                throw std::runtime_error("[FrameBuffer] Framebuffer incomplete on resize");
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void bind() {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        }
    };
};