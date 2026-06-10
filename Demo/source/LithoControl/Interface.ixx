module;

// Win32 + GDI+ for file dialogs and PNG→bitmap conversion
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <setupapi.h>
#pragma comment(lib, "setupapi.lib")

#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

#include <dbg.hpp>

export module LithoControl.Interface;

import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Style;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.TextInput;
import Rev.Element.Dropdown;
import Rev.Element.Checkbox;
import Rev.Serial;
import Rev.SocketClient;
import Rev.Primitive.Image;
import Rev.Graphics.Texture;

export namespace LithoControl {

    using namespace Rev;
    using namespace Rev::Element;
    using Text = Rev::Element::Text;  // disambiguate from Rev::Primitives::Text

    // ─────────────────────────────────────────────────────────────────────────
    // Theme constants
    // ─────────────────────────────────────────────────────────────────────────

    namespace Theme {

        // Base styles shared across buttons, inputs, sections
        Style SidebarRoot = {
            .overflow = Overflow::Hide,
            .layout = { Axis::Vertical, Align::Start, Align::Start },
            .size   = { 320_px, 100_pct },
            .background = { .color = rgba(20, 20, 20, 1) }
        };

        Style SectionHdr = {
            .layout   = { Axis::Horizontal, Align::Start, Align::Center },
            .size     = { 100_pct },
            .padding  = { 5_px, 5_px, 8_px, 8_px },
            .background = { .color = rgba(20, 20, 20, 1), .transition = 100_ms },
            .border   = { .top = { .color = rgba(42, 42, 42, 1), .width = 1_px } },
            .cursor   = Cursor::Hand
        };

        Style SectionHdrHover = {
            .applies    = { .hover = true },
            .background = { .color = rgba(28, 28, 28, 1) }
        };

        Style SectionBody = {
            .layout  = { Axis::Vertical, Align::Start, Align::Start },
            .size    = { 100_pct },
            .padding = { 8_px, 8_px, 8_px, 8_px }
        };

        Style RowH = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center },
            .size   = { 100_pct },
            .margin = { .bottom = 6_px }
        };

        Style Btn = {
            .layout   = { Axis::Horizontal, Align::Center, Align::Center },
            .size     = { Grow() },
            .margin   = { 3_px, 3_px, 3_px, 3_px },
            .padding  = { 5_px, 5_px, 12_px, 12_px },
            .background = { .color = rgba(30, 30, 30, 1), .transition = 100_ms },
            .border   = { .color = rgba(42, 42, 42, 1), .radius = 3_px, .width = 1_px },
            .cursor   = Cursor::Hand
        };

        Style BtnHover = {
            .applies    = { .hover = true },
            .background = { .color = rgba(42, 42, 42, 1) },
            .border     = { .color = rgba(0, 87, 255, 1) }
        };

        Style BtnAccent = {
            .layout   = { Axis::Horizontal, Align::Center, Align::Center },
            .size     = { Grow() },
            .margin   = { 3_px, 3_px, 3_px, 3_px },
            .padding  = { 5_px, 5_px, 12_px, 12_px },
            .background = { .color = rgba(0, 87, 255, 1) },
            .border   = { .radius = 3_px },
            .cursor   = Cursor::Hand
        };

        Style BtnAccentHover = {
            .applies    = { .hover = true },
            .background = { .color = rgba(0, 200, 255, 1) }
        };

        Style BtnDanger = {
            .layout   = { Axis::Horizontal, Align::Center, Align::Center },
            .size     = { Grow() },
            .margin   = { 3_px, 3_px, 3_px, 3_px },
            .padding  = { 5_px, 5_px, 12_px, 12_px },
            .background = { .color = rgba(255, 59, 48, 1) },
            .border   = { .radius = 3_px },
            .cursor   = Cursor::Hand
        };

        Style BtnSuccess = {
            .layout   = { Axis::Horizontal, Align::Center, Align::Center },
            .size     = { Grow() },
            .margin   = { 3_px, 3_px, 3_px, 3_px },
            .padding  = { 5_px, 5_px, 12_px, 12_px },
            .background = { .color = rgba(48, 209, 88, 1) },
            .border   = { .radius = 3_px },
            .cursor   = Cursor::Hand
        };

        Style BtnWarning = {
            .layout   = { Axis::Horizontal, Align::Center, Align::Center },
            .size     = { Grow() },
            .margin   = { 3_px, 3_px, 3_px, 3_px },
            .padding  = { 5_px, 5_px, 12_px, 12_px },
            .background = { .color = rgba(255, 159, 10, 1) },
            .border   = { .radius = 3_px },
            .cursor   = Cursor::Hand
        };

        Style BtnTxt     = { .text = { .color = rgba(232, 232, 232, 1), .size = 11_px } };
        Style BtnTxtBold = { .text = { .color = rgba(255, 255, 255, 1), .size = 11_px } };

        Style RightPanel = {
            .layout = { Axis::Vertical, Align::Start, Align::Start },
            .size   = { Grow(), 100_pct },
            .background = { .color = rgba(13, 13, 13, 1) }
        };

        Style PreviewArea = {
            .layout = { Axis::Horizontal, Align::Center, Align::Center },
            .size   = { 100_pct, Grow() },
            .margin = { 8_px, 8_px, 8_px, 8_px },
            .background = { .color = rgba(10, 10, 10, 1) },
            .border = { .color = rgba(42, 42, 42, 1), .radius = 4_px, .width = 1_px }
        };

        Style StatusPanel = {
            .layout = { Axis::Vertical, Align::Start, Align::Start },
            .size   = { 100_pct },
            .padding = { 8_px, 8_px, 8_px, 8_px },
            .background = { .color = rgba(20, 20, 20, 1) }
        };

        Style ProgressTrack = {
            .layout = { Axis::Horizontal, Align::Start, Align::Start },
            .size   = { Grow(), 6_px },
            .background = { .color = rgba(28, 28, 28, 1) },
            .border = { .radius = 3_px }
        };

        Style ProgressFill = {
            .size   = { 0_pct, 100_pct },
            .background = { .color = rgba(0, 87, 255, 1) },
            .border = { .radius = 3_px }
        };

        Style LogBox = {
            .overflow = Overflow::Hide,
            .size     = { 100_pct, 100_px },
            .margin   = { .bottom = 4_px },
            .padding  = { 4_px, 4_px, 6_px, 6_px },
            .background = { .color = rgba(28, 28, 28, 1) },
            .border   = { .color = rgba(42, 42, 42, 1), .radius = 3_px, .width = 1_px }
        };

        Style LogText = {
            .size = { 100_pct },
            .text = { .color = rgba(232, 232, 232, 1), .size = 10_px, .wrap = Wrap::BreakWord }
        };

        Style GcodeText = {
            .size = { 100_pct },
            .text = { .color = rgba(0, 255, 136, 1), .size = 10_px, .wrap = Wrap::BreakWord }
        };

        Style DimLabel = { .text = { .color = rgba(232, 232, 232, 0.4f), .size = 9_px } };
        Style NormText = { .text = { .color = rgba(232, 232, 232, 1),    .size = 11_px } };

        Style JobItem = {
            .size     = { 100_pct },
            .padding  = { 4_px, 4_px, 8_px, 8_px },
            .background = { .color = rgba(28, 28, 28, 0), .transition = 80_ms },
            .cursor   = Cursor::Hand
        };

        Style JobItemHover = {
            .applies    = { .hover = true },
            .background = { .color = rgba(28, 28, 28, 1) }
        };

        Style JobItemSelected = {
            .background = { .color = rgba(0, 87, 255, 1) }
        };

    } // namespace Theme

    // ─────────────────────────────────────────────────────────────────────────
    // Thread-safe string queue
    // ─────────────────────────────────────────────────────────────────────────

    struct MsgQueue {
        std::mutex              mtx;
        std::deque<std::string> q;

        void push(std::string s) {
            { std::lock_guard g(mtx); q.push_back(std::move(s)); }
            // Trigger a repaint so computeStyle drains this message without waiting
            // for the next user-input event. Safe to call from background threads.
            HWND hw = GetForegroundWindow();
            if (hw) InvalidateRect(hw, nullptr, FALSE);
        }

        bool pop(std::string& out) {
            std::lock_guard g(mtx);
            if (q.empty()) return false;
            out = std::move(q.front()); q.pop_front(); return true;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // ImagePreview element — renders a PNG with an optional tile-grid overlay
    // ─────────────────────────────────────────────────────────────────────────

    struct ImagePreview : public Box {

        Rev::Primitives::Image*    imgPrimitive = nullptr;
        Rev::Graphics::Texture*    imgTexture   = nullptr;

        int srcW = 0, srcH = 0;     // current texture pixel dimensions
        int tileX = 0, tileY = 0;   // tile grid counts (0 = no grid)

        // Deferred-bake state: decoding happens on the CPU at any time, but the GL
        // texture is (re)created in computePrimitives where the context is current —
        // mirroring Core::Svg::bake(). Creating textures in event handlers (where the
        // GL context may not be current) is why images previously failed to appear.
        std::vector<uint8_t> pendingPixels;
        int  pendingW = 0, pendingH = 0;
        bool needsBake = false;

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

        ImagePreview(Element* parent, StyleList styles = {})
            : Box(parent, styles)
        {
            imgPrimitive = new Rev::Primitives::Image(shared->canvas);

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
            // on its own — call refresh(e) to force a repaint this frame.
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
        }

        // Decode a PNG/BMP/JPEG via GDI+ into a tightly-packed RGBA buffer.
        static bool decodeToRGBA(const std::string& path,
                                 std::vector<uint8_t>& out, int& w, int& h) {
            std::wstring wp(path.begin(), path.end());
            Gdiplus::Bitmap bmp(wp.c_str());
            if (bmp.GetLastStatus() != Gdiplus::Ok) return false;

            w = (int)bmp.GetWidth();
            h = (int)bmp.GetHeight();
            if (w <= 0 || h <= 0) return false;

            Gdiplus::Bitmap rgbaBmp(w, h, PixelFormat32bppARGB);
            {
                Gdiplus::Graphics g(&rgbaBmp);
                g.DrawImage(&bmp, 0, 0, w, h);
            }

            Gdiplus::BitmapData bd;
            Gdiplus::Rect grect(0, 0, w, h);
            rgbaBmp.LockBits(&grect, Gdiplus::ImageLockModeRead,
                             PixelFormat32bppARGB, &bd);

            out.assign((size_t)w * h * 4, 0);
            auto* src = reinterpret_cast<uint8_t*>(bd.Scan0);
            for (int row = 0; row < h; row++) {
                for (int col = 0; col < w; col++) {
                    int si = row * bd.Stride + col * 4;
                    int di = (row * w + col) * 4;
                    out[di+0] = src[si+2];  // R  (GDI+ stores BGRA)
                    out[di+1] = src[si+1];  // G
                    out[di+2] = src[si+0];  // B
                    out[di+3] = src[si+3];  // A
                }
            }
            rgbaBmp.UnlockBits(&bd);
            return true;
        }

        // Queue a decoded buffer for upload on the next render pass.
        void stagePixels(std::vector<uint8_t>&& px, int w, int h) {
            pendingPixels = std::move(px);
            pendingW = w; pendingH = h;
            needsBake = true;
            // Reset the view to fit-on-first-show (zoom recomputed in computePrimitives)
            zoom = 0.0f;                 // 0 = "auto-fit on next layout"
            offsetX = offsetY = 0.0f;
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

            // Bake: upload any staged pixels now that the GL context is current.
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
            // image on top — otherwise the opaque PreviewArea background paints over it.
            Box::draw(e);

            if (imgPrimitive && imgTexture) {
                imgPrimitive->draw();
            }
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Interface
    // ─────────────────────────────────────────────────────────────────────────

    struct Interface : public Box {

        // ── State ────────────────────────────────────────────────────────────

        enum class Platform { STM32, Pi };
        enum class JobState  { Idle, Running, Paused };

        Platform  platform  = Platform::STM32;
        bool      connFlag  = false;
        JobState  jobState  = JobState::Idle;

        Rev::Serial*       stmSerial = nullptr;
        Rev::SocketClient* piClient  = nullptr;

        std::string inputFilePath;
        std::string dlpRoot;     // set when a file is loaded; used to resolve slicer/jobs paths
        std::string selectedJob;
        std::vector<std::string> jobs;
        bool jobListDirty = false;
        bool platformDirty = false;

        std::atomic<int>  frameN     { 0 };
        std::atomic<int>  frameTotal { 0 };
        std::atomic<bool> abortFlag  { false };
        std::atomic<bool> estopped   { false };   // latched by E-STOP, cleared by RESET
        std::atomic<bool> jobRunning { false };   // true while runStm32Job is executing
        std::thread       jobThread;
        HANDLE            jobThreadHandle = nullptr;  // for CancelSynchronousIo on E-STOP
        // Handle of whichever detached gantry op (jog/home/set-home) is currently
        // running, so E-STOP can cancel its blocking serial read too.
        std::atomic<void*> gantryThreadHandle { nullptr };

        // Serializes all stmSerial transactions so concurrent jog/job threads can't
        // interleave their bytes and corrupt each other's responses.
        std::mutex        serialMtx;

        // Posted by the slicer thread; consumed on the main thread in computeStyle
        std::atomic<int>  pendingTileX { 0 };
        std::atomic<int>  pendingTileY { 0 };
        std::atomic<bool> pendingPreviewReload { false };
        std::atomic<bool> pendingJobRescan    { false };
        std::atomic<bool> pendingSliceDone    { false };  // slicer worker → reset SLICE btn
        std::atomic<int>  pendingConnState    { -1 };  // -1 none, 0 disconnected, 1 connected
        // Display status set by worker threads, rendered into frameLabel on the main
        // thread: 0=idle, 1=running, 2=done, 3=estop. Avoids off-thread label writes.
        std::atomic<int>  dispState           { 0 };
        std::atomic<int>  jobExposeMs         { 0 };   // expose ms of the running job
        int               previewLiveFrame    = -1;    // last frame shown live (main only)

        MsgQueue logQ;
        MsgQueue gcodeQ;

        std::deque<std::string> logLines;
        std::deque<std::string> gcodeLines;
        static constexpr size_t MAX_LOG = 60;

        ULONG_PTR gdipToken = 0;

        // ── Persisted settings ───────────────────────────────────────────────

        struct Settings {
            int    exposeMs  = 3000;
            float  feedRate  = 20.0f;
            int    threshold = 128;
            float  projW     = 1.6f;
            float  projH     = 0.9f;
            float  overlap   = 0.0f;
            bool   invert    = false;
            bool   sendAfterSlice = false;
            std::string port     = "";
            std::string piHost   = "192.168.1.240";
            std::string jobsDir  = "./jobs";
            std::string platform = "stm32";
            std::string dlpRoot  = "";   // persisted repo root for resolving ./jobs
        } settings;

        // ── Sidebar scroll ────────────────────────────────────────────────────

        float sidebarScrollY   = 0.0f;
        Box*  sidebarBox       = nullptr;
        Box*  sidebarContent   = nullptr;
        Box*  sidebarScrollThumb = nullptr;
        bool  sbThumbDragging       = false;
        float sbThumbDragStartY     = 0.0f;
        float sbThumbDragStartScroll = 0.0f;

        // ── UI element pointers ──────────────────────────────────────────────

        // Connection
        Dropdown*  platformDrop     = nullptr;
        Box*       platformRowSlot  = nullptr;
        Box*       stmPortRow       = nullptr;
        Dropdown*  portDrop         = nullptr;
        Box*       piHostRow        = nullptr;
        TextInput* hostInput        = nullptr;
        Text*      connStatus     = nullptr;
        Text*      connectBtnTxt  = nullptr;

        // Slicer
        Text*      fileLabel      = nullptr;
        TextInput* jobNameInput   = nullptr;
        TextInput* exposeMsInput  = nullptr;
        TextInput* feedRateInput  = nullptr;
        TextInput* thresholdInput = nullptr;
        TextInput* projWInput     = nullptr;
        TextInput* projHInput     = nullptr;
        TextInput* overlapInput   = nullptr;
        Checkbox*  invertChk      = nullptr;
        Checkbox*  sendTargetChk  = nullptr;
        Box*       sendChkSlot    = nullptr;
        Text*      sliceBtnTxt    = nullptr;

        // Job queue
        TextInput* jobsDirInput   = nullptr;
        Box*       jobListBox     = nullptr;
        std::vector<Box*> jobItemBoxes;

        // Jog
        TextInput* jogDistInput   = nullptr;

        // Status
        Text*  frameLabel    = nullptr;
        Box*   progressFill  = nullptr;
        Box*   runnerLogBox  = nullptr;
        Text*  runnerLogTxt  = nullptr;
        float  runnerLogScrollY = 0.0f;
        Box*   gcodeLogBox   = nullptr;
        Text*  gcodeLogTxt   = nullptr;
        float  gcodeLogScrollY = 0.0f;

        // Preview
        ImagePreview* previewImg   = nullptr;
        Text*         previewLabel = nullptr;

        // Job list scroll
        float jobListScrollY    = 0.0f;
        Box*  jobListContent    = nullptr;

        // Sidebar collapse + drag resize
        bool   sidebarCollapsed      = false;
        Text*  sidebarCollapseBtn    = nullptr;
        bool   sidebarDragging       = false;
        bool   sidebarDragMoved      = false;
        float  sidebarDragStartX     = 0.0f;
        float  sidebarDragStartW     = 320.0f;
        Box*   sidebarScrollTrackBox = nullptr;

        // Status panel drag resize
        bool   statusDragging    = false;
        float  statusDragStartY  = 0.0f;
        float  statusDragStartH  = 200.0f;
        float  statusPanelH      = 200.0f;
        Box*   statusPanelBox    = nullptr;

        // ── Constructor ──────────────────────────────────────────────────────

        Interface(Element* parent) : Box(parent) {

            Gdiplus::GdiplusStartupInput gi;
            Gdiplus::GdiplusStartup(&gdipToken, &gi, nullptr);

            this->style->layout           = { Axis::Horizontal, Align::Start, Align::Start };
            this->style->size             = { 100_pct, 100_pct };
            this->style->background.color = rgba(13, 13, 13, 1);

            loadSettings();
            dlpRoot = settings.dlpRoot;   // restore so ./jobs resolves without a browse
            buildSidebar();
            buildCollapseHandle();
            buildRightPanel();
            refreshJobList();
            scanPorts(false);   // populate the COM list at startup; don't select/connect

            // Global drag handlers — fire on any mouse move/up over the Interface,
            // so drags stay active even when the cursor leaves the originating handle.
            this->onMouseMove([this](Rev::Element::Event& e) {
                if (sidebarDragging) {
                    float dx = e.mouse.pos.x - sidebarDragStartX;
                    if (std::abs(dx) > 4.0f) sidebarDragMoved = true;
                    // Snap to 0 (collapsed) below 150px; otherwise clamp to a usable
                    // minimum so controls aren't swallowed by an over-narrow sidebar.
                    float newW = std::clamp(sidebarDragStartW + dx, 0.0f, 700.0f);
                    if (newW < 150.0f) newW = (newW < 75.0f) ? 0.0f : 280.0f;
                    if (sidebarBox)
                        sidebarBox->style->size.width = Px(newW);
                    if (sidebarScrollTrackBox)
                        sidebarScrollTrackBox->style->position.left = Px(newW - 6.0f);
                }
                if (statusDragging) {
                    float dy    = e.mouse.pos.y - statusDragStartY;
                    float newH  = std::clamp(statusDragStartH - dy, 80.0f, 600.0f);
                    statusPanelH = newH;
                    if (statusPanelBox)
                        statusPanelBox->style->size.height = Px(statusPanelH);
                }
                if (sbThumbDragging && sidebarBox && sidebarContent) {
                    // Map thumb travel (track minus thumb) to content scroll range.
                    float trackH   = sidebarBox->rect.h;
                    float contentH = measureSpread(sidebarContent);
                    float maxScroll = (std::max)(0.0f, contentH - trackH);
                    float thumbH   = (contentH > trackH) ? (std::max)(20.0f, (trackH / contentH) * trackH) : trackH;
                    float travel   = (std::max)(1.0f, trackH - thumbH);
                    float dy       = e.mouse.pos.y - sbThumbDragStartY;
                    sidebarScrollY = std::clamp(sbThumbDragStartScroll + dy * (maxScroll / travel), 0.0f, maxScroll);
                    sidebarContent->style->position.top = Px(-sidebarScrollY);
                }
            });

            this->onMouseUp([this](Rev::Element::Event&) {
                sbThumbDragging = false;
                if (sidebarDragging && !sidebarDragMoved) {
                    // Click (no drag) — toggle collapse
                    sidebarCollapsed = !sidebarCollapsed;
                    if (sidebarCollapseBtn)
                        sidebarCollapseBtn->content = sidebarCollapsed ? ">" : "<";
                    float targetW = sidebarCollapsed ? 0.0f
                                  : (sidebarDragStartW > 30.0f ? sidebarDragStartW : 320.0f);
                    if (sidebarBox)
                        sidebarBox->style->size.width = Px(targetW);
                    if (sidebarScrollTrackBox)
                        sidebarScrollTrackBox->style->position.left = Px(targetW - 6.0f);
                }
                sidebarDragging = false;
                statusDragging  = false;
            });
        }

        ~Interface() {
            abortFlag = true;
            if (jobThread.joinable()) jobThread.join();
            delete stmSerial;
            delete piClient;
            saveSettings();
            if (gdipToken) Gdiplus::GdiplusShutdown(gdipToken);
        }

        // ── Build sidebar ─────────────────────────────────────────────────────

        void buildSidebar() {

            sidebarBox = new Box(this, { &Theme::SidebarRoot });
            Box* sb = sidebarBox;

            // Absolutely-positioned content column: full width, grows to fit all sections.
            // Shifted up by sidebarScrollY each frame.
            sidebarContent = new Box(sb);
            sidebarContent->style->layout.direction  = Axis::Vertical;
            sidebarContent->style->layout.horizontal = Align::Start;
            sidebarContent->style->layout.vertical   = Align::Start;
            sidebarContent->style->layout.position   = Position::Absolute;
            sidebarContent->style->size.width        = 100_pct;

            // Title
            Text* title = new Text(sidebarContent, "LITHOCONTROL  v2.0");
            title->style->size.width     = 100_pct;
            title->style->padding        = { 10_px, 8_px, 10_px, 10_px };
            title->style->text.color     = rgba(232, 232, 232, 1);
            title->style->text.size      = 12_px;
            title->style->border.bottom.color = rgba(42, 42, 42, 1);
            title->style->border.bottom.width = 1_px;

            Box* connBody = nullptr;
            makeSection(sidebarContent, "CONNECTION", connBody, false);
            buildConnectionPanel(connBody);

            Box* slicerBody = nullptr;
            makeSection(sidebarContent, "SLICER", slicerBody, false);
            buildSlicerPanel(slicerBody);

            Box* jobBody = nullptr;
            makeSection(sidebarContent, "JOB QUEUE", jobBody, false);
            buildJobPanel(jobBody);

            Box* jogBody = nullptr;
            makeSection(sidebarContent, "JOG", jogBody, false);
            buildJogPanel(jogBody);

            // Scrollbar track: thin strip on the right edge of the sidebar.
            // Absolutely positioned so it stays fixed while content scrolls.
            sidebarScrollTrackBox = new Box(sb);
            sidebarScrollTrackBox->style->layout.position      = Position::Absolute;
            sidebarScrollTrackBox->style->position.top         = Px(0);
            sidebarScrollTrackBox->style->position.left        = Px(314); // 320 - 6
            sidebarScrollTrackBox->style->size.width           = 6_px;
            sidebarScrollTrackBox->style->size.height          = 100_pct;
            sidebarScrollTrackBox->style->background.color     = rgba(30, 30, 30, 1);
            sidebarScrollTrackBox->style->border.radius        = 3_px;

            // Scrollbar thumb: sized and positioned in computeStyle each frame.
            sidebarScrollThumb = new Box(sidebarScrollTrackBox);
            sidebarScrollThumb->style->layout.position  = Position::Absolute;
            sidebarScrollThumb->style->position.left    = Px(0);
            sidebarScrollThumb->style->position.top     = Px(0);
            sidebarScrollThumb->style->size.width       = 6_px;
            sidebarScrollThumb->style->size.height      = Px(40);
            sidebarScrollThumb->style->background.color = rgba(110, 110, 110, 1);
            sidebarScrollThumb->style->border.radius    = 3_px;
            sidebarScrollThumb->style->cursor           = Cursor::Hand;

            // Grab the thumb to scroll. The drag delta is mapped to scroll in the
            // global onMouseMove handler (so it keeps tracking outside the thumb).
            sidebarScrollThumb->onMouseDown([this](Rev::Element::Event& e) {
                sbThumbDragging       = true;
                sbThumbDragStartY     = e.mouse.pos.y;
                sbThumbDragStartScroll = sidebarScrollY;
                e.propagate = false;
            });

            // Wheel handler: update scroll and immediately dirty sidebarContent
            // so the frame repaints without waiting for a hover event. Only consume
            // when the cursor is actually over the sidebar.
            sb->onMouseWheel([this, sb](Rev::Element::Event& e) {
                if (!sb->rect.contains(e.mouse.pos)) return;
                sidebarScrollY -= (e.mouse.wheel.y / 120.0f) * 40.0f;
                if (sidebarScrollY < 0.0f) sidebarScrollY = 0.0f;
                sidebarContent->style->position.top = Px(-sidebarScrollY);
                e.propagate = false;
            });
        }

        // Collapsible section header + body
        void makeSection(Box* parent, const std::string& title, Box*& body, bool collapsed) {

            Box* hdr = new Box(parent, { &Theme::SectionHdr, &Theme::SectionHdrHover });
            hdr->style->layout = { Axis::Horizontal, Align::Start, Align::Center };

            Text* arrow = new Text(hdr, collapsed ? ">" : "v");
            arrow->style->text.color = rgba(232, 232, 232, 0.4f);
            arrow->style->text.size  = 10_px;
            arrow->style->margin.right = 6_px;

            Text* lbl = new Text(hdr, title);
            lbl->style->text.color = rgba(232, 232, 232, 0.4f);
            lbl->style->text.size  = 10_px;

            body = new Box(parent, { &Theme::SectionBody });

            // Saved children used as collapse-state indicator (empty = expanded)
            auto saved = std::make_shared<std::vector<Element*>>();

            hdr->onMouseDown([arrow, body, saved](Rev::Element::Event& e) {
                if (saved->empty()) {
                    // Collapse: detach children and zero padding
                    *saved = body->children;
                    for (auto* c : *saved) body->removeChild(c);
                    body->style->padding = 0_px;
                    arrow->content = ">";
                } else {
                    // Expand: restore padding then reattach children
                    body->style->padding = { 8_px, 8_px, 8_px, 8_px };
                    for (auto* c : *saved) body->addChild(c);
                    saved->clear();
                    arrow->content = "v";
                }
            });
        }

        // ── Sidebar collapse handle ───────────────────────────────────────────
        // A 16px-wide strip between sidebar and right panel — always visible,
        // lets the user hide/show the sidebar without losing access to the toggle.

        void buildCollapseHandle() {
            Box* handle = new Box(this);
            handle->style->size             = { 16_px, 100_pct };
            handle->style->layout           = { Axis::Vertical, Align::Center, Align::Start };
            handle->style->background.color = rgba(20, 20, 20, 1);
            handle->style->border.right     = { rgba(42, 42, 42, 1), 1_px };
            handle->style->cursor           = Cursor::ArrowsHorizontal;

            sidebarCollapseBtn = new Text(handle, "<");
            sidebarCollapseBtn->style->text.color = rgba(232, 232, 232, 0.4f);
            sidebarCollapseBtn->style->text.size  = 9_px;
            sidebarCollapseBtn->style->margin.top = 8_px;

            handle->onMouseDown([this](Rev::Element::Event& e) {
                sidebarDragging   = true;
                sidebarDragMoved  = false;
                sidebarDragStartX = e.mouse.pos.x;
                sidebarDragStartW = sidebarBox ? sidebarBox->rect.w : 320.0f;
            });
        }

        // ── Connection panel ─────────────────────────────────────────────────

        void buildConnectionPanel(Box* body) {

            // Platform selector
            Text* platLbl = new Text(body, "PLATFORM");
            platLbl->style->text.color  = rgba(232, 232, 232, 0.4f);
            platLbl->style->text.size   = 9_px;
            platLbl->style->margin.bottom = 4_px;

            platformDrop = new Dropdown(body, {
                .options = { { "STM32 (Serial)", "stm32" }, { "Pi Zero 2W (TCP)", "pi" } },
                .placeholder = "STM32 (Serial)",
                .value = (settings.platform == "pi") ? "pi" : "stm32"
            });
            platformDrop->label->style->visibility = Visibility::Hidden;

            // Slot: holds exactly one platform-specific row at a time.
            // Using a slot instead of Visibility::Hidden means the hidden row is
            // removed from the tree and takes no layout space.
            platformRowSlot = new Box(body);
            platformRowSlot->style->layout.direction  = Axis::Vertical;
            platformRowSlot->style->layout.horizontal = Align::Start;
            platformRowSlot->style->size.width        = 100_pct;
            platformRowSlot->style->margin.top        = 6_px;

            // STM32 port row — vertical so dropdown + scan button stack cleanly
            stmPortRow = new Box(platformRowSlot);
            stmPortRow->style->layout = { Axis::Vertical, Align::Start, Align::Start };
            stmPortRow->style->size.width = 100_pct;

            Text* portLbl = new Text(stmPortRow, "COM PORT");
            portLbl->style->text.color    = rgba(232, 232, 232, 0.4f);
            portLbl->style->text.size     = 9_px;
            portLbl->style->margin.bottom = 4_px;

            // Start empty; a scan runs at startup (see constructor) to populate the
            // list with actually-present ports, without selecting/connecting one.
            portDrop = new Dropdown(stmPortRow, {
                .options     = {},
                .placeholder = "Scan for ports...",
                .value       = ""
            });
            portDrop->label->style->visibility = Visibility::Hidden;

            Box* scanRow = new Box(stmPortRow, { &Theme::RowH });
            scanRow->style->margin.top = 4_px;
            makeBtn(scanRow, "SCAN", [this]() { scanPorts(); });

            // Pi host row (built as child of slot so shared/canvas is valid,
            // then removed immediately by updatePlatformRows for the STM32 default)
            piHostRow = new Box(platformRowSlot);
            piHostRow->style->layout    = { Axis::Vertical, Align::Start, Align::Start };
            piHostRow->style->size.width = 100_pct;
            piHostRow->style->margin.bottom = 6_px;

            hostInput = new TextInput(piHostRow, {
                .label = "HOST / IP",
                .placeholder = "",
                .maxLength = 64
            });
            hostInput->text->content     = settings.piHost;
            hostInput->style->size.width = 100_pct;
            hostInput->label->style->text.color = rgba(232, 232, 232, 0.6f);
            hostInput->label->style->text.size  = 9_px;
            tightenInput(hostInput);

            // Status row
            Box* connRow = new Box(body, { &Theme::RowH });
            connRow->style->margin.top = 8_px;

            connStatus = new Text(connRow, "DISCONNECTED");
            connStatus->style->text.color = rgba(255, 59, 48, 1);
            connStatus->style->text.size  = 11_px;
            connStatus->style->size       = { Grow() };

            // Connect button — no fixed px width, flex with the row
            Box* connBtn = makeBtn(connRow, "CONNECT", nullptr, true);
            connectBtnTxt = (Text*)connBtn->children[0];
            connBtn->onMouseDown([this](Rev::Element::Event& e) { toggleConnect(); });

            // Activate the correct row for the initial platform
            updatePlatformRows();
        }

        // ── Slicer panel ──────────────────────────────────────────────────────

        void buildSlicerPanel(Box* body) {

            // File label + browse
            Box* fileRow = new Box(body, { &Theme::RowH });

            fileLabel = new Text(fileRow, "No file selected");
            fileLabel->style->text.color = rgba(232, 232, 232, 0.4f);
            fileLabel->style->text.size  = 10_px;
            fileLabel->style->size       = { Grow() };
            fileLabel->style->text.wrap  = Wrap::BreakWord;

            Box* browseBtn = makeBtn(fileRow, "BROWSE", [this]() { browseFile(); }, false);
            browseBtn->style->size.width = 70_px;
            browseBtn->style->size.max.width = 70_px;
            (void)browseBtn;

            Box* prevBtn = makeBtn(fileRow, "PREVIEW", [this]() {
                if (inputFilePath.empty()) { logQ.push("No file loaded"); return; }
                if (previewImg) {
                    previewImg->setGrid(0, 0);
                    previewImg->loadFile(inputFilePath);
                }
            }, false);
            prevBtn->style->size.width = 70_px;
            prevBtn->style->size.max.width = 70_px;
            (void)prevBtn;

            jobNameInput   = makeInput(body, "JOB NAME",   "",                         200);
            exposeMsInput  = makeInput(body, "EXPOSE MS",  std::to_string(settings.exposeMs), 8);
            feedRateInput  = makeInput(body, "FEED RATE",  fmtFloat(settings.feedRate), 8);
            thresholdInput = makeInput(body, "THRESHOLD",  std::to_string(settings.threshold), 4);
            projWInput     = makeInput(body, "PROJ W (mm)", fmtFloat(settings.projW),  8);
            projHInput     = makeInput(body, "PROJ H (mm)", fmtFloat(settings.projH),  8);
            overlapInput   = makeInput(body, "OVERLAP",    fmtFloat(settings.overlap), 6);

            invertChk = new Checkbox(body, {
                .label = "Invert artwork",
                .def   = settings.invert
            });
            invertChk->label->style->text.color = rgba(232, 232, 232, 0.7f);
            invertChk->label->style->text.size  = 11_px;

            // Off by default — slicing never touches the Pi unless this is ticked.
            // Only relevant on the Pi platform; the checkbox lives in a dedicated
            // slot (kept in the tree) and is added/removed from it per platform.
            sendChkSlot = new Box(body);
            sendChkSlot->style->layout    = { Axis::Vertical, Align::Start, Align::Start };
            sendChkSlot->style->size.width = 100_pct;
            sendTargetChk = new Checkbox(sendChkSlot, {
                .label = "Send to target after slicing (Pi)",
                .def   = settings.sendAfterSlice
            });
            sendTargetChk->label->style->text.color = rgba(232, 232, 232, 0.7f);
            sendTargetChk->label->style->text.size  = 11_px;

            Box* sliceRow = new Box(body, { &Theme::RowH });
            sliceRow->style->margin.top = 6_px;

            Box* sliceBtn = makeBtn(sliceRow, "SLICE", [this]() { startSlice(); }, true);
            sliceBtnTxt = (Text*)sliceBtn->children[0];
            (void)sliceBtn;

            // Set initial visibility for the current platform
            updateSendChkVisibility();
        }

        // Show the "send to target" checkbox only on the Pi platform. Remove it from
        // the layout entirely on STM32 so it leaves no gap.
        void updateSendChkVisibility() {
            if (!sendTargetChk || !sendChkSlot) return;
            auto& kids = sendChkSlot->children;
            bool present = std::find(kids.begin(), kids.end(),
                                     (Element*)sendTargetChk) != kids.end();
            // Add/remove from a fixed slot (proven-safe, same as platformRowSlot).
            if (platform == Platform::Pi && !present) {
                sendChkSlot->addChild(sendTargetChk);
            } else if (platform == Platform::STM32 && present) {
                sendChkSlot->removeChild(sendTargetChk);
            }
        }

        // ── Job panel ─────────────────────────────────────────────────────────

        void buildJobPanel(Box* body) {

            Box* dirRow = new Box(body, { &Theme::RowH });

            jobsDirInput = new TextInput(dirRow, {
                .label = "JOBS DIR",
                .placeholder = "",
                .maxLength = 256
            });
            // Use left-aligned content (not a centered placeholder) so the path
            // isn't clipped/crowded against the left edge of the field. Show the
            // resolved absolute path when the repo root is known (persisted), so the
            // field reflects where jobs actually load from.
            {
                std::string shown = settings.jobsDir;
                if (!dlpRoot.empty() && !std::filesystem::path(shown).is_absolute())
                    shown = (std::filesystem::path(dlpRoot) / shown).string();
                jobsDirInput->text->content = shown;
            }
            jobsDirInput->label->style->text.color = rgba(232, 232, 232, 0.6f);
            jobsDirInput->label->style->text.size  = 9_px;
            tightenInput(jobsDirInput);

            Box* browseDir = makeBtn(dirRow, "...", [this]() { browseJobsDir(); }, false);
            browseDir->style->size.width = 30_px;
            browseDir->style->size.max.width = 30_px;
            (void)browseDir;

            // Job list container. It flows naturally in the sidebar (no fixed height,
            // no inner clip) so it grows with its items and scrolls with the whole
            // sidebar. A nested overflow-clip here caused the list to vanish entirely
            // when its bottom ran past the window edge.
            jobListBox = new Box(body);
            jobListBox->style->layout   = { Axis::Vertical, Align::Start, Align::Start };
            jobListBox->style->size.width        = 100_pct;
            // NOTE: do not set size.min.height — the framework uses size.h.min for the
            // content-derived minimum, so an explicit min would pin the box height and
            // the items would overflow into the button row below.
            jobListBox->style->background.color = rgba(28, 28, 28, 1);
            jobListBox->style->border.color     = rgba(42, 42, 42, 1);
            jobListBox->style->border.width     = 1_px;
            jobListBox->style->border.radius    = 3_px;
            jobListBox->style->margin.bottom    = 6_px;
            jobListBox->style->padding          = { 2_px, 2_px, 2_px, 2_px };

            // Items are added directly to jobListBox (no absolute inner column).
            jobListContent = jobListBox;

            Box* btnRow = new Box(body, { &Theme::RowH });

            makeBtn(btnRow, "REFRESH", [this]() { refreshJobList(); }, false);
            makeBtn(btnRow, "PREVIEW", [this]() { previewJob(); }, false);
            makeBtn(btnRow, "START",   [this]() { startJob(); }, false, false, true);
            makeBtn(btnRow, "PAUSE",   [this]() { pauseJob(); }, false);
            makeBtn(btnRow, "ABORT",   [this]() { abortJob(); }, false, true);
        }

        // ── Jog panel ─────────────────────────────────────────────────────────

        void buildJogPanel(Box* body) {

            jogDistInput = makeInput(body, "DISTANCE (mm)", "1.0", 8);

            // D-pad
            Box* row1 = new Box(body, { &Theme::RowH });
            row1->style->layout = { Axis::Horizontal, Align::Center, Align::Center };
            makeJogBtn(row1, "Y+", [this]() { jog("Y", +1); });

            Box* row2 = new Box(body, { &Theme::RowH });
            row2->style->layout = { Axis::Horizontal, Align::Center, Align::Center };
            makeJogBtn(row2, "X-",  [this]() { jog("X", -1); });
            makeJogBtn(row2, "HOME",[this]() { sendHome(); });
            makeJogBtn(row2, "X+",  [this]() { jog("X", +1); });

            Box* row3 = new Box(body, { &Theme::RowH });
            row3->style->layout = { Axis::Horizontal, Align::Center, Align::Center };
            makeJogBtn(row3, "Y-", [this]() { jog("Y", -1); });

            // E-stop / Reset / Set Home
            Box* ctrlRow = new Box(body, { &Theme::RowH });
            ctrlRow->style->margin.top = 6_px;
            makeBtn(ctrlRow, "E-STOP",   [this]() { sendEstop(); },   false, true);
            makeBtn(ctrlRow, "RESET",    [this]() { sendReset(); },   false, false, false, true);
            makeBtn(ctrlRow, "SET HOME", [this]() { sendSetHome(); }, false);
        }

        // ── Right panel ───────────────────────────────────────────────────────

        void buildRightPanel() {

            Box* rp = new Box(this, { &Theme::RightPanel });

            // Preview control bar — frame cycling + merge for sliced jobs
            Box* prevBar = new Box(rp);
            prevBar->style->layout        = { Axis::Horizontal, Align::Start, Align::Center };
            prevBar->style->size.width    = 100_pct;
            prevBar->style->padding       = { 8_px, 8_px, 6_px, 6_px };
            prevBar->style->background.color = rgba(20, 20, 20, 1);

            auto makeSmallBtn = [this](Box* parent, const std::string& lbl, std::function<void()> cb) {
                Box* b = new Box(parent, { &Theme::Btn, &Theme::BtnHover });
                b->style->size     = { 64_px, 22_px };
                b->style->size.max = { 64_px, 22_px };
                b->style->margin   = { 2_px, 2_px, 0_px, 0_px };
                Text* t = new Text(b, lbl);
                t->style->text.size  = 10_px;
                t->style->text.color = rgba(232, 232, 232, 1);
                if (cb) b->onMouseDown([cb](Rev::Element::Event&) { cb(); });
                return b;
            };

            makeSmallBtn(prevBar, "< PREV", [this]() {
                if (previewImg) { previewImg->cycleFrame(-1); updatePreviewLabel(); }
            });
            makeSmallBtn(prevBar, "NEXT >", [this]() {
                if (previewImg) { previewImg->cycleFrame(+1); updatePreviewLabel(); }
            });
            makeSmallBtn(prevBar, "MERGE", [this]() {
                if (previewImg) { previewImg->mergeFrames(); updatePreviewLabel(); }
            });

            previewLabel = new Text(prevBar, "");
            previewLabel->style->text.color = rgba(232, 232, 232, 0.6f);
            previewLabel->style->text.size  = 10_px;
            previewLabel->style->margin.left = 10_px;

            // Preview area — ImagePreview fills remaining height above the status panel
            previewImg = new ImagePreview(rp, { &Theme::PreviewArea });

            // Drag handle between preview and status panel
            Box* statusDragHandle = new Box(rp);
            statusDragHandle->style->size             = { 100_pct, 6_px };
            statusDragHandle->style->background.color = rgba(28, 28, 28, 1);
            statusDragHandle->style->border.top       = { rgba(42, 42, 42, 1), 1_px };
            statusDragHandle->style->cursor           = Cursor::ArrowsVertical;
            statusDragHandle->onMouseDown([this](Rev::Element::Event& e) {
                statusDragging    = true;
                statusDragStartY  = e.mouse.pos.y;
                statusDragStartH  = statusPanelH;
                e.propagate = false;
            });

            // Status panel — explicit height so it can be resized by drag
            statusPanelBox = new Box(rp, { &Theme::StatusPanel });
            statusPanelBox->style->size.height = Px(statusPanelH);

            Box* sp = statusPanelBox;

            // Frame label + progress
            Box* progRow = new Box(sp, { &Theme::RowH });
            progRow->style->margin.bottom = 6_px;

            frameLabel = new Text(progRow, "IDLE");
            frameLabel->style->text.color = rgba(232, 232, 232, 0.4f);
            frameLabel->style->text.size  = 10_px;
            frameLabel->style->margin.right = 8_px;
            frameLabel->style->size.min.width = 120_px;

            Box* progTrack = new Box(progRow, { &Theme::ProgressTrack });
            progressFill = new Box(progTrack, { &Theme::ProgressFill });

            // Log row (runner + gcode side by side) — grows to fill status panel
            Box* logRow = new Box(sp);
            logRow->style->layout        = { Axis::Horizontal, Align::Start, Align::Start };
            logRow->style->size.width    = 100_pct;
            logRow->style->size.height   = Grow();

            // ── Runner log ────────────────────────────────────────────────────

            Box* runnerCol = new Box(logRow);
            runnerCol->style->layout       = { Axis::Vertical, Align::Start, Align::Start };
            runnerCol->style->size.width   = Grow();
            runnerCol->style->size.height  = Grow();
            runnerCol->style->margin.right = 8_px;

            // Header row with COPY button
            Box* runnerHdr = new Box(runnerCol);
            runnerHdr->style->layout        = { Axis::Horizontal, Align::Center, Align::Center };
            runnerHdr->style->size.width    = 100_pct;
            runnerHdr->style->margin.bottom = 4_px;

            Text* runnerLbl = new Text(runnerHdr, "RUNNER LOG");
            runnerLbl->style->text.color = rgba(232, 232, 232, 0.4f);
            runnerLbl->style->text.size  = 9_px;
            runnerLbl->style->size       = { Grow() };

            Box* runnerCopyBtn = new Box(runnerHdr, { &Theme::Btn, &Theme::BtnHover });
            runnerCopyBtn->style->size        = { 32_px, 18_px };
            runnerCopyBtn->style->size.max    = { 32_px, 18_px };
            runnerCopyBtn->style->margin      = { 0_px };
            runnerCopyBtn->style->padding     = { 2_px, 2_px, 4_px, 4_px };
            Text* runnerCopyTxt = new Text(runnerCopyBtn, "CPY");
            runnerCopyTxt->style->text.size  = 9_px;
            runnerCopyTxt->style->text.color = rgba(232, 232, 232, 0.8f);
            runnerCopyBtn->onMouseDown([this](Rev::Element::Event&) { copyToClipboard(logLines); });

            runnerLogBox = new Box(runnerCol, { &Theme::LogBox });
            runnerLogBox->style->size.height = Grow();  // override fixed 100_px from LogBox theme
            runnerLogTxt = new Text(runnerLogBox, "", { &Theme::LogText });
            runnerLogTxt->style->layout.position = Position::Absolute;
            runnerLogTxt->style->size.width      = 100_pct;
            runnerLogTxt->selectable = true;
            runnerLogBox->onMouseWheel([this](Rev::Element::Event& e) {
                // The absolute log text overflows this box's bounds, which marks the
                // whole status-panel chain as "hit" even when the cursor is over the
                // preview. Only consume the wheel if the cursor is truly in this box,
                // otherwise let it fall through to the preview (zoom).
                if (!runnerLogBox->rect.contains(e.mouse.pos)) return;
                float boxH  = runnerLogBox->rect.h;
                float textH = runnerLogTxt->rect.h;
                float maxS  = (std::max)(0.0f, textH - boxH);
                runnerLogScrollY -= (e.mouse.wheel.y / 120.0f) * 20.0f;
                runnerLogScrollY  = std::clamp(runnerLogScrollY, 0.0f, maxS);
                runnerLogTxt->style->position.top = Px(-runnerLogScrollY);
                e.propagate = false;
            });

            // ── G-code log ────────────────────────────────────────────────────

            Box* gcodeCol = new Box(logRow);
            gcodeCol->style->layout      = { Axis::Vertical, Align::Start, Align::Start };
            gcodeCol->style->size.width  = Grow();
            gcodeCol->style->size.height = Grow();

            // Header row with COPY button
            Box* gcodeHdr = new Box(gcodeCol);
            gcodeHdr->style->layout        = { Axis::Horizontal, Align::Center, Align::Center };
            gcodeHdr->style->size.width    = 100_pct;
            gcodeHdr->style->margin.bottom = 4_px;

            Text* gcodeLbl = new Text(gcodeHdr, "GCODE STREAM");
            gcodeLbl->style->text.color = rgba(232, 232, 232, 0.4f);
            gcodeLbl->style->text.size  = 9_px;
            gcodeLbl->style->size       = { Grow() };

            Box* gcodeCopyBtn = new Box(gcodeHdr, { &Theme::Btn, &Theme::BtnHover });
            gcodeCopyBtn->style->size        = { 32_px, 18_px };
            gcodeCopyBtn->style->size.max    = { 32_px, 18_px };
            gcodeCopyBtn->style->margin      = { 0_px };
            gcodeCopyBtn->style->padding     = { 2_px, 2_px, 4_px, 4_px };
            Text* gcodeCopyTxt = new Text(gcodeCopyBtn, "CPY");
            gcodeCopyTxt->style->text.size  = 9_px;
            gcodeCopyTxt->style->text.color = rgba(232, 232, 232, 0.8f);
            gcodeCopyBtn->onMouseDown([this](Rev::Element::Event&) { copyToClipboard(gcodeLines); });

            gcodeLogBox = new Box(gcodeCol, { &Theme::LogBox });
            gcodeLogBox->style->size.height = Grow();  // override fixed 100_px from LogBox theme
            gcodeLogTxt = new Text(gcodeLogBox, "", { &Theme::GcodeText });
            gcodeLogTxt->style->layout.position = Position::Absolute;
            gcodeLogTxt->style->size.width      = 100_pct;
            gcodeLogTxt->selectable = true;
            gcodeLogBox->onMouseWheel([this](Rev::Element::Event& e) {
                if (!gcodeLogBox->rect.contains(e.mouse.pos)) return;
                float boxH  = gcodeLogBox->rect.h;
                float textH = gcodeLogTxt->rect.h;
                float maxS  = (std::max)(0.0f, textH - boxH);
                gcodeLogScrollY -= (e.mouse.wheel.y / 120.0f) * 20.0f;
                gcodeLogScrollY  = std::clamp(gcodeLogScrollY, 0.0f, maxS);
                gcodeLogTxt->style->position.top = Px(-gcodeLogScrollY);
                e.propagate = false;
            });
        }

        // ─────────────────────────────────────────────────────────────────────
        // Rev compute overrides
        // ─────────────────────────────────────────────────────────────────────

        void computeStyle(Rev::Element::Event& e) override {

            // Apply connection-state changes posted by background threads.
            int cs = pendingConnState.exchange(-1);
            if (cs == 1 && connStatus) {
                connStatus->content = "CONNECTED";
                connStatus->style->text.color = rgba(48, 209, 88, 1);
                if (connectBtnTxt) connectBtnTxt->content = "DISCONNECT";
            } else if (cs == 0 && connStatus) {
                connStatus->content = "DISCONNECTED";
                connStatus->style->text.color = rgba(255, 59, 48, 1);
                if (connectBtnTxt) connectBtnTxt->content = "CONNECT";
            }

            // Render the status label from the worker-set display state.
            if (frameLabel) {
                int ds = dispState.load();
                if (ds == 3) {
                    frameLabel->content = "E-STOP";
                    frameLabel->style->text.color = rgba(255, 59, 48, 1);
                } else if (ds == 2) {
                    frameLabel->content = "DONE";
                    frameLabel->style->text.color = rgba(232, 232, 232, 0.4f);
                } else if (ds == 1) {
                    frameLabel->content = "FRAME " + std::to_string(frameN.load()) +
                                          "/" + std::to_string(frameTotal.load());
                    frameLabel->style->text.color = rgba(232, 232, 232, 0.4f);
                } else {
                    frameLabel->content = "IDLE";
                    frameLabel->style->text.color = rgba(232, 232, 232, 0.4f);
                }
            }

            // During a running job, advance the preview to the frame being exposed and
            // show its expose time. (showFrame's texture upload is deferred to the next
            // render pass, so it's safe to call here on the main thread.)
            if (previewImg && previewImg->hasFrames() && dispState.load() == 1) {
                int fn = frameN.load();
                if (fn >= 1 && fn != previewLiveFrame) {
                    previewImg->showFrame(fn - 1);
                    previewLiveFrame = fn;
                    if (previewLabel) {
                        int ms = jobExposeMs.load();
                        previewLabel->content =
                            "FRAME " + std::to_string(fn) + "/" +
                            std::to_string((int)previewImg->framePaths.size()) +
                            (ms > 0 ? ("  -  expose " + std::to_string(ms) + " ms") : "");
                    }
                }
            } else {
                previewLiveFrame = -1;
            }

            // Reset the SLICE button label after the slicer worker finishes.
            if (pendingSliceDone.exchange(false) && sliceBtnTxt) {
                sliceBtnTxt->content = "SLICE";
            }

            // Re-scan the jobs directory after a successful slice (main thread only).
            if (pendingJobRescan.exchange(false)) {
                refreshJobList();
            }

            // Reload preview image with tile grid after a successful slice
            if (pendingPreviewReload.exchange(false)) {
                if (previewImg && !inputFilePath.empty()) {
                    previewImg->setGrid(pendingTileX.load(), pendingTileY.load());
                    previewImg->loadFile(inputFilePath);
                }
            }

            // Drain log queue → update displayed text
            std::string msg;
            bool runnerUpdated = false;
            while (logQ.pop(msg)) {
                logLines.push_back(msg);
                if (logLines.size() > MAX_LOG) logLines.pop_front();
                runnerUpdated = true;
            }
            // Only rebuild the text when new lines arrived — reassigning content
            // every frame marks it dirty and would clobber an active text selection.
            if (runnerUpdated && runnerLogTxt) {
                std::string joined;
                for (auto& l : logLines) { joined += l; joined += '\n'; }
                runnerLogTxt->content = joined;
            }
            // Auto-scroll runner log to bottom when new messages arrive
            if (runnerUpdated && runnerLogBox && runnerLogTxt) {
                float boxH  = runnerLogBox->rect.h;
                float textH = runnerLogTxt->rect.h;
                float maxS  = (std::max)(0.0f, textH - boxH);
                runnerLogScrollY = maxS;
                runnerLogTxt->style->position.top = Px(-runnerLogScrollY);
            }

            bool gcodeUpdated = false;
            while (gcodeQ.pop(msg)) {
                gcodeLines.push_back(msg);
                if (gcodeLines.size() > MAX_LOG) gcodeLines.pop_front();
                gcodeUpdated = true;
            }
            if (gcodeUpdated && gcodeLogTxt) {
                std::string joined;
                for (auto& l : gcodeLines) { joined += l; joined += '\n'; }
                gcodeLogTxt->content = joined;
            }
            // Auto-scroll gcode log to bottom when new messages arrive
            if (gcodeUpdated && gcodeLogBox && gcodeLogTxt) {
                float boxH  = gcodeLogBox->rect.h;
                float textH = gcodeLogTxt->rect.h;
                float maxS  = (std::max)(0.0f, textH - boxH);
                gcodeLogScrollY = maxS;
                gcodeLogTxt->style->position.top = Px(-gcodeLogScrollY);
            }

            // Progress bar
            int n = frameN.load(), tot = frameTotal.load();
            if (tot > 0 && progressFill) {
                float pct = std::clamp(100.0f * n / tot, 0.0f, 100.0f);
                progressFill->style->size.width = Pct(pct);
            }

            // Sidebar scroll: keep content clamped and update the scrollbar thumb.
            // (position.top is set immediately in the wheel handler for instant repaint;
            //  this block handles initial sizing and window-resize clamping.)
            // Measure content height from the child spread — an absolutely-positioned
            // element's own rect.h is clamped to its parent, which would zero maxScroll.
            if (sidebarBox && sidebarContent && sidebarScrollThumb) {
                float trackH   = sidebarBox->rect.h;
                float contentH = measureSpread(sidebarContent);
                if (contentH <= 0.0f) contentH = trackH;
                float maxScroll = (std::max)(0.0f, contentH - trackH);
                if (sidebarScrollY > maxScroll) sidebarScrollY = maxScroll;
                sidebarContent->style->position.top = Px(-sidebarScrollY);

                float ratio   = (contentH > trackH) ? (trackH / contentH) : 1.0f;
                float thumbH  = (std::max)(20.0f, ratio * trackH);
                float thumbTop = (maxScroll > 0.0f)
                    ? (sidebarScrollY / maxScroll) * (trackH - thumbH)
                    : 0.0f;
                sidebarScrollThumb->style->size.height     = Px(thumbH);
                sidebarScrollThumb->style->position.top    = Px(thumbTop);
                sidebarScrollThumb->style->visibility = (maxScroll > 0.0f)
                    ? Visibility::Visible : Visibility::Hidden;
            }

            // (Job list now flows in the sidebar and scrolls with it — no separate
            //  inner scroll handling needed.)

            // Sync platform dropdown → mark for row swap. Do NOT mutate the tree here;
            // structural changes during the computeStyle pass corrupt the in-progress
            // draw iteration and crash. Defer the actual swap to computeChildren.
            if (platformDrop) {
                Platform sel = (platformDrop->params.value == "pi") ? Platform::Pi : Platform::STM32;
                if (sel != platform) {
                    platform = sel;
                    platformDirty = true;
                }
            }

            Box::computeStyle(e);
        }

        void computeChildren(Rev::Element::Event& e) override {

            if (platformDirty) {
                updatePlatformRows();
                platformDirty = false;
            }

            if (jobListDirty) {
                rebuildJobList();
                jobListDirty = false;
            }

            Box::computeChildren(e);
        }

        // ─────────────────────────────────────────────────────────────────────
        // Connection
        // ─────────────────────────────────────────────────────────────────────

        void updatePlatformRows() {
            if (!platformRowSlot || !stmPortRow || !piHostRow) return;
            auto& kids = platformRowSlot->children;
            bool stmIn = std::find(kids.begin(), kids.end(), (Element*)stmPortRow) != kids.end();
            bool piIn  = std::find(kids.begin(), kids.end(), (Element*)piHostRow)  != kids.end();
            if (platform == Platform::STM32) {
                if (piIn)   platformRowSlot->removeChild(piHostRow);
                if (!stmIn) platformRowSlot->addChild(stmPortRow);
            } else {
                if (stmIn)  platformRowSlot->removeChild(stmPortRow);
                if (!piIn)  platformRowSlot->addChild(piHostRow);
            }
            // Send-to-target checkbox is Pi-only
            updateSendChkVisibility();
        }

        void toggleConnect() {
            if (connFlag) {
                doDisconnect();
            } else {
                doConnect();
            }
        }

        void doConnect() {
            if (platform == Platform::STM32) {
                std::string port = portDrop ? portDrop->params.value : settings.port;
                if (port.empty()) port = "COM3";
                logQ.push("Connecting to " + port + "...");
                std::thread([this, port]() { connectStm32(port); }).detach();
            } else {
                std::string host = hostInput ? hostInput->text->strContent : settings.piHost;
                if (host.empty()) host = settings.piHost;
                logQ.push("Connecting to " + host + ":" + std::to_string(9876) + "...");
                std::thread([this, host]() { connectPi(host, 9876); }).detach();
            }
        }

        void doDisconnect() {
            if (stmSerial) { delete stmSerial; stmSerial = nullptr; }
            if (piClient)  { piClient->disconnect(); delete piClient; piClient = nullptr; }
            onDisconnected();
        }

        void connectStm32(const std::string& port) {
            auto* ser = new Rev::Serial(port, 115200);
            if (!ser->connected()) {
                delete ser;
                logQ.push("[ERR] Failed to open " + port);
                return;
            }
            ser->sendText("PING\n");
            std::string resp = ser->readLine(3000);
            if (resp != "PONG") {
                delete ser;
                logQ.push("[ERR] PING failed: got '" + resp + "'");
                return;
            }
            stmSerial = ser;
            onConnected();   // sets pendingJobRescan → refreshJobList runs on main thread
        }

        void connectPi(const std::string& host, int port) {

            piClient = new Rev::SocketClient([this](Rev::SocketClient::Event ev) {
                if (ev.type == Rev::SocketClient::Event::Connected) {
                    onConnected();
                    piClient->sendLine("LIST_JOBS");
                } else if (ev.type == Rev::SocketClient::Event::Disconnected) {
                    onDisconnected();
                } else if (ev.type == Rev::SocketClient::Event::Error) {
                    logQ.push("[ERR] " + ev.data);
                } else if (ev.type == Rev::SocketClient::Event::Line) {
                    handlePiMessage(ev.data);
                }
            });

            if (!piClient->connect(host, port)) {
                delete piClient;
                piClient = nullptr;
            }
        }

        // NOTE: these run on background threads (serial connect / Pi socket). They must
        // NOT touch the element tree directly — doing so races the render thread and
        // intermittently corrupts layout (broken scroll, hidden sections). They only
        // set atomics + log; computeStyle applies the actual UI change on the main thread.
        void onConnected() {
            connFlag = true;
            logQ.push("Connected");
            pendingConnState = 1;
            pendingJobRescan = true;
        }

        void onDisconnected() {
            connFlag = false;
            logQ.push("Disconnected");
            pendingConnState = 0;
        }

        void handlePiMessage(const std::string& msg) {
            logQ.push("<- " + msg);

            if (msg.rfind("JOBS ", 0) == 0) {
                std::string list = msg.substr(5);
                jobs.clear();
                std::istringstream ss(list);
                std::string tok;
                while (std::getline(ss, tok, ',')) {
                    if (!tok.empty() && tok != "(none)") jobs.push_back(tok);
                }
                jobListDirty = true;
            } else if (msg.rfind("FRAME ", 0) == 0) {
                // "FRAME n/total" — set atomics only; frameLabel is rendered on the
                // main thread in computeStyle (this runs on the Pi socket thread).
                auto slash = msg.find('/');
                if (slash != std::string::npos) {
                    try {
                        frameN     = std::stoi(msg.substr(6, slash - 6));
                        frameTotal = std::stoi(msg.substr(slash + 1));
                        dispState  = 1;
                    } catch (...) {}
                }
            } else if (msg.rfind("GCODE ", 0) == 0) {
                gcodeQ.push(msg.substr(6));
            } else if (msg == "JOB_DONE") {
                dispState = 2;
                logQ.push("[OK] Job complete");
                jobState = JobState::Idle;
            } else if (msg == "JOB_PAUSED") {
                jobState = JobState::Paused;
            }
        }

        // ─────────────────────────────────────────────────────────────────────
        // Job list
        // ─────────────────────────────────────────────────────────────────────

        void refreshJobList() {
            if (platform == Platform::STM32 || !connFlag) {
                // Scan local jobs directory
                std::string dir = resolveJobsDir();
                jobs.clear();
                std::string err;
                try {
                    if (!std::filesystem::exists(dir)) {
                        err = "path does not exist";
                    } else {
                        for (auto& entry : std::filesystem::directory_iterator(dir)) {
                            if (entry.is_directory() &&
                                std::filesystem::exists(entry.path() / "manifest.json")) {
                                jobs.push_back(entry.path().filename().string());
                            }
                        }
                        std::sort(jobs.begin(), jobs.end());
                    }
                } catch (const std::exception& ex) { err = ex.what(); }
                  catch (...)                       { err = "unknown error"; }

                if (!err.empty())
                    logQ.push("[JOBS] scan failed (" + dir + "): " + err);
                else
                    logQ.push("[JOBS] " + std::to_string(jobs.size()) +
                              " job(s) in " + dir);
                jobListDirty = true;
            } else if (piClient) {
                piClient->sendLine("LIST_JOBS");
            }
        }

        // Resolve the configured jobs directory to an absolute path.
        std::string resolveJobsDir() {
            // Read the live Observable content (strContent lags a frame, so a freshly
            // typed/browsed path wouldn't be seen by an immediate refresh).
            std::string dir = jobsDirInput ? jobsDirInput->text->content.get() : "";
            if (dir.empty()) dir = settings.jobsDir;
            if (!dlpRoot.empty() && !std::filesystem::path(dir).is_absolute())
                dir = (std::filesystem::path(dlpRoot) / dir).string();
            return dir;
        }

        // Load all frames of a previously-sliced job into the preview, enabling
        // frame cycling and merge. Grid cells are parsed from job.gcode comments.
        void previewJob() {
            if (selectedJob.empty()) { logQ.push("Select a job first"); return; }
            if (!previewImg) return;
            namespace fs = std::filesystem;

            fs::path jobDir = fs::path(resolveJobsDir()) / selectedJob;

            // Collect frame PNGs in numeric order (00001.png, 00002.png, ...)
            std::vector<std::string> frames;
            for (int i = 1; ; i++) {
                char name[16];
                std::snprintf(name, sizeof(name), "%05d.png", i);
                fs::path p = jobDir / name;
                if (!fs::exists(p)) break;
                frames.push_back(p.string());
            }
            if (frames.empty()) {
                logQ.push("[ERR] No frames in job " + selectedJob);
                return;
            }

            // Parse job.gcode for "col=X row=Y" comments to recover the tile grid.
            std::vector<std::pair<int,int>> cells;
            int maxCol = 0, maxRow = 0;
            std::ifstream gc((jobDir / "job.gcode").string());
            std::string line;
            while (std::getline(gc, line)) {
                auto cp = line.find("col=");
                auto rp = line.find("row=");
                if (cp == std::string::npos || rp == std::string::npos) continue;
                int col = std::atoi(line.c_str() + cp + 4);
                int row = std::atoi(line.c_str() + rp + 4);
                cells.push_back({ col, row });
                maxCol = (std::max)(maxCol, col);
                maxRow = (std::max)(maxRow, row);
            }
            int gCols = cells.empty() ? 1 : maxCol + 1;
            int gRows = cells.empty() ? (int)frames.size() : maxRow + 1;

            previewImg->setFrames(frames, cells, gCols, gRows);
            updatePreviewLabel();
            logQ.push("Preview: " + selectedJob + " (" + std::to_string(frames.size()) +
                      " frames, " + std::to_string(gCols) + "x" + std::to_string(gRows) + ")");
        }

        // Read expose_ms from a job's manifest into jobExposeMs (for the status line).
        void loadJobExpose(const std::string& jobName) {
            std::ifstream mf((std::filesystem::path(resolveJobsDir()) / jobName / "manifest.json").string());
            if (mf) {
                std::string j((std::istreambuf_iterator<char>(mf)), {});
                jobExposeMs = jsonInt(j, "expose_ms");
            }
        }

        // Update the "frame N/M" label above the preview area.
        void updatePreviewLabel() {
            if (!previewLabel || !previewImg) return;
            if (!previewImg->hasFrames()) { previewLabel->content = ""; return; }
            if (previewImg->merged) {
                previewLabel->content = "MERGED " + std::to_string(previewImg->gridCols) +
                                        "x" + std::to_string(previewImg->gridRows);
            } else {
                previewLabel->content = "FRAME " + std::to_string(previewImg->frameIdx + 1) +
                                        "/" + std::to_string((int)previewImg->framePaths.size());
            }
        }

        void rebuildJobList() {
            Box* parent = jobListContent ? jobListContent : jobListBox;
            if (!parent) return;
            for (auto* b : jobItemBoxes) delete b;
            jobItemBoxes.clear();

            for (auto& j : jobs) {
                Box* item = new Box(parent, { &Theme::JobItem, &Theme::JobItemHover });
                item->style->layout = { Axis::Horizontal, Align::Start, Align::Center };

                bool isSelected = (j == selectedJob);
                if (isSelected) item->styles.add(&Theme::JobItemSelected);

                Text* t = new Text(item, j);
                t->style->text.color = rgba(232, 232, 232, 1);
                t->style->text.size  = 11_px;

                item->onMouseDown([this, j, item](Rev::Element::Event& e) {
                    selectedJob = j;
                    // Refresh selection styling
                    for (auto* b : jobItemBoxes) b->styles.remove(&Theme::JobItemSelected);
                    item->styles.add(&Theme::JobItemSelected);
                });

                jobItemBoxes.push_back(item);
            }
        }

        // ─────────────────────────────────────────────────────────────────────
        // Job execution
        // ─────────────────────────────────────────────────────────────────────

        void startJob() {
            if (estopped) { logQ.push("E-STOP active — press RESET first"); return; }
            // A previous job thread may still be unwinding (e.g. blocked in a gantry
            // readLine after an E-STOP). Never join() it on the UI thread — that's what
            // froze the program. Refuse until it has actually finished.
            if (jobRunning) {
                logQ.push("Previous job still stopping — wait for it to finish");
                return;
            }
            if (selectedJob.empty()) { logQ.push("Select a job first"); return; }

            // Load the job's frames into the preview so the running frame can be
            // highlighted live, and read its expose time for the status readout.
            loadJobExpose(selectedJob);
            previewLiveFrame = -1;
            previewJob();

            if (platform == Platform::Pi) {
                if (!piClient) { logQ.push("Not connected to Pi"); return; }
                piClient->sendLine("START_JOB " + selectedJob);
                jobState  = JobState::Running;
                dispState = 1;
                logQ.push("Starting (Pi): " + selectedJob);
                return;
            }

            // STM32 path — execute in background thread
            if (!stmSerial) { logQ.push("Not connected to STM32"); return; }
            abortFlag = false;
            jobState  = JobState::Running;
            dispState = 1;

            // Use the SAME resolved (absolute) directory the queue scans, not the raw
            // "./jobs" text — otherwise the job path is relative to the EXE's CWD and
            // the manifest can't be found.
            std::string dir     = resolveJobsDir();
            std::string jobName = selectedJob;

            if (jobThread.joinable()) jobThread.join();
            jobThread = std::thread([this, dir, jobName]() {
                runStm32Job(dir + "/" + jobName);
            });
            // Win32 thread handle so E-STOP can cancel a blocking serial read on it.
            jobThreadHandle = (HANDLE)jobThread.native_handle();
        }

        void pauseJob() {
            if (platform == Platform::Pi && piClient) {
                if (jobState == JobState::Running) {
                    piClient->sendLine("PAUSE");
                    logQ.push("-> PAUSE");
                } else if (jobState == JobState::Paused) {
                    piClient->sendLine("RESUME");
                    logQ.push("-> RESUME");
                }
            }
        }

        void abortJob() {
            abortFlag = true;
            logQ.push("ABORT sent");
            jobState = JobState::Idle;
            // Unblock the job/jog thread's pending (synchronous) read so it stops promptly.
            if (jobRunning && jobThreadHandle) CancelSynchronousIo(jobThreadHandle);
            if (HANDLE gh = (HANDLE)gantryThreadHandle.load()) CancelSynchronousIo(gh);
            if (stmSerial) stmSerial->cancel();
            // Offload the write so a blocking/contended serial port can't freeze the UI.
            std::thread([this]() {
                if (platform == Platform::Pi && piClient) {
                    piClient->sendLine("ABORT");
                } else if (stmSerial) {
                    stmSerial->sendText("BLANK\n");
                }
            }).detach();
        }

        // STM32 job runner (runs on jobThread)
        void runStm32Job(const std::string& jobPath) {

            // Mark the job as running for the whole duration; clear on every exit path
            // so startJob can tell whether the previous thread has actually finished
            // (it may still be blocked in a serial readLine after an E-STOP).
            jobRunning = true;
            struct RunGuard { std::atomic<bool>& f; ~RunGuard() { f = false; } } runGuard{ jobRunning };

            // Load manifest
            std::ifstream mf(jobPath + "/manifest.json");
            if (!mf) { logQ.push("[ERR] Cannot open manifest: " + jobPath); return; }
            std::string mjson((std::istreambuf_iterator<char>(mf)), {});

            int nFrames  = jsonInt(mjson, "frames");
            int exposeMs = jsonInt(mjson, "expose_ms");

            if (nFrames <= 0) { logQ.push("[ERR] Bad manifest"); return; }

            // Load gcode
            std::vector<std::string> gcodeLines = readGcodeFile(jobPath + "/job.gcode");
            if ((int)gcodeLines.size() != nFrames) {
                logQ.push("[ERR] Gcode lines " + std::to_string(gcodeLines.size()) +
                          " != frames " + std::to_string(nFrames));
                return;
            }

            frameTotal = nFrames;
            logQ.push("JOB_START " + std::filesystem::path(jobPath).filename().string());

            for (int i = 0; i < nFrames; i++) {

                if (abortFlag) {
                    stmSerial->sendText("BLANK\n");
                    logQ.push("ERROR: aborted");
                    jobState = JobState::Idle;
                    if (!estopped) dispState = 0;
                    return;
                }

                frameN    = i + 1;
                dispState = 1;
                logQ.push("FRAME " + std::to_string(i + 1) + "/" + std::to_string(nFrames));

                std::string gline = gcodeLines[i];
                gcodeQ.push(gline);

                // Move gantry
                if (!stmSendWait("GANTRY " + gline + "\n", "OK", 60000)) return;
                if (!stmSendWait("GANTRY G4 P0\n", "OK", 10000)) return;

                // Load frame bitmap
                char frameName[16];
                std::snprintf(frameName, sizeof(frameName), "%05d.png", i + 1);
                std::vector<uint8_t> bitmap = pngToBitmap(jobPath + "/" + frameName);
                if (bitmap.size() != 28800) {
                    logQ.push("[ERR] Frame " + std::to_string(i + 1) + ": bad bitmap");
                    return;
                }

                uint8_t checksum = 0;
                for (uint8_t b : bitmap) checksum ^= b;

                // Pattern upload + exposure as one atomic serial transaction.
                {
                    std::lock_guard<std::mutex> lk(serialMtx);

                    stmSerial->sendText("PATTERN\n");
                    std::string r = stmSerial->readLine(6000);
                    if (r != "READY") { logQ.push("[ERR] PATTERN: " + r); return; }

                    stmSerial->sendBytes(bitmap.data(), bitmap.size());
                    stmSerial->sendByte(checksum);
                    r = stmSerial->readLine(6000);
                    if (r != "OK") { logQ.push("[ERR] Bitmap recv: " + r); return; }

                    stmSerial->sendText("EXPOSE " + std::to_string(exposeMs) + "\n");
                    r = stmSerial->readLine(6000);
                    if (r != "EXPOSING") { logQ.push("[ERR] EXPOSE start: " + r); return; }

                    r = stmSerial->readLine(exposeMs + 3000);
                    if (r != "DONE" && r != "ABORTED") {
                        logQ.push("[ERR] Expose finish: " + r);
                        return;
                    }
                }
            }

            logQ.push("JOB_DONE");
            // Marshal the label/progress to the main thread (this is the job thread).
            frameN    = frameTotal.load();
            dispState = 2;
            jobState  = JobState::Idle;
        }

        // ─────────────────────────────────────────────────────────────────────
        // Jog
        // ─────────────────────────────────────────────────────────────────────

        // Run a gantry command on a detached thread while publishing its Win32 handle
        // so E-STOP can CancelSynchronousIo() the blocking serial read it's parked on.
        void runGantryOp(std::function<void()> op) {
            std::thread([this, op]() {
                HANDLE h = nullptr;
                DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                                GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS);
                gantryThreadHandle.store(h);
                op();
                gantryThreadHandle.store(nullptr);
                if (h) CloseHandle(h);
            }).detach();
        }

        void jog(const std::string& axis, int dir) {
            if (!connFlag) { logQ.push("Not connected"); return; }
            if (estopped)  { logQ.push("E-STOP active — press RESET first"); return; }

            float dist  = jogDistInput ? parseFloat(jogDistInput->text->strContent, 1.0f) : 1.0f;
            float feed  = settings.feedRate;
            char buf[64];
            // No leading '+' (GRBL dislikes it); sign carried by the value.
            std::snprintf(buf, sizeof(buf), "%s%.3f F%.0f", axis.c_str(), dist * dir, feed);
            std::string cmd = buf;

            logQ.push("-> JOG " + cmd);
            runGantryOp([this, cmd]() {
                if (platform == Platform::STM32 && stmSerial) {
                    // Bounded RELATIVE move so the gantry travels exactly `dist` and
                    // stops — an absolute G1 could run to a far coordinate. Restore
                    // absolute mode afterwards regardless of outcome.
                    if (!stmSendWait("GANTRY G91\n", "OK", 5000)) return;
                    bool ok = stmSendWait("GANTRY G1 " + cmd + "\n", "OK", 30000);
                    stmSendWait("GANTRY G90\n", "OK", 5000);
                    if (ok) logQ.push("JOG_DONE");
                } else if (piClient) {
                    piClient->sendLine("JOG " + cmd);
                }
            });
        }

        void sendHome() {
            if (!connFlag) { logQ.push("Not connected"); return; }
            if (estopped)  { logQ.push("E-STOP active — press RESET first"); return; }
            logQ.push("-> HOME");
            runGantryOp([this]() {
                if (platform == Platform::STM32 && stmSerial) {
                    if (!stmSendWait("GANTRY G28\n", "OK", 60000)) return;
                    if (abortFlag) return;
                    stmSendWait("GANTRY G4 P0\n", "OK", 10000);
                    logQ.push("JOG_DONE");
                } else if (piClient) {
                    piClient->sendLine("JOG HOME");
                }
            });
        }

        void sendEstop() {
            logQ.push("[!!!] E-STOP SENT");
            estopped  = true;
            abortFlag = true;

            // Abort any in-flight serial read so a job thread blocked waiting on a
            // gantry response returns immediately — the job loop then stops at once
            // instead of waiting out the device's move timeout. The serial handle is
            // synchronous, so the read must be cancelled on its OWNING thread via
            // CancelSynchronousIo; PurgeComm/CancelIoEx only cover overlapped I/O.
            if (jobRunning && jobThreadHandle) CancelSynchronousIo(jobThreadHandle);
            if (HANDLE gh = (HANDLE)gantryThreadHandle.load()) CancelSynchronousIo(gh);
            if (stmSerial) stmSerial->cancel();

            dispState = 3;          // computeStyle renders the E-STOP label
            jobState  = JobState::Idle;

            // ...but issue the serial/socket write on a detached thread. A synchronous
            // WriteFile with no write timeout (or one contended by the running job
            // thread) would otherwise block the UI thread and freeze the program.
            // This is an emergency write — it deliberately skips the serial mutex.
            std::thread([this]() {
                if (platform == Platform::STM32 && stmSerial) {
                    // Bare realtime bytes: the firmware relays these straight to the
                    // gantry even mid-command (see GANTRY wait-loop E-STOP relay).
                    // '!' = feed-hold, 0x18 = soft-reset (immediate halt).
                    stmSerial->sendByte('!');
                    stmSerial->sendByte(0x18);
                } else if (piClient) {
                    piClient->sendLine("ESTOP");
                }
            }).detach();
        }

        void sendReset() {
            logQ.push("-> RESET");

            // Clear the latched E-STOP and restore the status label.
            estopped  = false;
            abortFlag = false;
            frameN    = 0;
            frameTotal= 0;
            dispState = 0;          // computeStyle renders the IDLE label
            jobState  = JobState::Idle;

            std::thread([this]() {
                if (platform == Platform::STM32 && stmSerial) {
                    std::lock_guard<std::mutex> lk(serialMtx);
                    stmSerial->sendText("BLANK\n");
                    // Clear FluidNC's alarm/lock left by an E-STOP soft-reset, then
                    // restore absolute mode so subsequent jogs/jobs behave.
                    stmSerial->sendText("GANTRY $X\n");
                    stmSerial->sendText("GANTRY G90\n");
                    logQ.push("READY");
                } else if (piClient) {
                    piClient->sendLine("RESET");
                }
            }).detach();
        }

        void sendSetHome() {
            if (!connFlag) { logQ.push("Not connected"); return; }
            if (estopped)  { logQ.push("E-STOP active — press RESET first"); return; }
            logQ.push("-> SET_HOME");
            std::thread([this]() {
                if (platform == Platform::STM32 && stmSerial) {
                    if (stmSendWait("GANTRY G10 L20 P1 X0 Y0\n", "OK", 10000) &&
                        stmSendWait("GANTRY G28.1\n", "OK", 10000)) {
                        logQ.push("HOME_SET");
                    }
                } else if (piClient) {
                    piClient->sendLine("SET_HOME");
                }
            }).detach();
        }

        void scanPorts(bool autoSelect = true) {
            static const GUID PORTS_GUID = {
                0x4d36e978, 0xe325, 0x11ce,
                { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 }
            };

            HDEVINFO devInfo = SetupDiGetClassDevsA(&PORTS_GUID, nullptr, nullptr, DIGCF_PRESENT);
            if (devInfo == INVALID_HANDLE_VALUE) {
                logQ.push("[SCAN] SetupDi failed");
                return;
            }

            SP_DEVINFO_DATA devData{};
            devData.cbSize = sizeof(devData);
            std::vector<Dropdown::Option> found;

            for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); ++i) {

                char friendlyName[256] = {};
                SetupDiGetDeviceRegistryPropertyA(devInfo, &devData, SPDRP_FRIENDLYNAME,
                    nullptr, (PBYTE)friendlyName, sizeof(friendlyName), nullptr);

                char portName[32] = {};
                HKEY hKey = SetupDiOpenDevRegKey(devInfo, &devData,
                    DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
                if (hKey != INVALID_HANDLE_VALUE) {
                    DWORD portLen = sizeof(portName);
                    RegQueryValueExA(hKey, "PortName", nullptr, nullptr,
                        (LPBYTE)portName, &portLen);
                    RegCloseKey(hKey);
                }

                if (portName[0] == '\0' || strncmp(portName, "COM", 3) != 0) continue;

                std::string friendly(friendlyName);
                // Strip the redundant " (COMx)" suffix that Windows appends
                std::string suffix = " (" + std::string(portName) + ")";
                auto spos = friendly.rfind(suffix);
                if (spos != std::string::npos) friendly.erase(spos);

                std::string label;
                if (!friendly.empty())
                    label = std::string(portName) + " \xe2\x80\x94 " + friendly;
                else
                    label = std::string(portName);

                found.push_back({ label, std::string(portName) });
                logQ.push("[SCAN] " + label);
            }

            SetupDiDestroyDeviceInfoList(devInfo);

            if (found.empty()) {
                logQ.push("[SCAN] No COM ports found");
                return;
            }

            // Populate dropdown. Only auto-select when the user pressed SCAN — on the
            // startup scan we list ports but leave the selection empty (no connect).
            if (portDrop) {
                portDrop->params.options = found;
                if (autoSelect) {
                    // Prefer the previously-saved port if it's still present.
                    std::string pick = found[0].value;
                    for (auto& o : found) if (o.value == settings.port) { pick = o.value; break; }
                    portDrop->params.value = pick;
                }
                portDrop->styles.dirty   = true;
            }
        }

        // ─────────────────────────────────────────────────────────────────────
        // Slicer
        // ─────────────────────────────────────────────────────────────────────

        void browseFile() {
            OPENFILENAMEA ofn{};
            char path[MAX_PATH] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.svg\0All Files\0*.*\0";
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                inputFilePath = path;
                dlpRoot = findDLPRoot(path);

                std::string fname = std::filesystem::path(path).filename().string();
                if (fileLabel) {
                    fileLabel->content = fname;
                    fileLabel->style->text.color = rgba(232, 232, 232, 1);
                }
                // Auto-fill job name from filename stem
                if (jobNameInput) {
                    std::string stem = std::filesystem::path(path).stem().string();
                    std::replace(stem.begin(), stem.end(), ' ', '_');
                    jobNameInput->text->content = stem;
                }
                // Load image into preview area (no tile grid until sliced)
                if (previewImg) {
                    previewImg->setGrid(0, 0);
                    previewImg->loadFile(path);
                }

                // Now that we know the repo root, point the jobs dir at <root>/jobs
                // (where the slicer writes) if it's still the relative default, then
                // refresh so the queue shows existing jobs without manual setup.
                if (!dlpRoot.empty() && jobsDirInput) {
                    std::string cur = jobsDirInput->text->content.get();
                    if (cur.empty() || cur == "./jobs" || cur == ".\\jobs") {
                        std::string abs = (std::filesystem::path(dlpRoot) / "jobs").string();
                        jobsDirInput->text->content = abs;
                        settings.jobsDir = abs;
                    }
                }
                // Persist now (repo root + jobs dir) so they survive even if a later
                // crash skips the destructor's save.
                settings.dlpRoot = dlpRoot;
                saveSettings();
                refreshJobList();

                logQ.push("Loaded: " + fname);
            }
        }

        void browseJobsDir() {
            BROWSEINFOA bi{};
            char path[MAX_PATH] = {};
            bi.pszDisplayName = path;
            bi.lpszTitle      = "Select jobs folder";
            bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
            LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
            if (pidl) {
                SHGetPathFromIDListA(pidl, path);
                CoTaskMemFree(pidl);
                if (jobsDirInput) jobsDirInput->text->content = path;
                settings.jobsDir = path;
                saveSettings();
                refreshJobList();
            }
        }

        void startSlice() {
            if (inputFilePath.empty()) { logQ.push("No file selected"); return; }

            std::string expose = exposeMsInput  ? exposeMsInput->text->strContent  : std::to_string(settings.exposeMs);
            std::string feed   = feedRateInput  ? feedRateInput->text->strContent   : fmtFloat(settings.feedRate);
            std::string thresh = thresholdInput ? thresholdInput->text->strContent  : std::to_string(settings.threshold);
            std::string projW  = projWInput     ? projWInput->text->strContent      : fmtFloat(settings.projW);
            std::string projH  = projHInput     ? projHInput->text->strContent      : fmtFloat(settings.projH);
            std::string over   = overlapInput   ? overlapInput->text->strContent    : fmtFloat(settings.overlap);
            std::string jname  = jobNameInput   ? jobNameInput->text->strContent    : "";
            // Slice into the SAME directory the job queue scans, so the new job shows
            // up there immediately after slicing.
            std::string outDir = resolveJobsDir();
            bool inv           = invertChk      ? (bool)invertChk->value            : settings.invert;

            // Only send to the Pi when explicitly enabled AND on the Pi platform.
            // The STM32 path loads jobs locally and never SCPs anything.
            bool sendToTarget  = sendTargetChk ? (bool)sendTargetChk->value : false;
            sendToTarget       = sendToTarget && (platform == Platform::Pi);
            std::string piHost = hostInput ? hostInput->text->strContent : settings.piHost;
            if (piHost.empty()) piHost = settings.piHost;

            if (sliceBtnTxt) sliceBtnTxt->content = "SLICING...";
            logQ.push("Slicing " + std::filesystem::path(inputFilePath).filename().string() + "...");

            std::thread([this, expose, feed, thresh, projW, projH, over, jname, outDir, inv, sendToTarget, piHost]() {
                runSlicerSubprocess(inputFilePath, expose, feed, thresh, projW, projH, over, jname, outDir, inv, sendToTarget, piHost);
            }).detach();
        }

        void runSlicerSubprocess(
            const std::string& inFile,
            const std::string& exposeMs,
            const std::string& feedRate,
            const std::string& threshold,
            const std::string& projW,
            const std::string& projH,
            const std::string& overlap,
            const std::string& jobName,
            const std::string& outDir,
            bool invert,
            bool sendToTarget,
            const std::string& piHost)
        {
            namespace fs = std::filesystem;

            // Find the DLP-photolithography repo root by walking up from the input file.
            std::string repoRoot = findDLPRoot(inFile);

            // Build absolute path to slicer.py
            std::string slicerScript = repoRoot.empty()
                ? "pc/slicer.py"
                : (repoRoot + "/pc/slicer.py");

            // Resolve outDir to an absolute path
            std::string absOutDir = outDir.empty() ? "jobs" : outDir;
            if (!fs::path(absOutDir).is_absolute() && !repoRoot.empty())
                absOutDir = (fs::path(repoRoot) / absOutDir).string();
            // Strip trailing backslashes — a trailing \ before the closing " would
            // be interpreted as an escaped quote by Windows command-line parsing,
            // causing argparse to absorb subsequent arguments into the path value.
            while (!absOutDir.empty() &&
                   (absOutDir.back() == '\\' || absOutDir.back() == '/'))
                absOutDir.pop_back();

            std::string cmd = "python \"" + slicerScript + "\" \"" + inFile + "\""
                " --expose-ms " + exposeMs +
                " --feed-rate " + feedRate +
                " --threshold " + threshold +
                " --output-dir \"" + absOutDir + "\"";
            if (!jobName.empty())   cmd += " --job-name \"" + jobName + "\"";
            if (invert)             cmd += " --invert";

            // slicer.py SCPs the job to the Pi by default — suppress that unless the
            // user explicitly enabled "send to target" (Pi platform only).
            if (sendToTarget) {
                cmd += " --pi-host " + piHost;
                logQ.push("Will send job to " + piHost + " after slicing");
            } else {
                cmd += " --no-send";
            }

            SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
            HANDLE hR, hW;
            CreatePipe(&hR, &hW, &sa, 0);
            SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si{};
            si.cb           = sizeof(si);
            si.hStdOutput   = hW;
            si.hStdError    = hW;
            si.dwFlags      = STARTF_USESTDHANDLES;

            // Set subprocess CWD to repoRoot so relative paths inside the slicer work
            const char* cwd = repoRoot.empty() ? nullptr : repoRoot.c_str();

            PROCESS_INFORMATION pi{};
            BOOL ok = CreateProcessA(nullptr, (LPSTR)cmd.c_str(),
                                     nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW, nullptr, cwd, &si, &pi);
            CloseHandle(hW);

            if (!ok) {
                CloseHandle(hR);
                logQ.push("[ERR] Failed to start slicer — is Python in PATH?");
                pendingSliceDone = true;   // reset SLICE label on main thread
                return;
            }

            char buf[256];
            DWORD rd;
            int parsedCols = 0, parsedRows = 0;
            while (ReadFile(hR, buf, sizeof(buf) - 1, &rd, nullptr) && rd > 0) {
                buf[rd] = '\0';
                std::string chunk(buf, rd);
                std::istringstream ss(chunk);
                std::string line;
                while (std::getline(ss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) {
                        logQ.push(line);   // slicer.py already prefixes "[SLICER]"
                        // Parse "Tiles:  2 cols x 2 rows = N frames"
                        auto tpos = line.find("Tiles:");
                        if (tpos != std::string::npos) {
                            int c = 0, r = 0;
                            if (std::sscanf(line.c_str() + tpos + 6, " %d cols x %d rows", &c, &r) == 2) {
                                parsedCols = c;
                                parsedRows = r;
                            }
                        }
                    }
                }
            }

            DWORD exitCode = 1;
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(hR);

            if (exitCode == 0) {
                logQ.push("[OK] Slice complete");
                // Re-scan the jobs directory on the main thread (refreshJobList reads
                // UI state, so it must not run on this worker thread).
                pendingJobRescan = true;
                // Signal main thread to reload preview with tile grid
                pendingTileX       = parsedCols;
                pendingTileY       = parsedRows;
                pendingPreviewReload = true;
            } else {
                logQ.push("[ERR] Slicer exited with code " + std::to_string(exitCode));
            }

            pendingSliceDone = true;   // reset SLICE label on the main thread
        }

        // ─────────────────────────────────────────────────────────────────────
        // Settings persistence (INI file in %APPDATA%)
        // ─────────────────────────────────────────────────────────────────────

        std::string settingsPath() {
            char ap[MAX_PATH];
            SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, ap);
            std::string dir = std::string(ap) + "\\LithoControl";
            CreateDirectoryA(dir.c_str(), nullptr);
            return dir + "\\settings.ini";
        }

        void loadSettings() {
            std::string ini = settingsPath();
            auto gi = [&](const char* k, int def) -> int {
                return GetPrivateProfileIntA("LithoControl", k, def, ini.c_str());
            };
            auto gs = [&](const char* k, const char* def) -> std::string {
                char buf[256];
                GetPrivateProfileStringA("LithoControl", k, def, buf, sizeof(buf), ini.c_str());
                return buf;
            };
            settings.exposeMs  = gi("exposeMs",  3000);
            settings.feedRate  = (float)gi("feedRateX10", 200) / 10.0f;
            settings.threshold = gi("threshold",  128);
            settings.projW     = (float)gi("projWX1000", 1600) / 1000.0f;
            settings.projH     = (float)gi("projHX1000",  900) / 1000.0f;
            settings.overlap   = (float)gi("overlapX100",   0) / 100.0f;
            settings.invert    = (bool)gi("invert", 0);
            settings.sendAfterSlice = (bool)gi("sendAfterSlice", 0);
            settings.port      = gs("port",     "COM3");
            settings.piHost    = gs("piHost",   "192.168.1.240");
            settings.jobsDir   = gs("jobsDir",  "./jobs");
            settings.platform  = gs("platform", "stm32");
            settings.dlpRoot   = gs("dlpRoot",  "");
        }

        void saveSettings() {
            std::string ini = settingsPath();
            auto wi = [&](const char* k, int v) {
                WritePrivateProfileStringA("LithoControl", k, std::to_string(v).c_str(), ini.c_str());
            };
            auto ws = [&](const char* k, const std::string& v) {
                WritePrivateProfileStringA("LithoControl", k, v.c_str(), ini.c_str());
            };
            wi("exposeMs",     settings.exposeMs);
            wi("feedRateX10",  (int)(settings.feedRate  * 10));
            wi("threshold",    settings.threshold);
            wi("projWX1000",   (int)(settings.projW     * 1000));
            wi("projHX1000",   (int)(settings.projH     * 1000));
            wi("overlapX100",  (int)(settings.overlap   * 100));
            wi("invert",       settings.invert ? 1 : 0);
            wi("sendAfterSlice", (sendTargetChk ? (bool)sendTargetChk->value : settings.sendAfterSlice) ? 1 : 0);
            ws("port",         portDrop ? portDrop->params.value : settings.port);
            ws("piHost",       hostInput  ? hostInput->text->content.get()  : settings.piHost);
            ws("jobsDir",      jobsDirInput ? jobsDirInput->text->content.get() : settings.jobsDir);
            ws("platform",     platform == Platform::Pi ? "pi" : "stm32");
            ws("dlpRoot",      dlpRoot);
        }

        // ─────────────────────────────────────────────────────────────────────
        // Helpers
        // ─────────────────────────────────────────────────────────────────────

        // Create a labelled TextInput with dark-theme label colour
        TextInput* makeInput(Box* parent, const std::string& lbl, const std::string& val, size_t maxLen) {
            auto* inp = new TextInput(parent, { .label = lbl, .placeholder = "", .maxLength = maxLen });
            if (!val.empty()) inp->text->content = val;
            inp->label->style->text.color = rgba(232, 232, 232, 0.6f);
            inp->label->style->text.size  = 9_px;
            tightenInput(inp);
            return inp;
        }

        // Give the field a fixed compact height with horizontal-only padding, so it
        // can't grow vertically during relayout. LrtbStyle order is L,R,T,B; the
        // container centers its text vertically within the fixed height.
        static void tightenInput(TextInput* inp) {
            if (!inp) return;
            if (inp->container) {
                inp->container->style->padding    = { 8_px, 8_px, 0_px, 0_px };
                inp->container->style->size.height     = 28_px;
                inp->container->style->size.min.height = 28_px;
                inp->container->style->size.max.height = 28_px;
                // Left-align contents and clip overflowing text at the box edge
                // instead of wrapping/growing the field.
                inp->container->style->layout.horizontal = Align::Start;
                inp->container->style->overflow          = Overflow::Hide;
            }
            if (inp->text) {
                inp->text->style->text.wrap = Wrap::False;   // single line, clipped
                // Keep the editable text element at full width (TextInput default
                // min-width:100%) so clicking anywhere in the field focuses it.
                // Left alignment comes from the container's Align::Start above.
            }
            if (inp->placeholder) {
                inp->placeholder->style->text.wrap = Wrap::False;
            }
            inp->style->margin = { 3_px, 3_px, 2_px, 2_px };
        }

        // Create a button Box
        Box* makeBtn(Box* parent, const std::string& label,
                     std::function<void()> cb,
                     bool accent = false, bool danger = false,
                     bool success = false, bool warning = false)
        {
            Style* base = &Theme::Btn;
            if (accent)  base = &Theme::BtnAccent;
            if (danger)  base = &Theme::BtnDanger;
            if (success) base = &Theme::BtnSuccess;
            if (warning) base = &Theme::BtnWarning;

            Box* btn = new Box(parent, accent ? StyleList{ &Theme::BtnAccent, &Theme::BtnAccentHover }
                                              : StyleList{ &Theme::Btn, &Theme::BtnHover });
            (void)base;

            Text* t = new Text(btn, label);
            t->style->text.color = (accent || danger || success || warning)
                                   ? rgba(255, 255, 255, 1)
                                   : rgba(232, 232, 232, 1);
            t->style->text.size  = 11_px;

            if (cb) {
                btn->onMouseDown([cb](Rev::Element::Event&) { cb(); });
            }
            return btn;
        }

        // Create a fixed-size jog D-pad button
        Box* makeJogBtn(Box* parent, const std::string& label, std::function<void()> cb) {
            Box* btn = new Box(parent, { &Theme::Btn, &Theme::BtnHover });
            btn->style->size      = { 55_px, 28_px };
            btn->style->size.max  = { 55_px, 28_px };
            btn->style->layout    = { Axis::Horizontal, Align::Center, Align::Center };

            Text* t = new Text(btn, label);
            t->style->text.color = rgba(232, 232, 232, 1);
            t->style->text.size  = 11_px;

            if (cb) btn->onMouseDown([cb](Rev::Element::Event&) { cb(); });
            return btn;
        }

        // Send serial command, wait for exact response token
        bool stmSendWait(const std::string& cmd, const std::string& expect, int timeoutMs) {
            if (!stmSerial) return false;
            // Hold the serial mutex for the whole send→reply so a concurrent jog or
            // job-frame transaction can't interleave bytes and garble the response.
            std::lock_guard<std::mutex> lk(serialMtx);
            stmSerial->sendText(cmd);
            std::string r = stmSerial->readLine(timeoutMs);
            if (r != expect) {
                logQ.push("[ERR] '" + cmd.substr(0, 20) + "' got: " + r);
                return false;
            }
            return true;
        }

        // PNG file → 640×360 1bpp bitmap (28 800 bytes) using GDI+
        std::vector<uint8_t> pngToBitmap(const std::string& path) {
            const int W = 640, H = 360, BYTES = W * H / 8;
            std::wstring wpath(path.begin(), path.end());
            Gdiplus::Bitmap bmp(wpath.c_str());

            // If wrong size, scale it
            Gdiplus::Bitmap* src = &bmp;
            Gdiplus::Bitmap* scaled = nullptr;
            if ((int)bmp.GetWidth() != W || (int)bmp.GetHeight() != H) {
                scaled = new Gdiplus::Bitmap(W, H);
                Gdiplus::Graphics g(scaled);
                g.DrawImage(&bmp, 0, 0, W, H);
                src = scaled;
            }

            std::vector<uint8_t> result(BYTES, 0);
            Gdiplus::BitmapData bd;
            Gdiplus::Rect rect(0, 0, W, H);
            src->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppRGB, &bd);

            auto* px = (uint8_t*)bd.Scan0;
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    uint8_t* p = px + y * bd.Stride + x * 4;
                    // GDI+ 32bppRGB: B,G,R stored at [0],[1],[2]
                    uint8_t gray = (uint8_t)(0.299f * p[2] + 0.587f * p[1] + 0.114f * p[0]);
                    if (gray >= 128) {
                        int i = y * W + x;
                        result[i >> 3] |= (1 << (7 - (i & 7)));
                    }
                }
            }

            src->UnlockBits(&bd);
            if (scaled) delete scaled;
            return result;
        }

        // Minimal JSON integer extraction (no external JSON lib needed)
        static int jsonInt(const std::string& json, const std::string& key) {
            auto pos = json.find("\"" + key + "\"");
            if (pos == std::string::npos) return 0;
            pos = json.find(':', pos);
            if (pos == std::string::npos) return 0;
            pos++;
            while (pos < json.size() && std::isspace((unsigned char)json[pos])) pos++;
            try { return std::stoi(json.substr(pos)); } catch (...) { return 0; }
        }

        // Read job.gcode, strip comments and blank lines
        static std::vector<std::string> readGcodeFile(const std::string& path) {
            std::ifstream f(path);
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(f, line)) {
                auto semi = line.find(';');
                if (semi != std::string::npos) line = line.substr(0, semi);
                while (!line.empty() && std::isspace((unsigned char)line.back()))  line.pop_back();
                while (!line.empty() && std::isspace((unsigned char)line.front())) line.erase(line.begin());
                if (!line.empty()) lines.push_back(line);
            }
            return lines;
        }

        // Walk up the input file's directory tree looking for pc/slicer.py.
        // Returns the repo root if found, empty string otherwise.
        static std::string findDLPRoot(const std::string& inputFile) {
            namespace fs = std::filesystem;
            fs::path p(inputFile);
            if (p.has_parent_path()) p = p.parent_path();
            while (true) {
                if (fs::exists(p / "pc" / "slicer.py")) return p.string();
                auto parent = p.parent_path();
                if (parent == p) break;
                p = parent;
            }
            return "";
        }

        static std::string fmtFloat(float v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.4g", (double)v);
            return buf;
        }

        static float parseFloat(const std::string& s, float def) {
            try { return std::stof(s); } catch (...) { return def; }
        }

        // Measure the laid-out height of a container by the vertical spread of its
        // children. Robust for absolutely-positioned content whose own rect.h is
        // clamped to the parent (which would otherwise report a too-small height).
        static float measureSpread(Box* container) {
            if (!container || container->children.empty()) return 0.0f;
            float top = 1e9f, bot = -1e9f;
            for (Element* c : container->children) {
                top = (std::min)(top, c->rect.y);
                bot = (std::max)(bot, c->rect.y + c->rect.h);
            }
            return (bot > top) ? (bot - top) : 0.0f;
        }

        void copyToClipboard(const std::deque<std::string>& lines) {
            std::string text;
            for (auto& l : lines) { text += l; text += '\n'; }
            if (!OpenClipboard(nullptr)) return;
            EmptyClipboard();
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
            if (hMem) {
                auto* dst = static_cast<char*>(GlobalLock(hMem));
                std::memcpy(dst, text.c_str(), text.size() + 1);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
            CloseClipboard();
        }
    };

} // namespace LithoControl
