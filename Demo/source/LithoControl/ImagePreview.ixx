module;
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>

export module LithoControl.ImagePreview;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Primitive.Image;
import Rev.Graphics.Texture;
import LithoControl.ImageDecode;

export namespace LithoControl {
    using namespace Rev;
    using namespace Rev::Element;
    using Text = Rev::Element::Text;

    // -------------------------------------------------------------------------
    // ImagePreview element -- renders a PNG with an optional tile-grid overlay
    // -------------------------------------------------------------------------

    struct ImagePreview : public Box {

        Rev::Primitives::Image*    imgPrimitive = nullptr;
        Rev::Graphics::Texture*    imgTexture   = nullptr;

        int srcW = 0, srcH = 0;     // current texture pixel dimensions
        int tileX = 0, tileY = 0;   // tile grid counts (0 = no grid)

        // Deferred-bake state: decoding happens on the CPU at any time, but the GL
        // texture is (re)created in computePrimitives where the context is current --
        // mirroring Core::Svg::bake(). Creating textures in event handlers (where the
        // GL context may not be current) is why images previously failed to appear.
        std::vector<uint8_t> pendingPixels;
        int  pendingW = 0, pendingH = 0;
        bool needsBake = false;

        // Artwork overlay -- composited on top of the camera feed.
        // stagePixels() dual-writes here; stagePixelsLive() (camera) leaves it alone.
        Rev::Primitives::Image*    overlayPrimitive = nullptr;
        Rev::Graphics::Texture*    overlayTexture   = nullptr;
        std::vector<uint8_t>       overlayPending;
        int  overlayPendingW = 0, overlayPendingH = 0;
        bool overlayNeedsBake = false;
        float overlayOpacity  = 0.0f;   // 0=hidden; set by transparency slider
        bool  overlayActive   = false;  // true while camera is live

        // Frame cycling / merge state for previewing a sliced job
        std::vector<std::string>          framePaths;
        std::vector<std::pair<int,int>>   frameCells;   // (col, row) per frame
        int  gridCols = 0, gridRows = 0;
        int  frameIdx = -1;
        bool merged   = false;

        // Zoom / pan (zoom is relative to actual image pixels, like the Python canvas)
        float zoom    = 1.0f;
        float offsetX = 0.0f, offsetY = 0.0f;
        bool  panning = false;
        float panStartX = 0, panStartY = 0;
        float panStartOffX = 0, panStartOffY = 0;

        // Hint text shown when no image is loaded; info overlay (bottom-left)
        Text* hint = nullptr;
        Text* info = nullptr;

        // Called with raw RGBA pixels whenever artwork (not camera) is staged.
        // Interface uses this to mirror the artwork to the HDMI projector.
        std::function<void(const std::vector<uint8_t>&, int, int)> onArtworkBaked;

        ImagePreview(Element* parent, StyleList styles = {})
            : Box(parent, styles)
        {
            imgPrimitive     = new Rev::Primitives::Image(shared->canvas);
            overlayPrimitive = new Rev::Primitives::Image(shared->canvas);

            // Clip zoomed/panned image to the preview bounds
            this->style->overflow = Overflow::Hide;

            hint = new Text(this, "[ NO ARTWORK LOADED ]");
            hint->style->text.color = rgba(232, 232, 232, 0.2f);
            hint->style->text.size  = 14_px;

            // Info overlay: "zoom 1.0x  (WxHpx)" pinned to bottom-left. The layout
            // engine only honours absolute left/top, so the top is set each frame in
            // computePrimitives from the preview height.
            info = new Text(this, "");
            info->style->layout.position = Position::Absolute;
            info->style->position.left   = 8_px;
            info->style->position.top    = 8_px;
            info->style->text.color = rgba(232, 232, 232, 0.5f);
            info->style->text.size  = 9_px;
            info->style->visibility = Visibility::Hidden;

            // Wheel = zoom about the view; clamped like the Python canvas [0.1, 20].
            // Zoom/pan change plain floats (not styles), so nothing dirties the tree
            // on its own -- call refresh(e) to force a repaint this frame.
            this->onMouseWheel([this](Rev::Element::Event& e) {
                if (srcW <= 0) return;
                float factor = (e.mouse.wheel.y > 0) ? 1.15f : (1.0f / 1.15f);
                zoom = std::clamp(zoom * factor, 0.1f, 20.0f);
                e.propagate = false;
                this->refresh(e);
            });

            // Drag = pan; double-click = reset view
            this->onMouseDown([this](Rev::Element::Event& e) {
                if (srcW <= 0) return;
                if (e.mouse.lb.isDoubleClick()) {
                    zoom = 1.0f; offsetX = offsetY = 0.0f;
                    panning = false;
                    this->refresh(e);
                    return;
                }
                panning = true;
                panStartX = e.mouse.pos.x; panStartY = e.mouse.pos.y;
                panStartOffX = offsetX;    panStartOffY = offsetY;
            });
            this->onMouseMove([this](Rev::Element::Event& e) {
                if (!panning) return;
                offsetX = panStartOffX + (e.mouse.pos.x - panStartX);
                offsetY = panStartOffY + (e.mouse.pos.y - panStartY);
                this->refresh(e);
            });
            this->onMouseUp([this](Rev::Element::Event&) { panning = false; });
        }

        ~ImagePreview() override {
            delete imgPrimitive;
            delete imgTexture;
            delete overlayPrimitive;
            delete overlayTexture;
        }

        // Decode a PNG/BMP/JPEG into a tightly-packed RGBA buffer.
        static bool decodeToRGBA(const std::string& path,
                                 std::vector<uint8_t>& out, int& w, int& h) {
            return LithoControl::decodeToRGBA(path, out, w, h);
        }

        // Queue a decoded buffer for upload on the next render pass.
        void stagePixels(std::vector<uint8_t>&& px, int w, int h) {
            // Dual-write: overlay copy has black keyed out (alpha = pixel brightness)
            // so unexposed areas are transparent over the camera feed.
            overlayPending.resize(px.size());
            for (size_t i = 0; i + 3 < px.size(); i += 4) {
                overlayPending[i+0] = px[i+0];
                overlayPending[i+1] = px[i+1];
                overlayPending[i+2] = px[i+2];
                uint8_t luma = px[i+0] > px[i+1] ? px[i+0] : px[i+1];
                if (px[i+2] > luma) luma = px[i+2];
                overlayPending[i+3] = luma;
            }
            overlayPendingW = w; overlayPendingH = h;
            overlayNeedsBake = true;

            if (onArtworkBaked) onArtworkBaked(px, w, h);  // fire before move
            pendingPixels = std::move(px);
            pendingW = w; pendingH = h;
            needsBake = true;
            // Reset the view to fit-on-first-show (zoom recomputed in computePrimitives)
            zoom = 0.0f;                 // 0 = "auto-fit on next layout"
            offsetX = offsetY = 0.0f;
            if (hint) hint->style->visibility = Visibility::Hidden;
            if (info) info->style->visibility = Visibility::Visible;
        }

        // Live camera feed: upload without resetting zoom/pan (auto-fit on first frame only).
        void stagePixelsLive(const std::vector<uint8_t>& px, int w, int h) {
            pendingPixels = px;
            pendingW = w; pendingH = h;
            needsBake = true;
            if (srcW <= 0) {
                zoom = 0.0f;  // first frame: auto-fit
                offsetX = offsetY = 0.0f;
            }
            if (hint) hint->style->visibility = Visibility::Hidden;
            if (info) info->style->visibility = Visibility::Visible;
        }

        // Load a single image file into the preview (clears any frame cycling state).
        void loadFile(const std::string& path) {
            std::vector<uint8_t> px; int w = 0, h = 0;
            if (!decodeToRGBA(path, px, w, h)) return;
            framePaths.clear(); frameCells.clear();
            frameIdx = -1; merged = false;
            stagePixels(std::move(px), w, h);
        }

        void setGrid(int cols, int rows) { tileX = cols; tileY = rows; }

        // Configure frame cycling for a sliced job and show the first frame.
        void setFrames(std::vector<std::string> paths,
                       std::vector<std::pair<int,int>> cells,
                       int gCols, int gRows) {
            framePaths = std::move(paths);
            frameCells = std::move(cells);
            gridCols = gCols; gridRows = gRows;
            merged = false;
            frameIdx = framePaths.empty() ? -1 : 0;
            if (frameIdx >= 0) showFrame(frameIdx);
        }

        void showFrame(int i) {
            if (i < 0 || i >= (int)framePaths.size()) return;
            frameIdx = i; merged = false;
            std::vector<uint8_t> px; int w = 0, h = 0;
            if (!decodeToRGBA(framePaths[i], px, w, h)) return;
            setGrid(0, 0);                     // single frame = single tile, no grid
            stagePixels(std::move(px), w, h);
        }

        void cycleFrame(int delta) {
            if (framePaths.empty()) return;
            int n = (int)framePaths.size();
            int i = (frameIdx < 0 ? 0 : (frameIdx + delta) % n);
            if (i < 0) i += n;
            showFrame(i);
        }

        // Stitch all frames back into the full artwork using their grid cells.
        void mergeFrames() {
            if (framePaths.empty() || gridCols <= 0 || gridRows <= 0) return;

            // Probe first frame for the per-tile pixel size
            std::vector<uint8_t> first; int tw = 0, th = 0;
            if (!decodeToRGBA(framePaths[0], first, tw, th)) return;

            int W = gridCols * tw;
            int H = gridRows * th;
            std::vector<uint8_t> canvasPx((size_t)W * H * 4, 0);

            for (size_t f = 0; f < framePaths.size(); f++) {
                std::vector<uint8_t> px; int w = 0, h = 0;
                if (f == 0) { px = first; w = tw; h = th; }
                else if (!decodeToRGBA(framePaths[f], px, w, h)) continue;

                int col = (f < frameCells.size()) ? frameCells[f].first  : (int)(f % gridCols);
                int row = (f < frameCells.size()) ? frameCells[f].second : (int)(f / gridCols);
                int ox = col * tw, oy = row * th;

                for (int y = 0; y < h && (oy + y) < H; y++) {
                    for (int x = 0; x < w && (ox + x) < W; x++) {
                        int si = (y * w + x) * 4;
                        int di = ((oy + y) * W + (ox + x)) * 4;
                        canvasPx[di+0] = px[si+0];
                        canvasPx[di+1] = px[si+1];
                        canvasPx[di+2] = px[si+2];
                        canvasPx[di+3] = px[si+3];
                    }
                }
            }

            merged = true;
            setGrid(gridCols, gridRows);       // show tile boundaries on the composite
            stagePixels(std::move(canvasPx), W, H);
        }

        bool hasFrames() const { return !framePaths.empty(); }

        void computePrimitives(Event& e) override {

            // Bake main texture (camera feed, or artwork when no camera)
            if (needsBake) {
                delete imgTexture;
                imgTexture = new Rev::Graphics::Texture(shared->canvas->context, {
                    .data     = pendingPixels.data(),
                    .width    = (size_t)pendingW,
                    .height   = (size_t)pendingH,
                    .channels = 4,
                    .filter   = Rev::Graphics::Texture::Filter::Bilinear
                });
                imgPrimitive->texture = imgTexture;
                srcW = pendingW; srcH = pendingH;
                needsBake = false;
                pendingPixels.clear();
                pendingPixels.shrink_to_fit();
            }

            // Bake overlay texture (artwork, kept separate from camera feed)
            if (overlayNeedsBake && !overlayPending.empty()) {
                delete overlayTexture;
                overlayTexture = new Rev::Graphics::Texture(shared->canvas->context, {
                    .data     = overlayPending.data(),
                    .width    = (size_t)overlayPendingW,
                    .height   = (size_t)overlayPendingH,
                    .channels = 4,
                    .filter   = Rev::Graphics::Texture::Filter::Bilinear
                });
                overlayPrimitive->texture = overlayTexture;
                overlayNeedsBake = false;
                overlayPending.clear();
                overlayPending.shrink_to_fit();
            }

            if (imgPrimitive && imgTexture && srcW > 0 && srcH > 0) {
                // zoom == 0 is the "auto-fit on first show" sentinel: pick the scale
                // that fits the whole image in the view (capped at 1:1).
                if (zoom <= 0.0f && rect.w > 0 && rect.h > 0) {
                    float fit = (std::min)(rect.w / (float)srcW, rect.h / (float)srcH);
                    zoom = (std::min)(fit, 1.0f);
                    if (zoom <= 0.0f) zoom = 1.0f;
                }

                // Scale relative to actual pixels, centered, plus pan offset.
                float dw = srcW * zoom;
                float dh = srcH * zoom;
                float dx = rect.x + (rect.w - dw) * 0.5f + offsetX;
                float dy = rect.y + (rect.h - dh) * 0.5f + offsetY;

                auto& d      = *imgPrimitive->data;
                d.x          = dx; d.y = dy; d.w = dw; d.h = dh;
                d.opacity    = 1.0f;
                d.tileCountX = (float)tileX;
                d.tileCountY = (float)tileY;

                // Position overlay co-registered with the main image (same screen rect)
                // so artwork and camera feed stay locked together during zoom/pan.
                if (overlayPrimitive && overlayTexture) {
                    auto& od      = *overlayPrimitive->data;
                    od.x          = dx; od.y = dy; od.w = dw; od.h = dh;
                    od.opacity    = overlayOpacity;
                    od.tileCountX = 0.0f;
                    od.tileCountY = 0.0f;
                }

                // Update the info overlay text and pin it near the bottom-left.
                if (info) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "zoom %.1fx  (%dx%dpx)", zoom, srcW, srcH);
                    info->content = buf;
                    if (rect.h > 24.0f)
                        info->style->position.top = Px(rect.h - 18.0f);
                }
            }

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            // Draw the box background (and set up overflow stencil) FIRST, then the
            // image on top -- otherwise the opaque PreviewArea background paints over it.
            Box::draw(e);

            if (imgPrimitive && imgTexture)
                imgPrimitive->draw();

            // Draw artwork overlay on top of camera feed
            if (overlayActive && overlayPrimitive && overlayTexture && overlayOpacity > 0.0f)
                overlayPrimitive->draw();
        }
    };

} // namespace LithoControl
