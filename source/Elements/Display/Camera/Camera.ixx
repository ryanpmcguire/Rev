module;

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

export module Rev.Element.Camera;

import Rev.Appearance;
import Rev.Element.Box;
import Rev.Element.Event;
import Rev.Media.Camera;
import Rev.Graphics.CameraTexture;
import Rev.Primitive.Video;

export namespace Rev::Element {

    namespace CameraStyles {
        Style Default = {
            .overflow = Overflow::Hide,
            .size = { 320_px, 240_px },
            .background = { .color = rgba(0, 0, 0, 1) }
        };
    }

    struct Camera : public Box {

        enum class Fit { Cover, Contain, Stretch };

        struct Params {
            size_t device = 0;
            Fit fit = Fit::Cover;
            bool autoStart = true;
            Primitives::Video::Settings display;
        };

        explicit Camera(Element* parent, StyleList styles = {})
            : Camera(parent, Params{}, styles) {}

        Camera(Element* parent, Params params, StyleList styles = {})
            : Box(parent, styles, "Camera"), params(params) {

            this->styles.prepend(&CameraStyles::Default);
            video = new Primitives::Video(shared->canvas);
            textures = new Graphics::CameraTexture(shared->canvas->context);
            textureError = textures->error();
            video->settings = params.display;
            media.onFrame(this, [this](Media::Camera::Frame::Event& event, Media::Camera::Frame* received) {
                if (!textures->upload(received->pixels, received->size.width, received->size.height, received->format)) {
                    {
                        std::scoped_lock lock(statusMutex);
                        textureError = textures->error();
                    }
                    if (shared && shared->canvas && shared->canvas->window) {
                        shared->canvas->window->requestFrameFromWorker();
                    }
                    return;
                }
                {
                    std::scoped_lock lock(statusMutex);
                    textureError.clear();
                }
                std::scoped_lock lock(frameMutex);
                frame = received;
                frameRevision = event.revision;
                if (shared && shared->canvas && shared->canvas->window) {
                    shared->canvas->window->requestFrameFromWorker();
                }
            });
            if (params.autoStart) { start(); }
        }

        ~Camera() {
            media.close();
            media.unsubscribeFrames(this);
            delete video;
            delete textures;
        }

        static std::vector<Media::Camera::DeviceInfo> devices() {
            return Media::Camera::devices();
        }

        bool start() {
            bool opened = media.open(params.device);
            if (!opened) { return false; }
            return true;
        }

        void stop() {
            media.close();
        }

        bool refreshControls() { return media.refreshControls(); }
        void refreshFrame() {
            std::scoped_lock lock(frameMutex);
            uploadedRevision = 0;
        }
        void setFit(Fit fit) {
            params.fit = fit;
            if (shared && shared->event) { refresh(*shared->event); }
        }

        void computePrimitives(Event& event) override {

            Box::computePrimitives(event);

            size_t frameWidth = 0;
            size_t frameHeight = 0;
            {
                std::scoped_lock lock(frameMutex);
                if (frame) {
                    frameWidth = frame->size.width;
                    frameHeight = frame->size.height;
                    if (frameRevision != uploadedRevision) {
                        Core::PixelFormat format;
                        Graphics::Texture* texture = textures->latest(format);
                        if (!texture) { return; }
                        video->setTexture(texture, format);
                        textures->display(texture);
                        uploadedRevision = frameRevision;
                    }
                }
            }

            auto& data = *video->data;
            data.rect = rect.rounded();
            data.uv = { 0, 0, 1, 1 };
            data.opacity = resolved.opacity;

            if (frameWidth == 0 || frameHeight == 0 || rect.w <= 0 || rect.h <= 0) { return; }

            const float sourceAspect = static_cast<float>(frameWidth) / static_cast<float>(frameHeight);
            const float targetAspect = rect.w / rect.h;

            if (params.fit == Fit::Contain) {
                if (sourceAspect > targetAspect) {
                    data.rect.h = rect.w / sourceAspect;
                    data.rect.y = rect.y + (rect.h - data.rect.h) * 0.5f;
                }
                else {
                    data.rect.w = rect.h * sourceAspect;
                    data.rect.x = rect.x + (rect.w - data.rect.w) * 0.5f;
                }
            }
            else if (params.fit == Fit::Cover) {
                if (sourceAspect > targetAspect) {
                    data.uv.w = targetAspect / sourceAspect;
                    data.uv.x = (1.0f - data.uv.w) * 0.5f;
                }
                else {
                    data.uv.h = sourceAspect / targetAspect;
                    data.uv.y = (1.0f - data.uv.h) * 0.5f;
                }
            }
        }

        void draw(Event& event) override {
            Box::draw(event);
            video->draw();
        }

        Params params;
        // Media owns capture and publication. This element only holds the currently
        // published image buffer long enough to upload it to its video primitive.
        Media::Camera media;
        std::mutex frameMutex;
        std::mutex statusMutex;
        Media::Camera::Frame* frame = nullptr;
        uint64_t frameRevision = 0;
        uint64_t uploadedRevision = 0;
        Primitives::Video* video = nullptr;
        Graphics::CameraTexture* textures = nullptr;
        std::string textureError;
    };
}
