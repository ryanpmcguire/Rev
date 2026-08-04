module;

// Win32 for the taskbar icon only (see computeStyle()) -- everything else
// that used to need it has moved into platform-specific files.
#ifdef _WIN32
#include <windows.h>
#endif

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
#include <ctime>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstdio>

#include <dbg.hpp>
#include <managed.hpp>

export module LithoControl.Interface;

import Rev.Core.Resource;
import Rev.Element;
import Rev.Element.Event;
import Rev.Window;
import Rev.Element.Style;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.TextInput;
import Rev.Element.Dropdown;
import Rev.Element.Checkbox;
import Rev.Serial;
import Rev.SocketClient;
import Rev.OS.Dialog;
import Rev.OS.SerialPort;
import Rev.OS.Display;
import Rev.OS.ThreadControl;
import Rev.OS.Clipboard;
import Rev.Primitive.Image;
import Rev.Graphics.Texture;
import LithoControl.Theme;
import LithoControl.ImagePreview;
import LithoControl.ImageDecode;
import LithoControl.ImageEncode;
import LithoControl.TestPatternRaster;

export namespace LithoControl {

    using namespace Rev;
    using namespace Rev::Element;
    using Text = Rev::Element::Text;  // disambiguate from Rev::Primitives::Text


    // -------------------------------------------------------------------------
    // Thread-safe string queue
    // -------------------------------------------------------------------------

    struct MsgQueue {
        std::mutex              mtx;
        std::deque<std::string> q;
        Window*                 ownerWindow = nullptr;  // set by Interface ctor

        void push(std::string s) {
            { std::lock_guard g(mtx); q.push_back(std::move(s)); }
            // Trigger a repaint so computeStyle drains this message without waiting
            // for the next user-input event. requestFrame() is documented safe to
            // call from background threads on every NativeWindow backend (Win32
            // InvalidateRect / X11 XSendEvent ClientMessage).
            if (ownerWindow && ownerWindow->window) ownerWindow->window->requestFrame();
        }

        bool pop(std::string& out) {
            std::lock_guard g(mtx);
            if (q.empty()) return false;
            out = std::move(q.front()); q.pop_front(); return true;
        }
    };

    struct Interface : public Box {

        // -- State ------------------------------------------------------------

        enum class Platform { STM32, Pi };
        enum class JobState  { Idle, Running, Paused };

        Platform  platform  = Platform::STM32;
        bool      connFlag  = false;
        JobState  jobState  = JobState::Idle;

        Rev::Serial*       stmSerial = nullptr;
        Rev::SocketClient* piClient  = nullptr;

        std::string inputFilePath;
        std::string dlpRoot;     // set when a file is loaded; used to resolve slicer/jobs paths
        std::string calibDtRoot; // calib-dt repo root, for the Calibration Actions panel (uv run <script>)
        std::string selectedJob;
        std::vector<std::string> jobs;
        bool jobListDirty = false;
        bool platformDirty = false;

        std::atomic<int>  frameN     { 0 };
        std::atomic<int>  frameTotal { 0 };
        std::atomic<bool> abortFlag  { false };
        std::atomic<bool> estopped   { false };   // latched by E-STOP, cleared by RESET
        std::atomic<bool> resetting  { false };   // true while sendReset thread is running
        std::atomic<bool> jobRunning { false };   // true while runStm32Job is executing
        std::thread       jobThread;
        void*             jobThreadHandle = nullptr;  // for ThreadControl::CancelBlockingIo on E-STOP
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
        std::atomic<bool> pendingSliceDone    { false };  // slicer worker -> reset SLICE btn
        std::atomic<bool> pendingEdidDone     { false };  // edid worker -> reset APPLY EDID btn
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

        // -- Persisted settings -----------------------------------------------

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
            std::string calibDtRoot = ""; // persisted calib-dt repo root
        } settings;

        // -- Pages --------------------------------------------------------------
        // The sidebar is split into three pages (PROJECTS/CALIBRATION/EXECUTION)
        // instead of showing every section at once. Each section built in
        // buildSidebar() is wrapped by makeSection() into a single Box that gets
        // attached to/detached from sidebarContent as a whole (addChild/
        // removeChild -- same "fully absent from the layout tree" pattern
        // already used for hdmiDisplayRow, rather than Visibility::Hidden,
        // which leaves a zero-height hit-area behind). The Execution page also
        // shows the runner/G-code log panel in the right panel (see
        // updatePageVisibility() / statusDragHandle / statusPanelBox).
        enum class Page { Connections, Calibration, Execution };
        Page currentPage = Page::Connections;
        std::vector<std::pair<Page, Box*>> pageSections;
        Box* pageTabConnectionsBtn = nullptr;
        Box* pageTabCalibrationBtn = nullptr;
        Box* pageTabExecutionBtn   = nullptr;
        Box* rightPanelBox         = nullptr;
        Box* statusDragHandle      = nullptr;
        // Job-progress row and G-code column inside statusPanelBox -- shown
        // only on the Execution page; the Calibration page reuses the same
        // panel for just the runner log (which already carries "[CALIB] ..."
        // lines from runCalibDtSubprocess()), acting as its terminal output.
        Box* statusProgRow         = nullptr;
        Box* gcodeColBox           = nullptr;
        Text* runnerLogLbl         = nullptr;
        Text*      calibDtPathLabel  = nullptr;

        // -- Sidebar scroll ----------------------------------------------------

        float sidebarScrollY   = 0.0f;
        Box*  sidebarBox       = nullptr;
        Box*  sidebarContent   = nullptr;
        Box*  sidebarScrollThumb = nullptr;
        bool  sbThumbDragging       = false;
        float sbThumbDragStartY     = 0.0f;
        float sbThumbDragStartScroll = 0.0f;

        // -- UI element pointers ----------------------------------------------

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
        TextInput* jobSearchInput = nullptr;
        std::string jobFilter;
        Box*       jobListBox     = nullptr;
        Box*       jobListInner   = nullptr;
        std::vector<Box*> jobItemBoxes;

        // Jog
        TextInput* jogDistInput   = nullptr;
        Text*      posXLabel      = nullptr;
        Text*      posYLabel      = nullptr;
        Text*      posZLabel      = nullptr;
        std::atomic<bool> posUpdatePending { false };
        std::string pendingPosX, pendingPosY, pendingPosZ;
        std::string cachedWCO = "0,0,0";   // last WCO seen from FluidNC ? response
        std::mutex  posMtx;

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

        // Overlay opacity slider
        Box*  overlaySliderTrack    = nullptr;
        Box*  overlaySliderFill     = nullptr;
        Text* overlayPctLabel       = nullptr;
        bool  overlaySliderDragging = false;

        // Zoom slider
        Box*  zoomSliderTrack    = nullptr;
        Box*  zoomSliderFill     = nullptr;
        Text* zoomLabel          = nullptr;
        bool  zoomSliderDragging = false;

        // Job list scroll
        float jobListScrollY         = 0.0f;
        Box*  jobListContent         = nullptr;
        Box*  jobListScrollTrack     = nullptr;
        Box*  jobListScrollThumb     = nullptr;
        bool  jobListThumbDragging   = false;
        float jobListThumbDragStartY = 0.0f;
        float jobListThumbDragStartScroll = 0.0f;

        // HDMI Passthrough
        struct HdmiDisplay { std::string devName; std::string label; int x, y, w, h; };
        std::vector<HdmiDisplay> hdmiDisplays;
        Checkbox*   hdmiPassthroughChk = nullptr;
        Checkbox*   hdmiRedChk        = nullptr;
        Checkbox*   hdmiGreenChk      = nullptr;
        Checkbox*   hdmiBlueChk       = nullptr;
        Checkbox*   hdmiEvmCorrectChk = nullptr;
        // BGRA on-pixel color for the HDMI projector window (channels checkboxes).
        // Default 0xFF000000 = no channels enabled (black). Updated in computeStyle.
        std::atomic<uint32_t> hdmiChannelMask { 0xFF000000u };
        // Physical flip to compensate for the projector's mount/optical path
        // (e.g. a mirror in the relay, or a rear/ceiling mount) -- applied in
        // composeHdmiFrame() to every frame sent to the projector window
        // (solid-color test, RGB test animation, and actual job frames
        // alike), since whatever flips the optical path flips all of them.
        // Atomics because composeHdmiFrame() is read from the projector
        // window's own message-loop thread, not necessarily the main thread
        // (same reasoning as hdmiChannelMask/hdmiSolidColor above).
        Checkbox*   hdmiFlipHChk = nullptr;
        Checkbox*   hdmiFlipVChk = nullptr;
        std::atomic<bool> hdmiFlipH { false };
        std::atomic<bool> hdmiFlipV { false };
        // Last decoded artwork pixels, cached so channel toggles repush without re-decode.
        std::vector<uint8_t> hdmiArtworkRGBA;
        int hdmiArtworkW = 0, hdmiArtworkH = 0;
        Box*        hdmiDisplaySlot    = nullptr;
        Box*        hdmiDisplayRow     = nullptr;
        Dropdown*   hdmiDisplayDrop    = nullptr;
        void*       hdmiHwnd           = nullptr;  // HWND on Windows; opaque handle to platform state on Linux
        std::thread hdmiWinThread;
        std::atomic<bool> hdmiWinRunning { false };
        std::mutex        hdmiFrameMtx;
        std::vector<uint8_t> hdmiCurrentFrame;  // 28800-byte 1bpp; empty = blank
        bool              hdmiIsBlank    = true;
        std::atomic<uint32_t> hdmiSolidColor { 0 };  // 0=off; else fill HDMI with this BGRA
        std::thread       hdmiColorTestThread;
        std::vector<uint8_t>  hdmiTestBGRA;           // 640×360×4; updated by animation thread
        std::atomic<bool>     hdmiTestActive  { false };
        std::atomic<bool>     hdmiTestRunning { false };
        // Calibration line-grid, generated by LithoRev itself (see
        // generateCalibGrid() in Interface.Build.cpp) and pushed through
        // composeHdmiFrame() -- NOT calib-dt's own ProjectorWindow -- so it
        // shares the one HDMI window/Flip H/V instead of a second,
        // uncoordinated fullscreen window fighting for the same monitor.
        std::atomic<bool>     hdmiCalibGridActive { false };
        std::thread           hdmiTestThread;
        Box*                  hdmiTestBtn     = nullptr;
        // User-supplied still image shown on the projector in place of the
        // built-in RGB test pattern (see loadCustomImage()). Takes priority
        // over hdmiTestBGRA in composeHdmiFrame() but not over hdmiSolidColor
        // or the calibration grid.
        std::vector<uint8_t>  hdmiCustomImageBGRA;  // 640x360x4; empty = none loaded
        std::atomic<bool>     hdmiCustomImageActive { false };
        Box*                  hdmiCustomImageBtn    = nullptr;
        Box*                  uvBtn           = nullptr;
        Text*                 uvBtnTxt        = nullptr;
        bool                  uvOn            = false;

        // Projector EDID apply (pc/apply_edid.bat -- CRU-based override installer)
        Box*                  edidBtn         = nullptr;
        Text*                 edidBtnTxt      = nullptr;
        std::atomic<bool>     edidApplying    { false };
        std::thread           edidThread;

        // The owning top-level Window, set once in the constructor. Used for
        // requestFrame() so background threads can trigger a repaint regardless
        // of which window (e.g. the HDMI projector window) currently holds
        // foreground focus.
        Window* ownerWindow = nullptr;

        // Set once in computeStyle() the first time the native window handle is
        // available (Windows only, for the taskbar icon -- see computeStyle()).
        bool iconApplied = false;

        // Camera preview
        struct CameraDevice { std::string name; };
        std::vector<CameraDevice>    cameraDevices;
        Dropdown*                    cameraDrop      = nullptr;
        Text*                        cameraBtnTxt    = nullptr;
        std::atomic<bool>            cameraRunning   { false };
        std::thread                  cameraThread;
        std::mutex                   cameraFrameMtx;
        std::vector<uint8_t>         cameraFrameRGBA;
        int                          cameraFrameW    = 0;
        int                          cameraFrameH    = 0;
        std::atomic<bool>            cameraFrameReady { false };
        // Opaque platform capture-session handle (IMFSourceReader* on Windows,
        // unused on Linux -- see CameraCapture.win.cpp / CameraCapture.lnx.cpp).
        std::atomic<void*>           cameraReader    { nullptr };
        // Windows only: which capture path the current/last-started camera
        // uses -- generic MediaFoundation UVC, or the AmScope vendor SDK
        // (Rev.AmcamCamera, see AmcamCamera.win.ixx). Unused on Linux.
        enum class CameraBackend { MediaFoundation, AmScope };
        CameraBackend                 cameraBackend  { CameraBackend::MediaFoundation };

        // Camera adjustment controls (flip/rotate/brightness/contrast/target
        // FPS/output resolution). Applied uniformly to every captured frame
        // regardless of backend -- see applyCameraAdjustments(). Rotation/
        // resolution/FPS are compact button groups rather than Dropdowns:
        // a Dropdown's option list is a position:absolute overlay that gets
        // clipped by the sidebar's Overflow::Hide when opened near the
        // bottom of the scrolled viewport (see Theme::SidebarRoot), which
        // made the lower options unreachable. Brightness/contrast are the
        // same custom track+fill drag-slider pattern as the zoom/overlay
        // sliders above (the framework Slider element's default label/value
        // styling is dark-on-light and unreadable on this dark sidebar).
        // Click/drag handlers write straight into the atomics below; the
        // capture thread (which calls applyCameraAdjustments()) reads them
        // directly, and a per-frame sync block updates the fill bars/labels.
        Checkbox*             cameraFlipHChk        = nullptr;
        Checkbox*             cameraFlipVChk        = nullptr;

        Box*                  cameraBrightTrack     = nullptr;
        Box*                  cameraBrightFill      = nullptr;
        Text*                 cameraBrightLabel     = nullptr;
        bool                  cameraBrightDragging  = false;

        Box*                  cameraContrastTrack   = nullptr;
        Box*                  cameraContrastFill    = nullptr;
        Text*                 cameraContrastLabel   = nullptr;
        bool                  cameraContrastDragging = false;

        std::atomic<bool>     cameraFlipH        { false };
        std::atomic<bool>     cameraFlipV        { false };
        std::atomic<int>      cameraRotationDeg  { 0 };    // 0, 90, 180, 270
        std::atomic<int>      cameraBrightness   { 0 };    // -100..100
        std::atomic<int>      cameraContrastPct  { 100 };  // 0..300 (%)
        std::atomic<int>      cameraTargetFps    { 0 };    // 0 = unlimited
        std::atomic<int>      cameraResW         { 0 };    // 0 = native (no rescale)
        std::atomic<int>      cameraResH         { 0 };
        std::chrono::steady_clock::time_point cameraLastFrameTime{};

        // -- Extended UVC controls (exposure / gain-ISO) ---------------------
        // Generic UVC cameras only (Windows Media Foundation / the sole Linux
        // V4L2 backend) -- the AmScope vendor SDK path (CameraBackend::AmScope,
        // used for the MU130 etc.) manages its own auto-exposure internally
        // and has no manual control surface, so this section stays detached
        // whenever that backend is active. Min==max (both 0) means the
        // connected UVC device doesn't expose that control.
        Checkbox*             cameraAutoExpoChk     = nullptr;

        Box*                  cameraExposureTrack   = nullptr;
        Box*                  cameraExposureFill    = nullptr;
        Text*                 cameraExposureLabel   = nullptr;
        bool                  cameraExposureDragging = false;

        Box*                  cameraGainTrack       = nullptr;
        Box*                  cameraGainFill        = nullptr;
        Text*                 cameraGainLabel       = nullptr;
        bool                  cameraGainDragging    = false;

        // Slot the extended-settings block attaches to/detaches from -- see
        // the sync in computeStyle(). Detaching (rather than
        // Visibility::Hidden) removes it from layout entirely, same as
        // platformRowSlot below.
        Box*                  cameraExtendedSlot    = nullptr;
        Box*                  cameraExtendedBox     = nullptr;

        std::atomic<bool>     cameraAutoExposure  { true };
        std::atomic<int>      cameraExposureVal   { 0 };
        std::atomic<int>      cameraExposureMin   { 0 };
        std::atomic<int>      cameraExposureMax   { 0 };
        std::atomic<int>      cameraGainVal       { 0 };
        std::atomic<int>      cameraGainMin       { 0 };
        std::atomic<int>      cameraGainMax       { 0 };

        // Live capture-session handles used to push manual exposure/gain to
        // the device -- IAMCameraControl*/IAMVideoProcAmp* on Windows (see
        // CameraCapture.win.cpp), the open V4L2 fd on Linux (see
        // CameraCapture.lnx.cpp). Null/-1 while no UVC camera is running or
        // the AmScope backend is active.
        std::atomic<void*>    cameraCtrlIface    { nullptr };  // Windows: IAMCameraControl*
        std::atomic<void*>    cameraProcAmpIface { nullptr };  // Windows: IAMVideoProcAmp*
        std::atomic<int>      cameraCtrlFd       { -1 };       // Linux: V4L2 device fd

        // Software FPS cap shared by both capture backends -- not a true
        // sensor frame-rate control, just throttles how often a captured
        // frame is staged for preview.
        bool shouldEmitCameraFrame() {
            int fps = cameraTargetFps.load();
            if (fps <= 0) return true;
            auto now = std::chrono::steady_clock::now();
            double minIntervalMs = 1000.0 / fps;
            double elapsedMs = std::chrono::duration<double, std::milli>(now - cameraLastFrameTime).count();
            if (elapsedMs < minIntervalMs) return false;
            cameraLastFrameTime = now;
            return true;
        }

        // Per-frame post-processing applied identically regardless of
        // capture backend (MediaFoundation/V4L2 UVC or the AmScope vendor
        // SDK) -- deliberately not driving vendor-specific sensor controls,
        // which differ wildly between UVC devices and the amcam SDK. This
        // adjusts the displayed preview only; it is not a substitute for
        // calib-dt's photometric calibration.
        void applyCameraAdjustments(std::vector<uint8_t>& rgba, int& w, int& h) {
            bool flipH        = cameraFlipH.load();
            bool flipV        = cameraFlipV.load();
            int  rot          = cameraRotationDeg.load();
            int  brightness   = cameraBrightness.load();
            int  contrastPct  = cameraContrastPct.load();
            int  targetW      = cameraResW.load();
            int  targetH      = cameraResH.load();

            if (flipH) {
                for (int y = 0; y < h; y++) {
                    uint8_t* row = rgba.data() + (size_t)y * w * 4;
                    for (int x = 0; x < w / 2; x++) {
                        int lo = x * 4, hi = (w - 1 - x) * 4;
                        for (int c = 0; c < 4; c++) std::swap(row[lo + c], row[hi + c]);
                    }
                }
            }
            if (flipV) {
                for (int y = 0; y < h / 2; y++) {
                    uint8_t* rowA = rgba.data() + (size_t)y * w * 4;
                    uint8_t* rowB = rgba.data() + (size_t)(h - 1 - y) * w * 4;
                    for (int x = 0; x < w * 4; x++) std::swap(rowA[x], rowB[x]);
                }
            }

            if (rot == 90 || rot == 270) {
                std::vector<uint8_t> rotated((size_t)w * h * 4);
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        int nx = (rot == 90) ? (h - 1 - y) : y;
                        int ny = (rot == 90) ? x           : (w - 1 - x);
                        std::memcpy(&rotated[((size_t)ny * h + nx) * 4],
                                    &rgba[((size_t)y * w + x) * 4], 4);
                    }
                }
                rgba.swap(rotated);
                std::swap(w, h);
            } else if (rot == 180) {
                size_t n = (size_t)w * h;
                for (size_t i = 0; i < n / 2; i++) {
                    uint8_t* a = &rgba[i * 4];
                    uint8_t* b = &rgba[(n - 1 - i) * 4];
                    for (int c = 0; c < 4; c++) std::swap(a[c], b[c]);
                }
            }

            if (brightness != 0 || contrastPct != 100) {
                float c = contrastPct / 100.0f;
                for (size_t i = 0; i < rgba.size(); i += 4) {
                    for (int ch = 0; ch < 3; ch++) { // leave alpha untouched
                        float v = (rgba[i + ch] - 128.0f) * c + 128.0f + brightness;
                        rgba[i + ch] = (uint8_t)std::clamp(v, 0.0f, 255.0f);
                    }
                }
            }

            if (targetW > 0 && targetH > 0 && (targetW != w || targetH != h)) {
                std::vector<uint8_t> resized((size_t)targetW * targetH * 4);
                for (int y = 0; y < targetH; y++) {
                    int sy = y * h / targetH;
                    for (int x = 0; x < targetW; x++) {
                        int sx = x * w / targetW;
                        std::memcpy(&resized[((size_t)y * targetW + x) * 4],
                                    &rgba[((size_t)sy * w + sx) * 4], 4);
                    }
                }
                rgba.swap(resized);
                w = targetW; h = targetH;
            }
        }

        // Writes the current live camera preview frame to
        // <cwd>/camera_captures/capture_YYYYMMDD_HHMMSS.png -- a quick grab
        // from the GUI, distinct from calib-dt's capture-frames script
        // (which drives the camera directly for batch calibration capture).
        void saveCameraFrame() {
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            {
                std::lock_guard<std::mutex> lk(cameraFrameMtx);
                if (cameraFrameW <= 0 || cameraFrameH <= 0) {
                    logQ.push("[CAM] No frame to save yet");
                    return;
                }
                rgba = cameraFrameRGBA;
                w = cameraFrameW; h = cameraFrameH;
            }

            std::filesystem::path dir = std::filesystem::current_path() / "camera_captures";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (ec) {
                logQ.push("[CAM] Could not create " + dir.string() + ": " + ec.message());
                return;
            }

            std::time_t nowT = std::time(nullptr);
            std::tm lt = *std::localtime(&nowT);
            char ts[32];
            std::snprintf(ts, sizeof(ts), "capture_%04d%02d%02d_%02d%02d%02d.png",
                          lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                          lt.tm_hour, lt.tm_min, lt.tm_sec);

            std::filesystem::path outPath = dir / ts;

            if (encodeRGBAToPng(outPath.string(), rgba.data(), w, h)) {
                logQ.push("[CAM] Saved " + outPath.string());
            } else {
                logQ.push("[CAM] Failed to save frame");
            }
        }

        bool hdmiPassthrough() const {
            return hdmiPassthroughChk && hdmiPassthroughChk->value.get()
                && hdmiHwnd != nullptr;
        }

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

        // -- Constructor ------------------------------------------------------

        Interface(Element* parent) : Box(parent) {

            ownerWindow = dynamic_cast<Window*>(parent);
            logQ.ownerWindow = ownerWindow;

            this->style->layout           = { Axis::Horizontal, Align::Start, Align::Start };
            this->style->size             = { 100_pct, 100_pct };
            this->style->background.color = rgba(13, 13, 13, 1);

            loadSettings();
            dlpRoot = settings.dlpRoot;   // restore so ./jobs resolves without a browse
            // Bootstrap dlpRoot from the jobs dir when it wasn't persisted separately.
            // e.g. if jobsDir = "C:\...\DLP-photolithography\jobs" -> dlpRoot = parent.
            if (dlpRoot.empty() && !settings.jobsDir.empty()) {
                namespace fs = std::filesystem;
                fs::path jd(settings.jobsDir);
                if (jd.is_absolute()) {
                    // Walk up from jobsDir looking for pc/slicer.py
                    fs::path candidate = jd;
                    while (true) {
                        if (fs::exists(candidate / "pc" / "slicer.py")) {
                            dlpRoot = candidate.string();
                            settings.dlpRoot = dlpRoot;
                            break;
                        }
                        auto parent = candidate.parent_path();
                        if (parent == candidate) break;
                        candidate = parent;
                    }
                }
            }

            calibDtRoot = settings.calibDtRoot;
            if (calibDtRoot.empty() && !dlpRoot.empty()) {
                std::string guess = findCalibDtRoot(dlpRoot);
                if (!guess.empty()) { calibDtRoot = guess; settings.calibDtRoot = guess; }
            }

            buildSidebar();
            buildCollapseHandle();
            buildRightPanel();
            updatePageVisibility();   // now that rightPanelBox/statusPanelBox exist, apply the default page
            refreshJobList();
            scanPorts(false);    // populate the COM list at startup; don't select/connect
            scanCameras();       // auto-populate camera dropdown
            scanDisplays();      // auto-populate display dropdown

            // Global drag handlers -- fire on any mouse move/up over the Interface,
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
                if (sbThumbDragging && !e.mouse.lb) sbThumbDragging = false;
                if (overlaySliderDragging && previewImg && overlaySliderTrack) {
                    float t = overlaySliderTrack->rect.x;
                    float w = overlaySliderTrack->rect.w;
                    if (w > 0.0f)
                        previewImg->overlayOpacity = std::clamp((e.mouse.pos.x - t) / w, 0.0f, 1.0f);
                }
                if (zoomSliderDragging && previewImg && zoomSliderTrack) {
                    float w = zoomSliderTrack->rect.w;
                    if (w > 0.0f) {
                        float t = std::clamp((e.mouse.pos.x - zoomSliderTrack->rect.x) / w, 0.0f, 1.0f);
                        previewImg->zoom = 0.1f * std::pow(200.0f, t);
                    }
                }
                if (cameraBrightDragging && cameraBrightTrack) {
                    float w = cameraBrightTrack->rect.w;
                    if (w > 0.0f) {
                        float t = std::clamp((e.mouse.pos.x - cameraBrightTrack->rect.x) / w, 0.0f, 1.0f);
                        cameraBrightness = (int)std::lround(-100.0f + t * 200.0f);
                    }
                }
                if (cameraContrastDragging && cameraContrastTrack) {
                    float w = cameraContrastTrack->rect.w;
                    if (w > 0.0f) {
                        float t = std::clamp((e.mouse.pos.x - cameraContrastTrack->rect.x) / w, 0.0f, 1.0f);
                        cameraContrastPct = (int)std::lround(t * 300.0f);
                    }
                }
                if (cameraExposureDragging && cameraExposureTrack) {
                    float w = cameraExposureTrack->rect.w;
                    int lo = cameraExposureMin.load(), hi = cameraExposureMax.load();
                    if (w > 0.0f && hi > lo) {
                        float t = std::clamp((e.mouse.pos.x - cameraExposureTrack->rect.x) / w, 0.0f, 1.0f);
                        int v = lo + (int)std::lround(t * (hi - lo));
                        cameraExposureVal = v;
                        setCameraExposure(v);
                    }
                }
                if (cameraGainDragging && cameraGainTrack) {
                    float w = cameraGainTrack->rect.w;
                    int lo = cameraGainMin.load(), hi = cameraGainMax.load();
                    if (w > 0.0f && hi > lo) {
                        float t = std::clamp((e.mouse.pos.x - cameraGainTrack->rect.x) / w, 0.0f, 1.0f);
                        int v = lo + (int)std::lround(t * (hi - lo));
                        cameraGainVal = v;
                        setCameraGain(v);
                    }
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
                if (jobListThumbDragging && jobListBox && jobListInner && jobListScrollThumb) {
                    float trackH    = jobListBox->rect.h;
                    float contentH  = measureSpread(jobListInner);
                    float maxScroll = (std::max)(0.0f, contentH - trackH);
                    float thumbH    = (contentH > trackH) ? (std::max)(16.0f, (trackH / contentH) * trackH) : trackH;
                    float travel    = (std::max)(1.0f, trackH - thumbH);
                    float dy        = e.mouse.pos.y - jobListThumbDragStartY;
                    jobListScrollY  = std::clamp(jobListThumbDragStartScroll + dy * (maxScroll / travel), 0.0f, maxScroll);
                    jobListInner->style->position.top = Px(-jobListScrollY);
                }
            });

            // Keyboard shortcuts for the viewport (only fire when no text input consumed them)
            this->onKeyDown([this](Rev::Element::Event& e) {
                if (!previewImg) return;
                float step = e.keyboard.shift ? 80.0f : 20.0f;
                bool moved = false;
                if (e.keyboard.arrows.left)  { previewImg->offsetX -= step; moved = true; }
                if (e.keyboard.arrows.right) { previewImg->offsetX += step; moved = true; }
                if (e.keyboard.arrows.up)    { previewImg->offsetY -= step; moved = true; }
                if (e.keyboard.arrows.down)  { previewImg->offsetY += step; moved = true; }
                if (moved) {
                    e.propagate = false;
                    requestRepaint();
                }
            });

            this->onTextInput([this](Rev::Element::Event& e) {
                if (!previewImg || e.keyboard.input.empty()) return;
                const char c = e.keyboard.input[0];
                if (c == '+' || c == '=') {
                    previewImg->zoom = std::clamp(previewImg->zoom * 1.15f, 0.1f, 20.0f);
                } else if (c == '-') {
                    previewImg->zoom = std::clamp(previewImg->zoom / 1.15f, 0.1f, 20.0f);
                } else if (c == 'r' || c == 'R' || c == 'f' || c == 'F') {
                    previewImg->zoom    = 0.0f;   // auto-fit sentinel
                    previewImg->offsetX = previewImg->offsetY = 0.0f;
                } else if (c == '1') {
                    previewImg->zoom    = 1.0f;
                    previewImg->offsetX = previewImg->offsetY = 0.0f;
                } else {
                    return;
                }
                e.propagate = false;
                requestRepaint();
            });

            this->onMouseUp([this](Rev::Element::Event&) {
                sbThumbDragging       = false;
                overlaySliderDragging = false;
                zoomSliderDragging    = false;
                jobListThumbDragging  = false;
                cameraBrightDragging   = false;
                cameraContrastDragging = false;
                cameraExposureDragging = false;
                cameraGainDragging     = false;
                if (sidebarDragging && !sidebarDragMoved) {
                    // Click (no drag) -- toggle collapse
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
            hdmiTestRunning.store(false);
            if (hdmiTestThread.joinable()) hdmiTestThread.join();
            if (edidThread.joinable()) edidThread.join();
            stopCamera();
            if (jobThread.joinable()) jobThread.join();
            if (hdmiWinThread.joinable())      hdmiWinThread.detach();
            if (hdmiColorTestThread.joinable()) hdmiColorTestThread.detach();
            delete stmSerial;
            delete piClient;
            saveSettings();
        }

        // -- Build sidebar -----------------------------------------------------


        // -- Build methods (bodies in Interface.Build.cpp) --------------------

        void buildSidebar();
        void buildCollapseHandle();
        void buildConnectionPanel(Box* body);
        void buildDisplayPanel(Box* body);
        void buildCameraDevicePanel(Box* body);
        void buildCameraSettingsPanel(Box* body);
        void buildCalibrationPanel(Box* body);
        void buildSlicerPanel(Box* body);
        void buildJobPanel(Box* body);
        void buildJogPanel(Box* body);
        void buildRightPanel();
        void updateSendChkVisibility();
        void updatePageVisibility();
        Box* makeSection(Box* parent, const std::string& title, Box*& body);
        Box* makeBtn(Box* parent, const std::string& label,
                     std::function<void()> cb,
                     bool accent = false, bool danger = false,
                     bool success = false, bool warning = false);
        Box* makeJogBtn(Box* parent, const std::string& label, std::function<void()> cb);
        TextInput* makeInput(Box* parent, const std::string& lbl, const std::string& val, size_t maxLen);
        static void tightenInput(TextInput* inp);


        // Collapsible section header + body.
        // Returns a toggle() callable — call it after populating the body to start
        // the section collapsed; the header click calls the same function to expand.

        // -- Sidebar collapse handle -------------------------------------------
        // A 16px-wide strip between sidebar and right panel -- always visible,
        // lets the user hide/show the sidebar without losing access to the toggle.


        // -- Connection panel -------------------------------------------------


        // -- Display output panel ----------------------------------------------


        void scanDisplays() {
            hdmiDisplays.clear();
            std::vector<Dropdown::Option> opts;
            for (const auto& d : Rev::OS::Display::List()) {
                hdmiDisplays.push_back({ d.id, d.label, d.x, d.y, d.w, d.h });
                opts.push_back({ d.label, d.id });
                logQ.push("[DISP] " + d.label);
            }
            if (hdmiDisplayDrop) hdmiDisplayDrop->params.options = opts;
        }

        void setDisplayResolution() {
            if (!hdmiDisplayDrop) return;
            std::string dev = hdmiDisplayDrop->params.value;
            if (dev.empty()) { logQ.push("[DISP] No display selected"); return; }
            if (Rev::OS::Display::SetMode(dev, 640, 360))
                logQ.push("[DISP] Set 640x360 OK on " + dev);
            else
                logQ.push("[DISP] Set resolution failed");
        }

        // ---- HDMI fullscreen window --------------------------------------
        // Implemented per-platform in HdmiWindow.win.cpp / .lnx.cpp:
        // a borderless, always-on-top window pinned over a specific monitor,
        // repainted on demand from hdmiCurrentFrame/hdmiSolidColor/hdmiTestBGRA.
        void openHdmiWindow();
        void closeHdmiWindow();
        void requestHdmiRepaint();   // no-op if the window isn't open

        // Builds a 640x360 BGRA pixel buffer from hdmiSolidColor / hdmiTestBGRA
        // (if hdmiTestActive) / hdmiCurrentFrame+hdmiChannelMask, in that
        // priority order. Shared by both platforms' paint routines (Windows:
        // HdmiWndProc's WM_PAINT/WM_LITHO_FRAME; Linux: paintHdmiWindow) so the
        // composition logic can't drift between them. `outPx640x360` must
        // already be sized to 640*360 by the caller. Implemented once in
        // Interface.Build.cpp (compiled on every platform).
        void composeHdmiFrame(uint32_t* outPx640x360);

        void renderBitmapToHdmi(const std::vector<uint8_t>& bmp) {
            {
                std::lock_guard<std::mutex> lk(hdmiFrameMtx);
                hdmiCurrentFrame = bmp;
                hdmiIsBlank = false;
            }
            requestHdmiRepaint();
        }

        void blankHdmi() {
            {
                std::lock_guard<std::mutex> lk(hdmiFrameMtx);
                hdmiCurrentFrame.clear();
                hdmiIsBlank = true;
            }
            requestHdmiRepaint();
        }

        void showSolid(uint32_t bgra) {
            hdmiSolidColor.store(bgra);
            requestHdmiRepaint();
        }

        // Repaint the main Rev window. Safe to call from background threads --
        // see the comment on MsgQueue::push().
        void requestRepaint() {
            if (ownerWindow && ownerWindow->window) ownerWindow->window->requestFrame();
        }

        // Scale the cached artwork RGBA to 640×360, threshold to 1bpp, and push to
        // the HDMI projector. Called when artwork changes or channel mask changes.
        // Does nothing if no channels are enabled or no artwork is loaded.
        void pushArtworkToHdmi() {
            constexpr int DW = 640, DH = 360;
            if (hdmiArtworkRGBA.empty() || hdmiArtworkW <= 0 || hdmiArtworkH <= 0
                || (hdmiChannelMask.load() & 0x00FFFFFFu) == 0) {
                blankHdmi();
                return;
            }
            std::vector<uint8_t> bitmap((DW * DH + 7) / 8, 0);
            int sw = hdmiArtworkW, sh = hdmiArtworkH;
            for (int dy = 0; dy < DH; dy++) {
                for (int dx = 0; dx < DW; dx++) {
                    int sx = dx * sw / DW;
                    int sy = dy * sh / DH;
                    int si = (sy * sw + sx) * 4;
                    uint8_t r = hdmiArtworkRGBA[si+0];
                    uint8_t g = hdmiArtworkRGBA[si+1];
                    uint8_t b = hdmiArtworkRGBA[si+2];
                    uint8_t luma = (r > g ? r : g);
                    if (b > luma) luma = b;
                    if (luma > 127) {
                        int idx = dy * DW + dx;
                        bitmap[idx >> 3] |= (uint8_t)(1u << (7 - (idx & 7)));
                    }
                }
            }
            renderBitmapToHdmi(bitmap);
        }

        void colorTest() {
            if (!hdmiPassthrough()) { logQ.push("[DISP] Open projector window first"); return; }
            if (hdmiColorTestThread.joinable()) hdmiColorTestThread.detach();
            // Capture the EVM correction state at the moment the test starts.
            bool evm = hdmiEvmCorrectChk && hdmiEvmCorrectChk->value.get();
            hdmiColorTestThread = std::thread([this, evm]() {
                // BI_RGB 32bpp DWORD layout: byte[0]=Blue, byte[1]=Green, byte[2]=Red.
                // With EVM CORRECT on, R↔B are swapped so the label matches the EVM LED.
                //   Normal : RED=0x00FF0000, BLUE=0x000000FF
                //   EVM    : RED=0x000000FF, BLUE=0x00FF0000  (physical wiring swap)
                struct Step { uint32_t normal; uint32_t evm; const char* name; };
                const Step steps[] = {
                    { 0x00FF0000u, 0x000000FFu, "RED"   },
                    { 0x0000FF00u, 0x0000FF00u, "GREEN" },
                    { 0x000000FFu, 0x00FF0000u, "BLUE"  },
                    { 0x00FFFFFFu, 0x00FFFFFFu, "WHITE" },
                };
                for (auto& s : steps) {
                    if (abortFlag) break;
                    logQ.push(std::string("[DISP] ") + s.name);
                    showSolid(evm ? s.evm : s.normal);
                    for (int i = 0; i < 200 && !abortFlag; i++)
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                hdmiSolidColor.store(0);
                requestHdmiRepaint();
                logQ.push("[DISP] Color test done");
            });
            hdmiColorTestThread.detach();
        }

        void runTestAnimation() {
            using namespace LithoControl::Raster;
            constexpr int W = 640, H = 360;
            hdmiTestBGRA.resize(W * H * 4, 0);

            std::vector<uint8_t> frame(W * H * 4, 0);

            static BitmapFont font;
            if (!font.loaded) {
                // Embedded via Rev's File()/resource-atlas pipeline (see
                // Rev/scripts/Create_Resource_Modules.py) -- the TTF bytes are
                // baked into the binary at build time, so the marquee text
                // renders even when LithoRev.exe is copied to a machine with
                // no Rev source tree present.
                auto ttf = File("Rev/resources/Fonts/Roboto/Roboto.ttf");
                font.load(ttf.data, ttf.size, 14.0f);
            }

            // SMPTE bar defs
            struct BarDef { int x0, x1; uint8_t r, g2, b; };
            const BarDef bars[] = {
                {   0,  91, 255, 255, 255 },  // White
                {  92, 182, 255, 255,   0 },  // Yellow
                { 183, 273,   0, 255, 255 },  // Cyan
                { 274, 365,   0, 255,   0 },  // Green
                { 366, 456, 255,   0, 255 },  // Magenta
                { 457, 547, 255,   0,   0 },  // Red
                { 548, 639,   0,   0, 255 },  // Blue
            };

            // Scrolling marquee (ASCII only -- the bitmap font atlas covers 32..127)
            const std::string marquee =
                "  LITHOCONTROL  *  640x360  *  RGB TEST PATTERN  "
                "*  FOCUS: CENTRE CHECKERBOARD  *  ";

            float marqueeW = font.measure(marquee);

            auto t0 = std::chrono::steady_clock::now();
            constexpr float PI = 3.14159265f;

            while (hdmiTestRunning.load()) {
                auto now = std::chrono::steady_clock::now();
                float t = (float)std::chrono::duration<double>(now - t0).count();

                clear(frame.data(), W, H, { 0, 0, 0, 255 });

                // SMPTE bars (top 2/3)
                for (const auto& b : bars) {
                    fillRect(frame.data(), W, H, b.x0, 0, b.x1 - b.x0 + 1, 240, { b.b, b.g2, b.r, 255 });
                }

                // Separator
                drawLine(frame.data(), W, H, 0, 239, (float)W, 239, { 255, 255, 255, 80 }, 1.0f);

                // Checkerboard focus target (8 px squares, 320×80, centred in bottom strip)
                {
                    constexpr int CBX = (W - 320) / 2, CBY = 260, CBS = 8;
                    for (int cy = 0; cy < 10; cy++)
                        for (int cx = 0; cx < 40; cx++)
                            if ((cx + cy) % 2 == 0)
                                fillRect(frame.data(), W, H, CBX + cx*CBS, CBY + cy*CBS, CBS, CBS, { 255, 255, 255, 255 });
                }

                // Grid on bottom strip (dim)
                {
                    for (int x = 0; x < W; x += 80) drawLine(frame.data(), W, H, (float)x, 240, (float)x, (float)H, { 255, 255, 255, 45 }, 1.0f);
                    for (int y = 240; y < H; y += 40) drawLine(frame.data(), W, H, 0, (float)y, (float)W, (float)y, { 255, 255, 255, 45 }, 1.0f);
                }

                // Crosshair
                drawLine(frame.data(), W, H, 0, H/2.0f, (float)W, H/2.0f, { 255, 255, 255, 110 }, 1.0f);
                drawLine(frame.data(), W, H, W/2.0f, 0, W/2.0f, (float)H, { 255, 255, 255, 110 }, 1.0f);

                // Corner rotors — 4 corners, each with distinct colour and spin rate
                struct Rotor { float x, y, rpm; uint8_t r, g2, b; };
                const Rotor rotors[] = {
                    {  50.f,  50.f,  20.f, 255, 255, 255 },  // TL white
                    { 590.f,  50.f, -25.f, 255, 220,   0 },  // TR yellow, opposite spin
                    {  50.f, 310.f,  30.f,   0, 180, 255 },  // BL cyan
                    { 590.f, 310.f, -18.f, 255,  70,  70 },  // BR red, opposite spin
                };
                for (const auto& ro : rotors) {
                    Color col    { ro.b, ro.g2, ro.r, 255 };
                    Color colDim { ro.b, ro.g2, ro.r, 70 };

                    // Black backing disc
                    fillCircle(frame.data(), W, H, ro.x, ro.y, 40.f, { 0, 0, 0, 210 });

                    // Outer ring
                    strokeCircle(frame.data(), W, H, ro.x, ro.y, 38.f, colDim, 1.5f);

                    // Tick marks (12, every 30°)
                    for (int i = 0; i < 12; i++) {
                        float a = i * PI / 6.0f;
                        float ri = (i % 3 == 0) ? 32.f : 35.f;
                        drawLine(frame.data(), W, H,
                            ro.x + std::cos(a)*ri, ro.y + std::sin(a)*ri,
                            ro.x + std::cos(a)*38.f, ro.y + std::sin(a)*38.f, colDim, 1.0f);
                    }

                    // 4 rotating spokes
                    float angle = t * ro.rpm * 2.0f * PI / 60.0f;
                    for (int i = 0; i < 4; i++) {
                        float a = angle + i * PI * 0.5f;
                        drawLine(frame.data(), W, H,
                            ro.x, ro.y,
                            ro.x + std::cos(a)*33.f, ro.y + std::sin(a)*33.f, col, 2.0f);
                    }

                    // Pulsing centre dot
                    float pulse = 0.5f + 0.5f * std::sin(t * 5.0f + ro.x * 0.05f);
                    float dr = 2.5f + pulse * 2.5f;
                    fillCircle(frame.data(), W, H, ro.x, ro.y, dr, col);
                }

                // Scrolling marquee strip (bottom 18 px)
                {
                    fillRect(frame.data(), W, H, 0, 342, W, 18, { 0, 0, 0, 210 });

                    float scrollX = std::fmod(t * 80.0f, marqueeW);
                    Color textCol { 210, 210, 210, 255 };
                    font.draw(frame.data(), W, H, marquee, -scrollX, 356.f, textCol, 0, 342, W, 360);
                    font.draw(frame.data(), W, H, marquee, marqueeW - scrollX, 356.f, textCol, 0, 342, W, 360);
                }

                {
                    std::lock_guard<std::mutex> lk(hdmiFrameMtx);
                    hdmiTestBGRA = frame;
                }

                requestHdmiRepaint();
                requestRepaint();

                std::this_thread::sleep_for(std::chrono::milliseconds(33));  // ~30 fps
            }
        }

        void toggleTestImage() {
            bool nowActive = !hdmiTestActive.load();
            if (nowActive) {
                hdmiCustomImageActive.store(false);
                hdmiSolidColor.store(0);
                hdmiTestActive.store(true);
                hdmiTestRunning.store(true);
                hdmiTestThread = std::thread([this]() { runTestAnimation(); });
            } else {
                hdmiTestRunning.store(false);
                if (hdmiTestThread.joinable()) hdmiTestThread.join();
                hdmiTestActive.store(false);
                requestHdmiRepaint();
            }
            if (hdmiTestBtn) {
                hdmiTestBtn->styles.remove(&Theme::Btn);
                hdmiTestBtn->styles.remove(&Theme::BtnHover);
                hdmiTestBtn->styles.remove(&Theme::BtnAccent);
                hdmiTestBtn->styles.remove(&Theme::BtnAccentHover);
                if (nowActive) {
                    hdmiTestBtn->styles.add(&Theme::BtnAccent);
                    hdmiTestBtn->styles.add(&Theme::BtnAccentHover);
                } else {
                    hdmiTestBtn->styles.add(&Theme::Btn);
                    hdmiTestBtn->styles.add(&Theme::BtnHover);
                }
            }
            logQ.push(nowActive ? "[DISP] RGB test animation ON" : "[DISP] RGB test animation OFF");
        }

        // Lets the user pick any image file and show it on the projector in
        // place of the built-in RGB test pattern -- useful for checking focus
        // or color reproduction against their own reference artwork rather
        // than only the generated test pattern.
        void loadCustomImage() {
            if (!hdmiPassthrough()) { logQ.push("[DISP] Open projector window first"); return; }

            std::string path;
            if (!Rev::OS::Dialog::OpenFile(path, "Open Test Image",
                    "Images\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0")) return;

            std::vector<uint8_t> rgba;
            if (!LithoControl::decodeToRGBAResized(path, rgba, 640, 360)) {
                logQ.push("[DISP] Failed to load image: " + path);
                return;
            }

            std::vector<uint8_t> bgra(640u * 360u * 4u);
            for (size_t i = 0; i < 640u * 360u; i++) {
                bgra[i * 4 + 0] = rgba[i * 4 + 2];  // B
                bgra[i * 4 + 1] = rgba[i * 4 + 1];  // G
                bgra[i * 4 + 2] = rgba[i * 4 + 0];  // R
                bgra[i * 4 + 3] = rgba[i * 4 + 3];  // A
            }

            if (hdmiTestActive.load()) {
                hdmiTestRunning.store(false);
                if (hdmiTestThread.joinable()) hdmiTestThread.join();
                hdmiTestActive.store(false);
                if (hdmiTestBtn) {
                    hdmiTestBtn->styles.remove(&Theme::BtnAccent);
                    hdmiTestBtn->styles.remove(&Theme::BtnAccentHover);
                    hdmiTestBtn->styles.add(&Theme::Btn);
                    hdmiTestBtn->styles.add(&Theme::BtnHover);
                }
            }
            hdmiSolidColor.store(0);

            {
                std::lock_guard<std::mutex> lk(hdmiFrameMtx);
                hdmiCustomImageBGRA = std::move(bgra);
            }
            hdmiCustomImageActive.store(true);
            requestHdmiRepaint();
            logQ.push("[DISP] Custom test image loaded: " +
                      std::filesystem::path(path).filename().string());
        }

        // -- Camera panel ------------------------------------------------------


        // Implemented per-platform in CameraCapture.win.cpp (Windows Media
        // Foundation) / CameraCapture.lnx.cpp (V4L2).
        void scanCameras();
        void startCamera();
        void stopCamera();
        void runCameraCapture(int deviceIdx);

        // Windows only, implemented in CameraCapture.win.cpp via
        // Rev.AmcamCamera -- the AmScope vendor-SDK capture path (used for
        // the MU130 and other AmScope cameras) selected when the chosen
        // cameraDrop entry carries an "amcam:" id. No Linux implementation;
        // never referenced from the Linux build.
        void runCameraCaptureAmcam(const std::string& deviceId);

        // Extended UVC controls (exposure/gain-ISO) -- implemented per-
        // platform alongside the capture loop that owns the device handle.
        // No-ops while the AmScope backend is active or no camera is running.
        void setCameraExposure(int value);
        void setCameraGain(int value);
        void setCameraAutoExposure(bool enabled);

        void toggleCamera() {
            if (cameraRunning) {
                stopCamera();
            } else {
                startCamera();
            }
        }

        // -- Slicer panel ------------------------------------------------------


        // Show the "send to target" checkbox only on the Pi platform. Remove it from
        // the layout entirely on STM32 so it leaves no gap.

        // -- Job panel ---------------------------------------------------------


        // -- Jog panel ---------------------------------------------------------


        // -- Right panel -------------------------------------------------------


        // ---------------------------------------------------------------------
        // Rev compute overrides
        // ---------------------------------------------------------------------

        void computeStyle(Rev::Element::Event& e) override {

            // Taskbar/title-bar icon (Windows only -- Linux has no WM_SETICON
            // equivalent; window titling itself is now handled cross-platform by
            // Window::Details.name at construction, see main.cpp). Runs once, as
            // soon as the native window handle exists.
#ifdef _WIN32
            if (!iconApplied && ownerWindow && ownerWindow->window && ownerWindow->window->handle) {
                iconApplied = true;
                {
                    HWND hwnd = ownerWindow->window->handle;
                    HINSTANCE hinst = GetModuleHandleW(nullptr);
                    HICON big   = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(1),
                                                    IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
                    HICON small_ = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(1),
                                                    IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
                    if (big)    SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)big);
                    if (small_) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_);
                }
            }
#endif

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

            // Reset the APPLY EDID button label after the edid worker finishes.
            if (pendingEdidDone.exchange(false)) {
                edidApplying.store(false);
                if (edidBtnTxt) edidBtnTxt->content = "APPLY EDID";
            }

            // Re-scan the jobs directory after a successful slice (main thread only).
            if (pendingJobRescan.exchange(false)) {
                refreshJobList();
            }

            // Apply position query results from background thread
            if (posUpdatePending.exchange(false)) {
                std::lock_guard<std::mutex> lk(posMtx);
                if (posXLabel) posXLabel->content = pendingPosX;
                if (posYLabel) posYLabel->content = pendingPosY;
                if (posZLabel) posZLabel->content = pendingPosZ;
            }

            // Reload preview image with tile grid after a successful slice
            if (pendingPreviewReload.exchange(false)) {
                if (previewImg && !inputFilePath.empty()) {
                    previewImg->setGrid(pendingTileX.load(), pendingTileY.load());
                    previewImg->loadFile(inputFilePath);
                }
            }

            // Drain log queue -> update displayed text
            std::string msg;
            bool runnerUpdated = false;
            while (logQ.pop(msg)) {
                std::time_t nowT = std::time(nullptr);
                std::tm lt = *std::localtime(&nowT);
                char ts[12];
                std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d ", lt.tm_hour, lt.tm_min, lt.tm_sec);
                logLines.push_back(std::string(ts) + msg);
                if (logLines.size() > MAX_LOG) logLines.pop_front();
                runnerUpdated = true;
            }
            // Only rebuild the text when new lines arrived -- reassigning content
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

            // Keep overlay active flag and slider UI in sync
            if (previewImg) {
                previewImg->overlayActive = cameraRunning.load();

                // Sync zoom slider fill and label from current zoom level
                if (zoomSliderFill && zoomLabel && previewImg->zoom > 0.0f) {
                    float t = std::log(previewImg->zoom / 0.1f) / std::log(200.0f);
                    t = std::clamp(t, 0.0f, 1.0f);
                    zoomSliderFill->style->size.width = Pct(t * 100.0f);
                    char buf[12];
                    std::snprintf(buf, sizeof(buf), "%.2gx", (double)previewImg->zoom);
                    zoomLabel->content = buf;
                }
                if (overlaySliderFill)
                    overlaySliderFill->style->size.width = Pct(previewImg->overlayOpacity * 100.0f);
                if (overlayPctLabel) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "%d%%", (int)(previewImg->overlayOpacity * 100.0f + 0.5f));
                    overlayPctLabel->content = buf;
                }
            }

            // Mirror flip checkbox UI into atomics the capture thread can read
            // safely (see applyCameraAdjustments()). Rotation/resolution/FPS
            // and brightness/contrast are button groups and drag-sliders that
            // write straight into their atomics on click/drag; only the fill
            // bar/label visuals need syncing here.
            if (cameraFlipHChk) cameraFlipH = cameraFlipHChk->value.get();
            if (cameraFlipVChk) cameraFlipV = cameraFlipVChk->value.get();

            if (cameraBrightFill || cameraBrightLabel) {
                int val = cameraBrightness.load();
                float t = std::clamp((val + 100.0f) / 200.0f, 0.0f, 1.0f);
                if (cameraBrightFill) cameraBrightFill->style->size.width = Pct(t * 100.0f);
                if (cameraBrightLabel) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%+d", val);
                    cameraBrightLabel->content = buf;
                }
            }
            if (cameraContrastFill || cameraContrastLabel) {
                int val = cameraContrastPct.load();
                float t = std::clamp(val / 300.0f, 0.0f, 1.0f);
                if (cameraContrastFill) cameraContrastFill->style->size.width = Pct(t * 100.0f);
                if (cameraContrastLabel) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "%d%%", val);
                    cameraContrastLabel->content = buf;
                }
            }

            // Extended UVC settings: mirror the auto-exposure checkbox into
            // the hardware only on change (not every frame).
            if (cameraAutoExpoChk) {
                bool chk = cameraAutoExpoChk->value.get();
                if (chk != cameraAutoExposure.load()) {
                    cameraAutoExposure = chk;
                    setCameraAutoExposure(chk);
                }
            }
            if (cameraExposureFill || cameraExposureLabel) {
                int lo = cameraExposureMin.load(), hi = cameraExposureMax.load();
                int val = cameraExposureVal.load();
                float t = (hi > lo) ? std::clamp((float)(val - lo) / (float)(hi - lo), 0.0f, 1.0f) : 0.0f;
                if (cameraExposureFill) cameraExposureFill->style->size.width = Pct(t * 100.0f);
                if (cameraExposureLabel)
                    cameraExposureLabel->content = (hi > lo) ? std::to_string(val) : "not supported by this camera";
            }
            if (cameraGainFill || cameraGainLabel) {
                int lo = cameraGainMin.load(), hi = cameraGainMax.load();
                int val = cameraGainVal.load();
                float t = (hi > lo) ? std::clamp((float)(val - lo) / (float)(hi - lo), 0.0f, 1.0f) : 0.0f;
                if (cameraGainFill) cameraGainFill->style->size.width = Pct(t * 100.0f);
                if (cameraGainLabel)
                    cameraGainLabel->content = (hi > lo) ? std::to_string(val) : "not supported by this camera";
            }

            // Extended settings only apply to generic UVC cameras (Windows
            // Media Foundation / the sole Linux backend) -- keep the section
            // detached (no layout space, same as platformRowSlot) whenever
            // the AmScope SDK backend is active or no camera is running.
            if (cameraExtendedSlot && cameraExtendedBox) {
                bool wantExtended = cameraRunning.load() && cameraBackend != CameraBackend::AmScope;
                auto& kids = cameraExtendedSlot->children;
                bool present = std::find(kids.begin(), kids.end(), (Element*)cameraExtendedBox) != kids.end();
                if (wantExtended && !present) cameraExtendedSlot->addChild(cameraExtendedBox);
                else if (!wantExtended && present) cameraExtendedSlot->removeChild(cameraExtendedBox);
            }

            // Drain live camera frame into preview
            if (cameraFrameReady.exchange(false) && previewImg) {
                std::lock_guard<std::mutex> lk(cameraFrameMtx);
                if (cameraFrameW > 0)
                    previewImg->stagePixelsLive(cameraFrameRGBA, cameraFrameW, cameraFrameH);
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
            // Measure content height from the child spread -- an absolutely-positioned
            // element's own rect.h is clamped to its parent, which would zero maxScroll.
            if (sidebarBox && sidebarContent && sidebarScrollThumb) {
                float trackH   = sidebarBox->rect.h;
                float contentH = measureSpread(sidebarContent);
                if (contentH <= 0.0f) contentH = trackH;
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

            // Job list scrollbar: size and position thumb, pin track to right edge.
            if (jobListBox && jobListInner && jobListScrollTrack && jobListScrollThumb) {
                float trackH   = jobListBox->rect.h;
                float contentH = measureSpread(jobListInner);
                if (contentH <= 0.0f) contentH = trackH;
                // Clamp scroll after window resize
                float maxScroll = (std::max)(0.0f, contentH - trackH);
                if (jobListScrollY > maxScroll) {
                    jobListScrollY = maxScroll;
                    jobListInner->style->position.top = Px(-jobListScrollY);
                }
                // Pin track to right edge of the list box
                float trackX = jobListBox->rect.w - 5.0f;
                if (trackX < 0.0f) trackX = 0.0f;
                jobListScrollTrack->style->position.left = Px(trackX);
                // Show/hide thumb based on whether content overflows
                bool scrollable = contentH > trackH + 1.0f;
                jobListScrollTrack->style->visibility = scrollable ? Visibility::Visible : Visibility::Hidden;
                if (scrollable) {
                    float ratio    = trackH / contentH;
                    float thumbH   = (std::max)(16.0f, ratio * (trackH - 4.0f));
                    float thumbTop = (maxScroll > 0.0f)
                        ? (jobListScrollY / maxScroll) * (trackH - 4.0f - thumbH)
                        : 0.0f;
                    jobListScrollThumb->style->size.height  = Px(thumbH);
                    jobListScrollThumb->style->position.top = Px(thumbTop);
                }
            }

            // Sync platform dropdown -> mark for row swap. Do NOT mutate the tree here;
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

            // Search input change triggers a list rebuild
            if (jobSearchInput && jobSearchInput->text) {
                std::string cur = std::string(jobSearchInput->text->content);
                if (cur != jobFilter) {
                    jobFilter     = cur;
                    jobListDirty  = true;
                }
            }

            if (jobListDirty) {
                rebuildJobList();
                // Reset scroll when list changes so the top is always visible
                jobListScrollY = 0.0f;
                if (jobListInner) jobListInner->style->position.top = Px(0);
                jobListDirty = false;
            }

            // Recompute HDMI channel mask from R/G/B checkboxes.
            // When EVM CORRECT is on, R↔B are swapped to match the EVM's physical
            // wiring (BI_RGB byte[0] drives the Blue data lines → Red LED on the EVM;
            // BI_RGB byte[2] drives the Red data lines → Blue LED on the EVM).
            //   Normal  : R=0x00FF0000, G=0x0000FF00, B=0x000000FF  (standard display)
            //   EVM corr: R=0x000000FF, G=0x0000FF00, B=0x00FF0000  (labels match EVM LEDs)
            {
                bool evm = hdmiEvmCorrectChk && hdmiEvmCorrectChk->value.get();
                uint32_t mask = 0xFF000000u;
                if (hdmiRedChk   && hdmiRedChk->value.get())
                    mask |= evm ? 0x000000FFu : 0x00FF0000u;
                if (hdmiGreenChk && hdmiGreenChk->value.get())
                    mask |= 0x0000FF00u;
                if (hdmiBlueChk  && hdmiBlueChk->value.get())
                    mask |= evm ? 0x00FF0000u : 0x000000FFu;
                if (mask != hdmiChannelMask.load()) {
                    hdmiChannelMask.store(mask);
                    pushArtworkToHdmi();
                }
            }

            // Mirror HDMI flip checkboxes into atomics composeHdmiFrame()
            // reads (may run on the projector window's own thread).
            {
                bool fh = hdmiFlipHChk && hdmiFlipHChk->value.get();
                bool fv = hdmiFlipVChk && hdmiFlipVChk->value.get();
                if (fh != hdmiFlipH.load()) { hdmiFlipH.store(fh); requestHdmiRepaint(); }
                if (fv != hdmiFlipV.load()) { hdmiFlipV.store(fv); requestHdmiRepaint(); }
            }

            // Show/hide the display selector row based on the passthrough checkbox.
            // Use addChild/removeChild (slot pattern) so the row is fully absent from
            // the layout tree when hidden — this prevents the hit-area offset caused by
            // a zero-height Visibility::Hidden element lingering in row.members.
            if (hdmiPassthroughChk && hdmiDisplaySlot && hdmiDisplayRow) {
                bool wantVisible = hdmiPassthroughChk->value.get();
                auto& kids = hdmiDisplaySlot->children;
                bool inSlot = std::find(kids.begin(), kids.end(),
                                        (Element*)hdmiDisplayRow) != kids.end();
                if (wantVisible && !inSlot)  hdmiDisplaySlot->addChild(hdmiDisplayRow);
                if (!wantVisible && inSlot)  hdmiDisplaySlot->removeChild(hdmiDisplayRow);
            }

            Box::computeChildren(e);
        }

        // ---------------------------------------------------------------------
        // Connection
        // ---------------------------------------------------------------------

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
            onConnected();   // sets pendingJobRescan -> refreshJobList runs on main thread
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
        // NOT touch the element tree directly -- doing so races the render thread and
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
                // "FRAME n/total" -- set atomics only; frameLabel is rendered on the
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

        // ---------------------------------------------------------------------
        // Job list
        // ---------------------------------------------------------------------

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

            // Case-insensitive filter
            std::string filterLo = jobFilter;
            for (char& c : filterLo) c = (char)::tolower((unsigned char)c);

            for (auto& j : jobs) {
                if (!filterLo.empty()) {
                    std::string jLo = j;
                    for (char& c : jLo) c = (char)::tolower((unsigned char)c);
                    if (jLo.find(filterLo) == std::string::npos) continue;
                }

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

        // ---------------------------------------------------------------------
        // Job execution
        // ---------------------------------------------------------------------

        void startJob() {
            if (estopped) { logQ.push("E-STOP active -- press RESET first"); return; }
            // A previous job thread may still be unwinding (e.g. blocked in a gantry
            // readLine after an E-STOP). Never join() it on the UI thread -- that's what
            // froze the program. Refuse until it has actually finished.
            if (jobRunning) {
                logQ.push("Previous job still stopping -- wait for it to finish");
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

            // STM32 path -- execute in background thread
            if (!stmSerial) { logQ.push("Not connected to STM32"); return; }
            abortFlag = false;
            jobState  = JobState::Running;
            dispState = 1;

            // Use the SAME resolved (absolute) directory the queue scans, not the raw
            // "./jobs" text -- otherwise the job path is relative to the EXE's CWD and
            // the manifest can't be found.
            std::string dir     = resolveJobsDir();
            std::string jobName = selectedJob;

            if (jobThread.joinable()) jobThread.join();
            jobThread = std::thread([this, dir, jobName]() {
                runStm32Job(dir + "/" + jobName);
            });
            // Win32 thread handle so E-STOP can cancel a blocking serial read on it.
            jobThreadHandle = reinterpret_cast<void*>(jobThread.native_handle());
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
            if (jobRunning && jobThreadHandle) Rev::OS::ThreadControl::CancelBlockingIo(jobThreadHandle);
            if (void* gh = gantryThreadHandle.load()) Rev::OS::ThreadControl::CancelBlockingIo(gh);
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
                if (!stmSendWait("GANTRY " + gline + "\n", "OK", 130000)) return;
                if (!stmSendWait("GANTRY G4 P0\n", "OK", 130000)) return;

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

                // Expose via HDMI passthrough or STM32 serial
                if (hdmiPassthrough()) {
                    // Render frame to projector display, sleep expose duration, blank
                    renderBitmapToHdmi(bitmap);
                    int elapsed = 0;
                    while (elapsed < exposeMs) {
                        if (abortFlag) { blankHdmi(); return; }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        elapsed += 10;
                    }
                    blankHdmi();
                } else {
                    // Pattern upload + exposure as one atomic serial transaction.
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
            queryPosition();
            // Marshal the label/progress to the main thread (this is the job thread).
            frameN    = frameTotal.load();
            dispState = 2;
            jobState  = JobState::Idle;
        }

        // ---------------------------------------------------------------------
        // Jog
        // ---------------------------------------------------------------------

        // Run a gantry command on a detached thread while publishing its handle
        // so E-STOP can interrupt the blocking serial read it's parked on.
        void runGantryOp(std::function<void()> op) {
            std::thread([this, op]() {
                void* h = Rev::OS::ThreadControl::CurrentThreadHandle();
                gantryThreadHandle.store(h);
                op();
                gantryThreadHandle.store(nullptr);
                Rev::OS::ThreadControl::ReleaseThreadHandle(h);
            }).detach();
        }

        void jog(const std::string& axis, int dir) {
            if (!connFlag) { logQ.push("Not connected"); return; }
            if (estopped)  { logQ.push("E-STOP active -- press RESET first"); return; }

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
                    // stops -- an absolute G1 could run to a far coordinate. Restore
                    // absolute mode afterwards regardless of outcome.
                    if (!stmSendWait("GANTRY G91\n", "OK", 5000)) return;
                    bool ok = stmSendWait("GANTRY G1 " + cmd + "\n", "OK", 5000);
                    // G4 P0 dwell completes only after all buffered motion is done --
                    // without this, "OK" returns as soon as FluidNC accepts the G1
                    // command, before the axis has actually finished moving.
                    if (ok) ok = stmSendWait("GANTRY G4 P0\n", "OK", 130000);  // must exceed firmware 120s deadline
                    stmSendWait("GANTRY G90\n", "OK", 5000);
                    if (ok) { logQ.push("JOG_DONE"); queryPosition(); }
                } else if (piClient) {
                    piClient->sendLine("JOG " + cmd);
                }
            });
        }

        void sendHome() {
            if (!connFlag) { logQ.push("Not connected"); return; }
            if (estopped)  { logQ.push("E-STOP active -- press RESET first"); return; }
            logQ.push("-> HOME");
            runGantryOp([this]() {
                if (platform == Platform::STM32 && stmSerial) {
                    if (!stmSendWait("GANTRY G28\n", "OK", 130000)) return;
                    if (abortFlag) return;
                    stmSendWait("GANTRY G4 P0\n", "OK", 130000);
                    logQ.push("JOG_DONE");
                    queryPosition();
                } else if (piClient) {
                    piClient->sendLine("JOG HOME");
                }
            });
        }

        void sendEstop() {
            logQ.push("[!!!] E-STOP SENT");
            estopped  = true;
            abortFlag = true;

            // Interrupting the blocking read via ThreadControl::CancelBlockingIo
            // (Win32 CancelSynchronousIo / POSIX pthread_kill) is the correct way
            // to unblock it here. stmSerial->cancel() is deliberately NOT called
            // -- on Windows it (PurgeComm RXABORT + CancelIoEx) queues a second
            // ERROR_OPERATION_ABORTED completion on the handle, which then causes
            // the first two ReadFile calls in sendReset (BLANK, $X) to fail
            // immediately even after ClearCommError, leaving FluidNC locked. The
            // same call is used on Linux for consistency, though tcflush() there
            // doesn't have an identically-documented failure mode -- unverified
            // on real hardware, flag if sendReset misbehaves after E-STOP.
            if (jobRunning && jobThreadHandle) Rev::OS::ThreadControl::CancelBlockingIo(jobThreadHandle);
            if (void* gh = gantryThreadHandle.load()) Rev::OS::ThreadControl::CancelBlockingIo(gh);

            dispState = 3;          // computeStyle renders the E-STOP label
            jobState  = JobState::Idle;

            // ...but issue the serial/socket write on a detached thread. A synchronous
            // WriteFile with no write timeout (or one contended by the running job
            // thread) would otherwise block the UI thread and freeze the program.
            // This is an emergency write -- it deliberately skips the serial mutex.
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
            if (resetting.exchange(true)) return;  // ignore if already resetting

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
                    // Hold serialMtx for the ENTIRE reset sequence so no jog or job
                    // thread can interleave commands between the individual sends.
                    std::lock_guard<std::mutex> lk(serialMtx);

                    // Close and reopen the COM port. The STLink USB VCP driver can
                    // enter a state after E-STOP (0x18 soft-reset) where ReadFile
                    // returns immediately with empty data even after ClearCommError.
                    // Reopening the handle is the only reliable way to reset it.
                    std::string portName = stmSerial->port;
                    delete stmSerial;
                    stmSerial = nullptr;

                    // Give the STM32 USB VCP time to re-enumerate if it bounced,
                    // and give FluidNC time to finish its soft-reset startup sequence.
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

                    stmSerial = new Rev::Serial(portName, 115200);
                    if (!stmSerial->connected()) {
                        logQ.push("[ERR] RESET: failed to reopen " + portName);
                        delete stmSerial; stmSerial = nullptr;
                        connFlag = false;
                        resetting = false;
                        return;
                    }

                    auto sendOne = [&](const std::string& cmd, int ms) -> bool {
                        stmSerial->sendText(cmd);
                        std::string r = stmSerial->readLine(ms);
                        if (r != "OK") {
                            logQ.push("[ERR] '" + cmd.substr(0, 20) + "' got: " + r);
                            return false;
                        }
                        return true;
                    };

                    sendOne("BLANK\n", 3000);
                    // $X timeout must exceed FluidNC's post-reset init time. The STM32
                    // firmware loops up to 2 min waiting for FluidNC's "ok" — if the PC
                    // times out first the deferred "OK" poisons the next command's read.
                    sendOne("GANTRY $X\n", 15000);
                    sendOne("GANTRY G90\n", 5000);
                    logQ.push("READY");
                } else if (piClient) {
                    piClient->sendLine("RESET");
                }
                resetting = false;
            }).detach();
        }

        void sendSetHome() {
            if (!connFlag) { logQ.push("Not connected"); return; }
            if (estopped)  { logQ.push("E-STOP active -- press RESET first"); return; }
            logQ.push("-> SET_HOME");
            std::thread([this]() {
                if (platform == Platform::STM32 && stmSerial) {
                    if (stmSendWait("GANTRY G10 L20 P1 X0 Y0 Z0\n", "OK", 10000) &&
                        stmSendWait("GANTRY G28.1\n", "OK", 10000)) {
                        logQ.push("HOME_SET");
                        queryPosition();
                    }
                } else if (piClient) {
                    piClient->sendLine("SET_HOME");
                }
            }).detach();
        }

        void queryPosition() {
            if (!connFlag) { logQ.push("Not connected"); return; }
            std::thread([this]() {
                if (platform == Platform::STM32 && stmSerial) {
                    std::lock_guard<std::mutex> lk(serialMtx);
                    stmSerial->sendText("STATUS\n");
                    std::string r = stmSerial->readLine(1000);
                    if (r.empty() || r.rfind("ERROR", 0) == 0) {
                        logQ.push("[POS] " + (r.empty() ? "no response" : r));
                        return;
                    }
                    // Parse FluidNC status: <State|MPos:x,y,z|...> or <State|WPos:x,y,z|...>
                    // Look for MPos or WPos
                    auto extract = [&](const std::string& key) -> std::string {
                        auto p = r.find(key + ":");
                        if (p == std::string::npos) return "";
                        p += key.size() + 1;
                        auto end = r.find_first_of("|>", p);
                        return r.substr(p, end == std::string::npos ? std::string::npos : end - p);
                    };
                    // Prefer WPos (work coordinates) -- reflects G10/G92 offset.
                    // If only MPos+WCO are reported, compute WPos = MPos - WCO.
                    // FluidNC omits WCO from most ? responses (only when changed or every ~5
                    // queries), so cache the last-seen WCO and reuse it when absent.
                    std::string coords = extract("WPos");
                    if (coords.empty()) {
                        std::string mpos = extract("MPos");
                        std::string wco  = extract("WCO");
                        {
                            std::lock_guard<std::mutex> wlk(posMtx);
                            if (!wco.empty()) cachedWCO = wco;
                            else              wco = cachedWCO;
                        }
                        if (!mpos.empty()) {
                            float mx=0,my=0,mz=0, ox=0,oy=0,oz=0;
                            sscanf(mpos.c_str(), "%f,%f,%f", &mx, &my, &mz);
                            sscanf(wco.c_str(),  "%f,%f,%f", &ox, &oy, &oz);
                            char tmp[64];
                            std::snprintf(tmp, sizeof(tmp), "%f,%f,%f", mx-ox, my-oy, mz-oz);
                            coords = tmp;
                        }
                    } else {
                        // WPos reported directly; still update WCO cache if present
                        std::string wco = extract("WCO");
                        if (!wco.empty()) {
                            std::lock_guard<std::mutex> wlk(posMtx);
                            cachedWCO = wco;
                        }
                    }
                    if (coords.empty()) { logQ.push("[POS] " + r); return; }

                    float x = 0, y = 0, z = 0;
                    sscanf(coords.c_str(), "%f,%f,%f", &x, &y, &z);
                    char buf[64];
                    {
                        std::lock_guard<std::mutex> lk(posMtx);
                        std::snprintf(buf, sizeof(buf), "%.3f", x); pendingPosX = buf;
                        std::snprintf(buf, sizeof(buf), "%.3f", y); pendingPosY = buf;
                        std::snprintf(buf, sizeof(buf), "%.3f", z); pendingPosZ = buf;
                    }
                    posUpdatePending = true;
                }
            }).detach();
        }

        void scanPorts(bool autoSelect = true) {

            std::vector<Dropdown::Option> found;
            for (const auto& p : Rev::OS::SerialPort::List()) {
                found.push_back({ p.label, p.device });
                logQ.push("[SCAN] " + p.label);
            }

            if (found.empty()) {
                logQ.push("[SCAN] No COM ports found");
                return;
            }

            // Populate dropdown. Only auto-select when the user pressed SCAN -- on the
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

        // ---------------------------------------------------------------------
        // Slicer
        // ---------------------------------------------------------------------

        void browseFile() {
            std::string path;
            if (Rev::OS::Dialog::OpenFile(path, "Open Artwork",
                    "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.svg\0All Files\0*.*\0")) {
                inputFilePath = path;
                // Only update dlpRoot if the new file is inside the repo.
                // If the user browses an image from an arbitrary folder, keep the
                // previously-persisted repo root so slicer.py stays findable.
                {
                    std::string found = findDLPRoot(path);
                    if (!found.empty()) dlpRoot = found;
                }

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
            std::string path;
            if (Rev::OS::Dialog::PickFolder(path, "Select jobs folder")) {
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

        // Runs `cmd` (a full shell command line) with cwd as its working directory,
        // invoking onLine for each complete line of its merged stdout+stderr as it
        // arrives. Returns the process exit code, or -1 if the process couldn't be
        // started at all. Implemented per-platform in SlicerSubprocess.win.cpp
        // (CreateProcessA + pipe) / SlicerSubprocess.lnx.cpp (popen).
        static int runCapturedProcess(const std::string& cmd, const std::string& cwd,
                                       const std::function<void(const std::string&)>& onLine);

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

            // Find the DLP-photolithography repo root by walking up from the input file;
            // fall back to the persisted dlpRoot so files browsed outside the repo still
            // resolve slicer.py correctly.
            std::string repoRoot = findDLPRoot(inFile);
            if (repoRoot.empty()) repoRoot = dlpRoot;

            // Build absolute path to slicer.py
            std::string slicerScript = repoRoot.empty()
                ? "pc/slicer.py"
                : (repoRoot + "/pc/slicer.py");

            // Resolve outDir to an absolute path
            std::string absOutDir = outDir.empty() ? "jobs" : outDir;
            if (!fs::path(absOutDir).is_absolute() && !repoRoot.empty())
                absOutDir = (fs::path(repoRoot) / absOutDir).string();
            // Strip trailing backslashes -- a trailing \ before the closing " would
            // be interpreted as an escaped quote by Windows command-line parsing,
            // causing argparse to absorb subsequent arguments into the path value.
            while (!absOutDir.empty() &&
                   (absOutDir.back() == '\\' || absOutDir.back() == '/'))
                absOutDir.pop_back();

            // Most Linux distros only ship "python3" -- "python" is unaliased
            // unless python-is-python3 is installed. Windows Python installs
            // conventionally provide "python" (the py.org installer and the
            // Microsoft Store package both do).
#ifdef _WIN32
            const char* pythonExe = "python";
#else
            const char* pythonExe = "python3";
#endif
            std::string cmd = std::string(pythonExe) + " \"" + slicerScript + "\" \"" + inFile + "\""
                " --expose-ms " + exposeMs +
                " --feed-rate " + feedRate +
                " --threshold " + threshold +
                " --output-dir \"" + absOutDir + "\"";
            if (!jobName.empty())   cmd += " --job-name \"" + jobName + "\"";
            if (invert)             cmd += " --invert";

            // slicer.py SCPs the job to the Pi by default -- suppress that unless the
            // user explicitly enabled "send to target" (Pi platform only).
            if (sendToTarget) {
                cmd += " --pi-host " + piHost;
                logQ.push("Will send job to " + piHost + " after slicing");
            } else {
                cmd += " --no-send";
            }

            int parsedCols = 0, parsedRows = 0;
            int exitCode = runCapturedProcess(cmd, repoRoot, [&](const std::string& line) {
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
            });

            if (exitCode < 0) {
                logQ.push("[ERR] Failed to start slicer -- is Python in PATH?");
                pendingSliceDone = true;   // reset SLICE label on main thread
                return;
            }

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

        // ---------------------------------------------------------------------
        // Projector EDID apply (pc/apply_edid.bat)
        // ---------------------------------------------------------------------

        void applyEdid() {
            if (edidApplying.load()) return;
            edidApplying.store(true);
            if (edidBtnTxt) edidBtnTxt->content = "APPLYING...";
            logQ.push("Applying projector EDID override (UAC prompt expected)...");

            if (edidThread.joinable()) edidThread.join();
            std::string repoRoot = dlpRoot;
            edidThread = std::thread([this, repoRoot]() {
                // An uncaught exception on this thread (e.g. from std::filesystem)
                // calls std::terminate and takes the whole app down silently --
                // catch here so a failure shows up in the log instead of a crash.
                try {
                    runEdidApplySubprocess(repoRoot);
                } catch (const std::exception& e) {
                    logQ.push(std::string("[ERR] EDID apply threw: ") + e.what());
                    pendingEdidDone = true;
                } catch (...) {
                    logQ.push("[ERR] EDID apply threw an unknown exception");
                    pendingEdidDone = true;
                }
            });
        }

        // Pushes lines from a log file to logQ, skipping the first `skipLines`
        // (already-pushed) lines. Returns the file's current total line count,
        // so the caller can pass it back in as `skipLines` on the next tail.
        size_t tailLogFile(const std::string& path, size_t skipLines) {
            std::ifstream log(path);
            if (!log) return skipLines;
            std::string line;
            size_t i = 0;
            while (std::getline(log, line)) {
                if (i >= skipLines && !line.empty()) logQ.push(line);
                ++i;
            }
            return i;
        }

        // Elevates pc/apply_edid.bat via UAC (writing the EDID override lives under
        // HKLM) and, after confirmation, pc/restart_driver.bat. Windows-only --
        // there's no Linux equivalent to a CRU-based EDID override or a Windows
        // display driver restart; a custom projector mode on Linux would go
        // through XRandR instead (see Rev::OS::Display::SetMode), a different
        // enough mechanism that it's not a drop-in port. Implemented in
        // EdidApply.win.cpp / EdidApply.lnx.cpp (Linux: logs unsupported).
        void runEdidApplySubprocess(const std::string& repoRoot);

        // ---------------------------------------------------------------------
        // Settings persistence (INI file in %APPDATA%)
        // ---------------------------------------------------------------------

        std::string settingsPath() {
            namespace fs = std::filesystem;
#ifdef _WIN32
            const char* base = std::getenv("APPDATA");
            fs::path dir = fs::path(base ? base : ".") / "LithoControl";
#else
            const char* home = std::getenv("HOME");
            fs::path dir = fs::path(home ? home : ".") / ".config" / "LithoControl";
#endif
            std::error_code ec;
            fs::create_directories(dir, ec);
            return (dir / "settings.ini").string();
        }

        // Minimal portable INI reader/writer -- a single flat [section], no
        // comments or nesting, which is all settings.ini actually needs. Replaces
        // GetPrivateProfileString/WritePrivateProfileString (Windows-only).
        static std::vector<std::pair<std::string, std::string>> readIniSection(
                const std::string& path, const std::string& section) {
            std::vector<std::pair<std::string, std::string>> kv;
            std::ifstream f(path);
            if (!f) return kv;

            std::string line;
            bool inSection = false;
            while (std::getline(f, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                    line.pop_back();
                size_t start = line.find_first_not_of(" \t");
                if (start == std::string::npos) continue;
                line = line.substr(start);
                if (line.empty()) continue;

                if (line.front() == '[' && line.back() == ']') {
                    inSection = (line.substr(1, line.size() - 2) == section);
                    continue;
                }
                if (!inSection) continue;

                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                kv.push_back({ line.substr(0, eq), line.substr(eq + 1) });
            }
            return kv;
        }

        static void writeIniSection(const std::string& path, const std::string& section,
                const std::vector<std::pair<std::string, std::string>>& kv) {
            std::ofstream f(path, std::ios::trunc);
            f << "[" << section << "]\n";
            for (auto& [k, v] : kv) f << k << "=" << v << "\n";
        }

        void loadSettings() {
            std::string ini = settingsPath();
            auto kv = readIniSection(ini, "LithoControl");
            auto gi = [&](const char* k, int def) -> int {
                for (auto& [key, val] : kv) {
                    if (key == k) { try { return std::stoi(val); } catch (...) { return def; } }
                }
                return def;
            };
            auto gs = [&](const char* k, const char* def) -> std::string {
                for (auto& [key, val] : kv) if (key == k) return val;
                return def;
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
            settings.calibDtRoot = gs("calibDtRoot", "");
        }

        void saveSettings() {
            std::string ini = settingsPath();
            std::vector<std::pair<std::string, std::string>> kv;
            auto wi = [&](const char* k, int v) { kv.push_back({ k, std::to_string(v) }); };
            auto ws = [&](const char* k, const std::string& v) { kv.push_back({ k, v }); };
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
            ws("calibDtRoot",  calibDtRoot);
            writeIniSection(ini, "LithoControl", kv);
        }

        // ---------------------------------------------------------------------
        // Helpers
        // ---------------------------------------------------------------------

        // Create a labelled TextInput with dark-theme label colour

        // Give the field a fixed compact height with horizontal-only padding, so it
        // can't grow vertically during relayout. LrtbStyle order is L,R,T,B; the
        // container centers its text vertically within the fixed height.

        // Create a button Box

        // Create a fixed-size jog D-pad button

        // Send serial command, wait for exact response token
        bool stmSendWait(const std::string& cmd, const std::string& expect, int timeoutMs) {
            if (!stmSerial) return false;
            // Hold the serial mutex for the whole send->reply so a concurrent jog or
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

        // PNG file -> 640x360 1bpp bitmap (28800 bytes).
        // Job frames use red-channel-only PNGs (R=255 = expose, R=0 = mask).
        // PIXEL_ON = 0xF800 (pure red in RGB565) drives the blue LED via the
        // LTDC_R* -> EVM Blue wiring. Using max(R,G,B) makes this work for any
        // single-channel or white-pixel frame format.
        std::vector<uint8_t> pngToBitmap(const std::string& path) {
            const int W = 640, H = 360, BYTES = W * H / 8;

            std::vector<uint8_t> px;
            if (!LithoControl::decodeToRGBAResized(path, px, W, H)) return std::vector<uint8_t>(BYTES, 0);

            std::vector<uint8_t> result(BYTES, 0);
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    const uint8_t* p = px.data() + (y * W + x) * 4;
                    uint8_t luma = p[0] > p[1] ? p[0] : p[1];
                    if (p[2] > luma) luma = p[2];
                    if (luma >= 128) {
                        int i = y * W + x;
                        result[i >> 3] |= (1 << (7 - (i & 7)));
                    }
                }
            }

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

        // Guesses the calib-dt repo as a sibling of the DLP-photolithography
        // repo (both are typically checked out side-by-side under the same
        // GitHub folder) -- only used to prefill the Calibration Actions
        // panel's folder field; the user can always browse to override it.
        static std::string findCalibDtRoot(const std::string& dlpRootPath) {
            namespace fs = std::filesystem;
            if (dlpRootPath.empty()) return "";
            fs::path candidate = fs::path(dlpRootPath).parent_path() / "calib-dt";
            if (fs::exists(candidate / "pyproject.toml")) return candidate.string();
            return "";
        }

        // Runs a calib-dt CLI script (`uv run <scriptArgs>`) with calibDtRoot
        // as the working directory, streaming its output into the log --
        // same subprocess mechanism as runSlicerSubprocess(), just pointed at
        // a different repo. Call from a detached thread (see startCalibDtAction).
        void runCalibDtSubprocess(const std::string& scriptArgs) {
            if (calibDtRoot.empty()) {
                logQ.push("[CALIB] Set the calib-dt folder first");
                return;
            }
            std::string cmd = "uv run " + scriptArgs;
            logQ.push("[CALIB] $ " + cmd);
            int exitCode = runCapturedProcess(cmd, calibDtRoot, [this](const std::string& line) {
                logQ.push("[CALIB] " + line);
            });
            logQ.push(exitCode == 0 ? "[CALIB] Done" : "[CALIB] Exit code " + std::to_string(exitCode));
        }

        void startCalibDtAction(const std::string& scriptArgs) {
            std::thread([this, scriptArgs]() { runCalibDtSubprocess(scriptArgs); }).detach();
        }

        // Toggles the calibration grid on LithoRev's own HDMI window (see
        // hdmiCalibGridActive/generateCalibGrid()) -- a quick way to check
        // focus/alignment without running the full calib-dt capture+fit.
        void toggleCalibGrid() {
            if (!hdmiPassthrough()) { logQ.push("[CALIB] Open projector window first"); return; }
            bool now = !hdmiCalibGridActive.load();
            hdmiCalibGridActive.store(now);
            requestHdmiRepaint();
            logQ.push(now ? "[CALIB] Grid ON" : "[CALIB] Grid OFF");
        }

        // Projects the calibration grid through LithoRev's own HDMI window,
        // waits briefly to settle, then runs calib-dt's camera capture+fit
        // WITHOUT calib-dt's own --project-pattern/--projector-monitor --
        // those would open a second, uncoordinated fullscreen window
        // fighting for the same monitor instead of sharing this one (and its
        // Flip H/V). Turns the grid back off once the subprocess exits,
        // success or failure. Call via startCalibrationWithGrid() (detached
        // thread) -- this blocks on the subprocess.
        void runCalibrationWithGrid() {
            if (!hdmiPassthrough()) { logQ.push("[CALIB] Open projector window first"); return; }
            hdmiCalibGridActive.store(true);
            requestHdmiRepaint();
            logQ.push("[CALIB] Projecting calibration grid...");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            runCalibDtSubprocess("run-calibration --camera --out calibration_output");
            hdmiCalibGridActive.store(false);
            requestHdmiRepaint();
        }

        void startCalibrationWithGrid() {
            std::thread([this]() { runCalibrationWithGrid(); }).detach();
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
            Rev::OS::Clipboard::SetText(text);
        }
    };

} // namespace LithoControl
