module;

#include <cstdint>
#include <functional>
#include <memory>
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
            : Box(parent, styles, "Camera"),
              params(params),
              ownedMedia(std::make_unique<Media::Camera>()),
              media(*ownedMedia) {

            source = &media;
            ownsSource = true;
            attach();

            if (params.autoStart) { start(); }
        }

        Camera(
            Element* parent,
            Media::Camera& attached,
            Params params,
            StyleList styles = {}
        ) : Box(parent, styles, "Camera"),
            params(params),
            ownedMedia(),
            media(attached) {

            source = &attached;
            attach();
        }

        Camera(
            Element* parent,
            Media::Camera& attached,
            StyleList styles = {}
        ) : Camera(parent, attached, Params{}, styles) {}

        void attach() {

            this->styles.prepend(&CameraStyles::Default);
            video = new Primitives::Video(shared->canvas);
            textures = new Graphics::CameraTexture(shared->canvas->context);
            textureError = textures->error();
            video->settings = params.display;
            // Main-thread signal only: note that a newer snapshot exists and ask
            // for a repaint. The upload happens in computePrimitives, on the UI
            // thread that owns the GL context, from Media::Camera::latestFrame().
            source->onFrame(this, [this](Media::Camera::Frame::Event&, const Media::Camera::Frame*) {
                if (shared && shared->canvas && shared->canvas->window) {
                    shared->canvas->window->requestFrameFromWorker();
                }
            });
            source->acquireFrames(this);
        }

        ~Camera() {
            source->releaseFrames(this);
            source->unsubscribeFrames(this);

            if (ownsSource) {
                media.close();
            }

            delete video;
            delete textures;
        }

        static std::vector<Media::Camera::DeviceInfo> devices() {
            return Media::Camera::devices();
        }

        bool start() {
            if (!ownsSource) {
                return source->info.open;
            }

            bool opened = media.open(params.device);
            if (!opened) { return false; }
            return true;
        }

        void stop() {
            if (ownsSource) {
                media.close();
            }
        }

        bool refreshControls() { return source->refreshControls(); }
        void refreshFrame() {
            std::scoped_lock lock(frameMutex);
            uploadedRevision = 0;
            displayedRevision = 0;
        }
        // Display diagnostics, updated each computePrimitives. The texture is
        // allocated at the frame size (CameraTexture::nextSlot), so texture W x H
        // equals frame W x H.
        struct DisplayInfo {
            size_t frameWidth = 0, frameHeight = 0;
            float drawnWidth = 0, drawnHeight = 0;
            float scale = 0;            // drawn px per frame px (logical)
            std::string mediaFormat;    // subtype and fps from Media::Camera
        };
        DisplayInfo displayInfo() {
            std::scoped_lock statusLock(statusMutex);
            return info;
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
                std::shared_ptr<const Media::Camera::Frame> latest = source->latestFrame();
                std::scoped_lock lock(frameMutex);
                if (latest && latest->revision != uploadedRevision) {
                    bool uploaded = textures->upload(latest->pixels,
                        latest->size.width, latest->size.height, latest->format, latest->pitch);
                    {
                        std::scoped_lock statusLock(statusMutex);
                        textureError = uploaded ? std::string() : textures->error();
                    }
                    if (uploaded) {
                        frame = latest;
                        frameRevision = latest->revision;
                    }
                    uploadedRevision = latest->revision;
                }
                if (frame) {
                    frameWidth = frame->size.width;
                    frameHeight = frame->size.height;
                    if (frameRevision != displayedRevision) {
                        Core::PixelFormat format;
                        Graphics::Texture* texture = textures->latest(format);
                        if (!texture) { return; }
                        video->setTexture(texture, format);
                        textures->display(texture);
                        displayedRevision = frameRevision;
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
                // Whole pixels: a session-dependent sub-pixel phase changes which
                // texels are sampled and therefore the apparent sharpness.
                data.rect = data.rect.rounded();
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
            {
                std::scoped_lock statusLock(statusMutex);
                info.frameWidth = frameWidth;
                info.frameHeight = frameHeight;
                info.drawnWidth = data.rect.w;
                info.drawnHeight = data.rect.h;
                info.scale = frameWidth ? data.rect.w / static_cast<float>(frameWidth) : 0.0f;
                info.mediaFormat = source->mediaFormat();
            }
        }

        void draw(Event& event) override {
            Box::draw(event);
            video->draw();
        }

        Params params;
        // Media owns capture and publication. This element only holds the currently
        // published image buffer long enough to upload it to its video primitive.
        std::unique_ptr<Media::Camera> ownedMedia;
        Media::Camera& media;
        Media::Camera* source = nullptr;
        bool ownsSource = false;
        std::mutex frameMutex;
        std::mutex statusMutex;
        std::shared_ptr<const Media::Camera::Frame> frame;
        uint64_t frameRevision = 0;
        uint64_t uploadedRevision = 0;
        uint64_t displayedRevision = 0;
        Primitives::Video* video = nullptr;
        Graphics::CameraTexture* textures = nullptr;
        std::string textureError;
        DisplayInfo info;
    };
}
