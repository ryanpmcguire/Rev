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

        int srcW = 0, srcH = 0;     // original image pixel dimensions
        int tileX = 0, tileY = 0;   // tile grid counts (0 = no grid)

        // Hint text shown when no image is loaded
        Text* hint = nullptr;

        ImagePreview(Element* parent, StyleList styles = {})
            : Box(parent, styles)
        {
            imgPrimitive = new Rev::Primitives::Image(shared->canvas);
            hint = new Text(this, "[ NO ARTWORK LOADED ]");
            hint->style->text.color = rgba(232, 232, 232, 0.2f);
            hint->style->text.size  = 14_px;
        }

        ~ImagePreview() override {
            delete imgPrimitive;
            delete imgTexture;
        }

        // Load a PNG/BMP/JPEG via GDI+ and upload to GPU texture.
        // Tile grid is inferred from slicer output (call setGrid after slicing).
        void loadFile(const std::string& path) {

            std::wstring wp(path.begin(), path.end());
            Gdiplus::Bitmap bmp(wp.c_str());
            if (bmp.GetLastStatus() != Gdiplus::Ok) return;

            srcW = (int)bmp.GetWidth();
            srcH = (int)bmp.GetHeight();

            // Draw into a 32bppARGB bitmap so pixel format is consistent
            Gdiplus::Bitmap rgbaBmp(srcW, srcH, PixelFormat32bppARGB);
            {
                Gdiplus::Graphics g(&rgbaBmp);
                g.DrawImage(&bmp, 0, 0, srcW, srcH);
            }

            Gdiplus::BitmapData bd;
            Gdiplus::Rect grect(0, 0, srcW, srcH);
            rgbaBmp.LockBits(&grect, Gdiplus::ImageLockModeRead,
                             PixelFormat32bppARGB, &bd);

            // GDI+ 32bppARGB stores B,G,R,A → reorder to R,G,B,A for OpenGL
            std::vector<uint8_t> rgba(srcW * srcH * 4);
            auto* src = reinterpret_cast<uint8_t*>(bd.Scan0);
            for (int row = 0; row < srcH; row++) {
                for (int col = 0; col < srcW; col++) {
                    int si = row * bd.Stride + col * 4;
                    int di = (row * srcW + col) * 4;
                    rgba[di+0] = src[si+2];  // R
                    rgba[di+1] = src[si+1];  // G
                    rgba[di+2] = src[si+0];  // B
                    rgba[di+3] = src[si+3];  // A
                }
            }
            rgbaBmp.UnlockBits(&bd);

            // Upload to GPU (replace existing texture if any)
            delete imgTexture;
            imgTexture = new Rev::Graphics::Texture(shared->canvas->context, {
                .data     = rgba.data(),
                .width    = (size_t)srcW,
                .height   = (size_t)srcH,
                .channels = 4,
                .filter   = Rev::Graphics::Texture::Filter::Bilinear
            });
            imgPrimitive->texture = imgTexture;

            // Hide hint text once an image is loaded
            if (hint) hint->style->visibility = Visibility::Hidden;
        }

        void setGrid(int cols, int rows) {
            tileX = cols;
            tileY = rows;
        }

        void computePrimitives(Event& e) override {

            if (imgPrimitive && imgTexture && srcW > 0 && srcH > 0) {
                // Letterbox: scale to fit the element's rect while preserving aspect ratio
                float scaleX = rect.w / (float)srcW;
                float scaleY = rect.h / (float)srcH;
                float scale  = (std::min)(scaleX, scaleY);
                float dw = srcW * scale;
                float dh = srcH * scale;
                float dx = rect.x + (rect.w - dw) * 0.5f;
                float dy = rect.y + (rect.h - dh) * 0.5f;

                auto& d      = *imgPrimitive->data;
                d.x          = dx; d.y = dy; d.w = dw; d.h = dh;
                d.opacity    = 1.0f;
                d.tileCountX = (float)tileX;
                d.tileCountY = (float)tileY;
            }

            Box::computePrimitives(e);
        }

        void draw(Event& e) override {

            // Draw image behind children (hint text / no-image state)
            if (imgPrimitive && imgTexture) {
                imgPrimitive->draw();
            }

            Box::draw(e);
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

        std::atomic<int>  frameN     { 0 };
        std::atomic<int>  frameTotal { 0 };
        std::atomic<bool> abortFlag  { false };
        std::thread       jobThread;

        // Posted by the slicer thread; consumed on the main thread in computeStyle
        std::atomic<int>  pendingTileX { 0 };
        std::atomic<int>  pendingTileY { 0 };
        std::atomic<bool> pendingPreviewReload { false };

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
            std::string port     = "";
            std::string piHost   = "192.168.1.240";
            std::string jobsDir  = "./jobs";
            std::string platform = "stm32";
        } settings;

        // ── Sidebar scroll ────────────────────────────────────────────────────

        float sidebarScrollY   = 0.0f;
        Box*  sidebarBox       = nullptr;
        Box*  sidebarContent   = nullptr;
        Box*  sidebarScrollThumb = nullptr;

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
        ImagePreview* previewImg = nullptr;

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
            buildSidebar();
            buildCollapseHandle();
            buildRightPanel();
            refreshJobList();

            // Global drag handlers — fire on any mouse move/up over the Interface,
            // so drags stay active even when the cursor leaves the originating handle.
            this->onMouseMove([this](Rev::Element::Event& e) {
                if (sidebarDragging) {
                    float dx = e.mouse.pos.x - sidebarDragStartX;
                    if (std::abs(dx) > 4.0f) sidebarDragMoved = true;
                    float newW = std::clamp(sidebarDragStartW + dx, 0.0f, 700.0f);
                    if (newW < 30.0f) newW = 0.0f;
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
            });

            this->onMouseUp([this](Rev::Element::Event&) {
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
            sidebarScrollThumb->style->position.left    = Px(1);
            sidebarScrollThumb->style->position.top     = Px(0);
            sidebarScrollThumb->style->size.width       = 4_px;
            sidebarScrollThumb->style->size.height      = Px(40);
            sidebarScrollThumb->style->background.color = rgba(80, 80, 80, 1);
            sidebarScrollThumb->style->border.radius    = 2_px;

            // Wheel handler: update scroll and immediately dirty sidebarContent
            // so the frame repaints without waiting for a hover event.
            sb->onMouseWheel([this](Rev::Element::Event& e) {
                sidebarScrollY -= (e.mouse.wheel.y / 120.0f) * 40.0f;
                if (sidebarScrollY < 0.0f) sidebarScrollY = 0.0f;
                // Setting style directly fires Dist::operator= → dirty flag → refresh()
                // → propagates to root → immediate repaint this frame.
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

            // Seed the options list with the saved port (if any) so something
            // shows before the user hits SCAN.
            std::vector<Dropdown::Option> initOpts;
            if (!settings.port.empty())
                initOpts.push_back({ settings.port, settings.port });

            portDrop = new Dropdown(stmPortRow, {
                .options     = initOpts,
                .placeholder = "Scan for ports...",
                .value       = settings.port
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

            Box* sliceRow = new Box(body, { &Theme::RowH });
            sliceRow->style->margin.top = 6_px;

            Box* sliceBtn = makeBtn(sliceRow, "SLICE", [this]() { startSlice(); }, true);
            sliceBtnTxt = (Text*)sliceBtn->children[0];
            (void)sliceBtn;
        }

        // ── Job panel ─────────────────────────────────────────────────────────

        void buildJobPanel(Box* body) {

            Box* dirRow = new Box(body, { &Theme::RowH });

            jobsDirInput = new TextInput(dirRow, {
                .label = "JOBS DIR",
                .placeholder = settings.jobsDir,
                .maxLength = 256
            });
            jobsDirInput->label->style->text.color = rgba(232, 232, 232, 0.6f);
            jobsDirInput->label->style->text.size  = 9_px;

            Box* browseDir = makeBtn(dirRow, "...", [this]() { browseJobsDir(); }, false);
            browseDir->style->size.width = 30_px;
            browseDir->style->size.max.width = 30_px;
            (void)browseDir;

            // Job list container (scrollable via stencil)
            jobListBox = new Box(body);
            jobListBox->style->layout   = { Axis::Vertical, Align::Start, Align::Start };
            jobListBox->style->size     = { 100_pct, 100_px };
            jobListBox->style->overflow = Overflow::Hide;
            jobListBox->style->background.color = rgba(28, 28, 28, 1);
            jobListBox->style->border.color     = rgba(42, 42, 42, 1);
            jobListBox->style->border.width     = 1_px;
            jobListBox->style->border.radius    = 3_px;
            jobListBox->style->margin.bottom    = 6_px;

            Box* btnRow = new Box(body, { &Theme::RowH });

            makeBtn(btnRow, "REFRESH", [this]() { refreshJobList(); }, false);
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
            if (runnerLogTxt) {
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
            if (gcodeLogTxt) {
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
            if (sidebarBox && sidebarContent && sidebarScrollThumb) {
                float trackH   = sidebarBox->rect.h;
                float contentH = sidebarContent->rect.h > 0.0f ? sidebarContent->rect.h : trackH;
                float maxScroll = (std::max)(0.0f, contentH - trackH);
                if (sidebarScrollY > maxScroll) {
                    sidebarScrollY = maxScroll;
                    sidebarContent->style->position.top = Px(-sidebarScrollY);
                }
                float ratio   = (contentH > trackH) ? (trackH / contentH) : 1.0f;
                float thumbH  = (std::max)(20.0f, ratio * trackH);
                float thumbTop = (maxScroll > 0.0f)
                    ? (sidebarScrollY / maxScroll) * (trackH - thumbH)
                    : 0.0f;
                sidebarScrollThumb->style->size.height  = Px(thumbH);
                sidebarScrollThumb->style->position.top = Px(thumbTop);
            }

            // Sync platform dropdown → hide/show rows
            if (platformDrop) {
                Platform sel = (platformDrop->params.value == "pi") ? Platform::Pi : Platform::STM32;
                if (sel != platform) {
                    platform = sel;
                    updatePlatformRows();
                }
            }

            Box::computeStyle(e);
        }

        void computeChildren(Rev::Element::Event& e) override {

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
            onConnected();
            refreshJobList();
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

        void onConnected() {
            connFlag = true;
            // Post UI updates via queue (called from background thread)
            logQ.push("Connected");
            // Update status on main thread via computeStyle check
            // (connFlag is atomic-safe bool, no race since only one thread writes)
            if (connStatus) {
                connStatus->content     = "CONNECTED";
                connStatus->style->text.color = rgba(48, 209, 88, 1);
            }
            if (connectBtnTxt) connectBtnTxt->content = "DISCONNECT";
        }

        void onDisconnected() {
            connFlag = false;
            logQ.push("Disconnected");
            if (connStatus) {
                connStatus->content = "DISCONNECTED";
                connStatus->style->text.color = rgba(255, 59, 48, 1);
            }
            if (connectBtnTxt) connectBtnTxt->content = "CONNECT";
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
                // "FRAME n/total"
                auto slash = msg.find('/');
                if (slash != std::string::npos) {
                    try {
                        frameN     = std::stoi(msg.substr(6, slash - 6));
                        frameTotal = std::stoi(msg.substr(slash + 1));
                        frameLabel->content = "FRAME " + std::to_string(frameN.load()) +
                                              "/" + std::to_string(frameTotal.load());
                    } catch (...) {}
                }
            } else if (msg.rfind("GCODE ", 0) == 0) {
                gcodeQ.push(msg.substr(6));
            } else if (msg == "JOB_DONE") {
                frameLabel->content = "DONE";
                progressFill->style->size.width = 100_pct;
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
                std::string dir = jobsDirInput ? jobsDirInput->text->strContent : "";
                if (dir.empty()) dir = settings.jobsDir;
                // Resolve a relative default against the known DLP repo root
                if (!dlpRoot.empty() && !std::filesystem::path(dir).is_absolute())
                    dir = (std::filesystem::path(dlpRoot) / dir).string();
                jobs.clear();
                try {
                    for (auto& entry : std::filesystem::directory_iterator(dir)) {
                        if (entry.is_directory()) {
                            if (std::filesystem::exists(entry.path() / "manifest.json")) {
                                jobs.push_back(entry.path().filename().string());
                            }
                        }
                    }
                    std::sort(jobs.begin(), jobs.end());
                } catch (...) {}
                jobListDirty = true;
            } else if (piClient) {
                piClient->sendLine("LIST_JOBS");
            }
        }

        void rebuildJobList() {
            if (!jobListBox) return;
            for (auto* b : jobItemBoxes) delete b;
            jobItemBoxes.clear();

            for (auto& j : jobs) {
                Box* item = new Box(jobListBox, { &Theme::JobItem, &Theme::JobItemHover });
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
            if (selectedJob.empty()) { logQ.push("Select a job first"); return; }

            if (platform == Platform::Pi) {
                if (!piClient) { logQ.push("Not connected to Pi"); return; }
                piClient->sendLine("START_JOB " + selectedJob);
                jobState = JobState::Running;
                logQ.push("Starting (Pi): " + selectedJob);
                return;
            }

            // STM32 path — execute in background thread
            if (!stmSerial) { logQ.push("Not connected to STM32"); return; }
            abortFlag = false;
            jobState  = JobState::Running;

            std::string dir = jobsDirInput ? jobsDirInput->text->strContent : settings.jobsDir;
            if (dir.empty()) dir = settings.jobsDir;
            std::string jobName = selectedJob;

            if (jobThread.joinable()) jobThread.join();
            jobThread = std::thread([this, dir, jobName]() {
                runStm32Job(dir + "/" + jobName);
            });
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
            if (platform == Platform::Pi && piClient) {
                piClient->sendLine("ABORT");
            } else if (stmSerial) {
                stmSerial->sendText("BLANK\n");
            }
            logQ.push("ABORT sent");
            jobState = JobState::Idle;
        }

        // STM32 job runner (runs on jobThread)
        void runStm32Job(const std::string& jobPath) {

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
                    return;
                }

                frameN = i + 1;
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

                // Send PATTERN
                stmSerial->sendText("PATTERN\n");
                std::string r = stmSerial->readLine(6000);
                if (r != "READY") { logQ.push("[ERR] PATTERN: " + r); return; }

                stmSerial->sendBytes(bitmap.data(), bitmap.size());
                stmSerial->sendByte(checksum);
                r = stmSerial->readLine(6000);
                if (r != "OK") { logQ.push("[ERR] Bitmap recv: " + r); return; }

                // Expose
                stmSerial->sendText("EXPOSE " + std::to_string(exposeMs) + "\n");
                r = stmSerial->readLine(6000);
                if (r != "EXPOSING") { logQ.push("[ERR] EXPOSE start: " + r); return; }

                r = stmSerial->readLine(exposeMs + 3000);
                if (r != "DONE" && r != "ABORTED") {
                    logQ.push("[ERR] Expose finish: " + r);
                    return;
                }
            }

            logQ.push("JOB_DONE");
            frameLabel->content = "DONE";
            progressFill->style->size.width = 100_pct;
            jobState = JobState::Idle;
        }

        // ─────────────────────────────────────────────────────────────────────
        // Jog
        // ─────────────────────────────────────────────────────────────────────

        void jog(const std::string& axis, int dir) {
            if (!connFlag) { logQ.push("Not connected"); return; }

            float dist  = jogDistInput ? parseFloat(jogDistInput->text->strContent, 1.0f) : 1.0f;
            float feed  = settings.feedRate;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s%+.3f F%.0f", axis.c_str(), dist * dir, feed);
            std::string cmd = buf;

            logQ.push("-> JOG " + cmd);
            std::thread([this, cmd]() {
                if (platform == Platform::STM32 && stmSerial) {
                    stmSendWait("GANTRY G1 " + cmd + "\n", "OK", 30000);
                    stmSendWait("GANTRY G4 P0\n", "OK", 10000);
                    logQ.push("JOG_DONE");
                } else if (piClient) {
                    piClient->sendLine("JOG " + cmd);
                }
            }).detach();
        }

        void sendHome() {
            if (!connFlag) { logQ.push("Not connected"); return; }
            logQ.push("-> HOME");
            std::thread([this]() {
                if (platform == Platform::STM32 && stmSerial) {
                    stmSendWait("GANTRY G28\n", "OK", 60000);
                    stmSendWait("GANTRY G4 P0\n", "OK", 10000);
                    logQ.push("JOG_DONE");
                } else if (piClient) {
                    piClient->sendLine("JOG HOME");
                }
            }).detach();
        }

        void sendEstop() {
            logQ.push("[!!!] E-STOP SENT");
            abortFlag = true;
            if (platform == Platform::STM32 && stmSerial) {
                stmSerial->sendText("GANTRY !\n");
            } else if (piClient) {
                piClient->sendLine("ESTOP");
            }
            if (frameLabel) {
                frameLabel->content = "E-STOP";
                frameLabel->style->text.color = rgba(255, 59, 48, 1);
            }
        }

        void sendReset() {
            logQ.push("-> RESET");
            std::thread([this]() {
                if (platform == Platform::STM32 && stmSerial) {
                    stmSerial->sendText("BLANK\n");
                    stmSerial->sendText("GANTRY \x18\n");
                    logQ.push("READY");
                } else if (piClient) {
                    piClient->sendLine("RESET");
                }
                abortFlag = false;
            }).detach();
        }

        void sendSetHome() {
            if (!connFlag) { logQ.push("Not connected"); return; }
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

        void scanPorts() {
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

            // Populate dropdown and select the first found port
            if (portDrop) {
                portDrop->params.options = found;
                portDrop->params.value   = found[0].value;
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
            std::string outDir = jobsDirInput   ? jobsDirInput->text->strContent    : settings.jobsDir;
            bool inv           = invertChk      ? (bool)invertChk->value            : settings.invert;

            if (sliceBtnTxt) sliceBtnTxt->content = "SLICING...";
            logQ.push("Slicing " + std::filesystem::path(inputFilePath).filename().string() + "...");

            std::thread([this, expose, feed, thresh, projW, projH, over, jname, outDir, inv]() {
                runSlicerSubprocess(inputFilePath, expose, feed, thresh, projW, projH, over, jname, outDir, inv);
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
            bool invert)
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
                if (sliceBtnTxt) sliceBtnTxt->content = "SLICE";
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
                        logQ.push("[SLICER] " + line);
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
                jobListDirty = true;
                // Signal main thread to reload preview with tile grid
                pendingTileX       = parsedCols;
                pendingTileY       = parsedRows;
                pendingPreviewReload = true;
            } else {
                logQ.push("[ERR] Slicer exited with code " + std::to_string(exitCode));
            }

            if (sliceBtnTxt) sliceBtnTxt->content = "SLICE";
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
            settings.port      = gs("port",     "COM3");
            settings.piHost    = gs("piHost",   "192.168.1.240");
            settings.jobsDir   = gs("jobsDir",  "./jobs");
            settings.platform  = gs("platform", "stm32");
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
            ws("port",         portDrop ? portDrop->params.value : settings.port);
            ws("piHost",       hostInput  ? hostInput->text->strContent  : settings.piHost);
            ws("jobsDir",      jobsDirInput ? jobsDirInput->text->strContent : settings.jobsDir);
            ws("platform",     platform == Platform::Pi ? "pi" : "stm32");
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
            return inp;
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
