module;

// Win32 + GDI+ for file dialogs and PNG→bitmap conversion
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
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
        std::mutex             mtx;
        std::deque<std::string> q;
        void push(std::string s) { std::lock_guard g(mtx); q.push_back(std::move(s)); }
        bool pop(std::string& out) {
            std::lock_guard g(mtx);
            if (q.empty()) return false;
            out = std::move(q.front()); q.pop_front(); return true;
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
        std::string selectedJob;
        std::vector<std::string> jobs;
        bool jobListDirty = false;

        std::atomic<int>  frameN     { 0 };
        std::atomic<int>  frameTotal { 0 };
        std::atomic<bool> abortFlag  { false };
        std::thread       jobThread;

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

        // ── UI element pointers ──────────────────────────────────────────────

        // Connection
        Dropdown*  platformDrop   = nullptr;
        Box*       stmPortRow     = nullptr;
        TextInput* portInput      = nullptr;
        Box*       piHostRow      = nullptr;
        TextInput* hostInput      = nullptr;
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
        Text*  runnerLogTxt  = nullptr;
        Text*  gcodeLogTxt   = nullptr;

        // ── Constructor ──────────────────────────────────────────────────────

        Interface(Element* parent) : Box(parent) {

            Gdiplus::GdiplusStartupInput gi;
            Gdiplus::GdiplusStartup(&gdipToken, &gi, nullptr);

            this->style->layout           = { Axis::Horizontal, Align::Start, Align::Start };
            this->style->size             = { 100_pct, 100_pct };
            this->style->background.color = rgba(13, 13, 13, 1);

            loadSettings();
            buildSidebar();
            buildRightPanel();
            refreshJobList();
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

            Box* sb = new Box(this, { &Theme::SidebarRoot });

            // Title
            Text* title = new Text(sb, "LITHOCONTROL  v2.0");
            title->style->size.width     = 100_pct;
            title->style->padding        = { 10_px, 8_px, 10_px, 10_px };
            title->style->text.color     = rgba(232, 232, 232, 1);
            title->style->text.size      = 12_px;
            title->style->border.bottom.color = rgba(42, 42, 42, 1);
            title->style->border.bottom.width = 1_px;

            Box* connBody = nullptr;
            makeSection(sb, "CONNECTION", connBody, false);
            buildConnectionPanel(connBody);

            Box* slicerBody = nullptr;
            makeSection(sb, "SLICER", slicerBody, false);
            buildSlicerPanel(slicerBody);

            Box* jobBody = nullptr;
            makeSection(sb, "JOB QUEUE", jobBody, false);
            buildJobPanel(jobBody);

            Box* jogBody = nullptr;
            makeSection(sb, "JOG", jogBody, false);
            buildJogPanel(jogBody);
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
            body->style->visibility = collapsed ? Visibility::Hidden : Visibility::Visible;

            hdr->onMouseDown([arrow, body](Rev::Element::Event& e) {
                bool vis = (body->style->visibility == Visibility::Visible);
                body->style->visibility = vis ? Visibility::Hidden : Visibility::Visible;
                arrow->content = vis ? ">" : "v";
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

            platformDrop->onMouseDown([this](Rev::Element::Event& e) {
                // Handled in computeStyle via params.value
            });

            // STM32 port row
            stmPortRow = new Box(body, { &Theme::RowH });
            stmPortRow->style->margin.top = 6_px;

            portInput = new TextInput(stmPortRow, {
                .label = "COM PORT",
                .placeholder = settings.port.empty() ? "COM3" : settings.port,
                .maxLength = 20
            });

            Box* scanBtn = makeBtn(stmPortRow, "SCAN", [this]() { scanPorts(); }, false, false, false);
            scanBtn->style->size.width = 60_px;
            scanBtn->style->size.max.width = 60_px;
            (void)scanBtn;

            // Pi host row
            piHostRow = new Box(body, { &Theme::RowH });
            piHostRow->style->margin.top = 6_px;

            hostInput = new TextInput(piHostRow, {
                .label = "HOST / IP",
                .placeholder = settings.piHost,
                .maxLength = 64
            });

            // Status + connect button
            Box* connRow = new Box(body, { &Theme::RowH });
            connRow->style->margin.top = 8_px;

            connStatus = new Text(connRow, "DISCONNECTED");
            connStatus->style->text.color = rgba(255, 59, 48, 1);
            connStatus->style->text.size  = 11_px;
            connStatus->style->size       = { Grow() };

            Box* connBtn = makeBtn(connRow, "CONNECT", nullptr, true);
            connBtn->style->size.width = 100_px;
            connBtn->style->size.max.width = 100_px;
            connectBtnTxt = (Text*)connBtn->children[0];

            connBtn->onMouseDown([this](Rev::Element::Event& e) {
                toggleConnect();
            });

            // Show/hide rows for current platform
            updatePlatformRows();

            // Platform dropdown change
            platformDrop->dropdown->onMouseDown([this](Rev::Element::Event& e) {
                // Handled after selection settles — poll in computeStyle
            });
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

            // Preview placeholder
            Box* prev = new Box(rp, { &Theme::PreviewArea });
            Text* prevHint = new Text(prev, "[ NO ARTWORK LOADED ]");
            prevHint->style->text.color = rgba(232, 232, 232, 0.2f);
            prevHint->style->text.size  = 14_px;
            (void)prevHint;

            // Status panel
            Box* sp = new Box(rp, { &Theme::StatusPanel });

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

            // Log row (runner + gcode side by side)
            Box* logRow = new Box(sp);
            logRow->style->layout = { Axis::Horizontal, Align::Start, Align::Start };
            logRow->style->size.width = 100_pct;

            // Runner log
            Box* runnerCol = new Box(logRow);
            runnerCol->style->layout = { Axis::Vertical, Align::Start, Align::Start };
            runnerCol->style->size   = { Grow() };
            runnerCol->style->margin.right = 8_px;

            Text* runnerLbl = new Text(runnerCol, "RUNNER LOG");
            runnerLbl->style->text.color  = rgba(232, 232, 232, 0.4f);
            runnerLbl->style->text.size   = 9_px;
            runnerLbl->style->margin.bottom = 4_px;

            Box* runnerBox = new Box(runnerCol, { &Theme::LogBox });
            runnerLogTxt = new Text(runnerBox, "", { &Theme::LogText });

            // G-code log
            Box* gcodeCol = new Box(logRow);
            gcodeCol->style->layout = { Axis::Vertical, Align::Start, Align::Start };
            gcodeCol->style->size   = { Grow() };

            Text* gcodeLbl = new Text(gcodeCol, "GCODE STREAM");
            gcodeLbl->style->text.color  = rgba(232, 232, 232, 0.4f);
            gcodeLbl->style->text.size   = 9_px;
            gcodeLbl->style->margin.bottom = 4_px;

            Box* gcodeBox = new Box(gcodeCol, { &Theme::LogBox });
            gcodeLogTxt = new Text(gcodeBox, "", { &Theme::GcodeText });
        }

        // ─────────────────────────────────────────────────────────────────────
        // Rev compute overrides
        // ─────────────────────────────────────────────────────────────────────

        void computeStyle(Rev::Element::Event& e) override {

            // Drain log queue → update displayed text
            std::string msg;
            while (logQ.pop(msg)) {
                logLines.push_back(msg);
                if (logLines.size() > MAX_LOG) logLines.pop_front();
            }
            if (runnerLogTxt) {
                std::string joined;
                for (auto& l : logLines) { joined += l; joined += '\n'; }
                runnerLogTxt->content = joined;
            }

            while (gcodeQ.pop(msg)) {
                gcodeLines.push_back(msg);
                if (gcodeLines.size() > MAX_LOG) gcodeLines.pop_front();
            }
            if (gcodeLogTxt) {
                std::string joined;
                for (auto& l : gcodeLines) { joined += l; joined += '\n'; }
                gcodeLogTxt->content = joined;
            }

            // Progress bar
            int n = frameN.load(), tot = frameTotal.load();
            if (tot > 0 && progressFill) {
                float pct = std::clamp(100.0f * n / tot, 0.0f, 100.0f);
                progressFill->style->size.width = Pct(pct);
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
            if (!stmPortRow || !piHostRow) return;
            stmPortRow->style->visibility = (platform == Platform::STM32) ? Visibility::Visible : Visibility::Hidden;
            piHostRow->style->visibility  = (platform == Platform::Pi)    ? Visibility::Visible : Visibility::Hidden;
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
                std::string port = portInput ? portInput->text->strContent : settings.port;
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
                std::string dir = jobsDirInput ? jobsDirInput->text->strContent : settings.jobsDir;
                if (dir.empty()) dir = settings.jobsDir;
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
            // Enumerate COM ports via Win32 registry
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
                logQ.push("[SCAN] No COM ports found");
                return;
            }
            std::string ports;
            DWORD idx = 0;
            char  name[256], val[256];
            DWORD nameLen, valLen, type;
            while (true) {
                nameLen = valLen = 256;
                if (RegEnumValueA(hKey, idx++, name, &nameLen,
                                  nullptr, &type, (BYTE*)val, &valLen) != ERROR_SUCCESS) break;
                if (!ports.empty()) ports += ", ";
                ports += val;
            }
            RegCloseKey(hKey);
            logQ.push("[SCAN] Ports: " + (ports.empty() ? "(none)" : ports));
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
                if (fileLabel) {
                    fileLabel->content = std::filesystem::path(path).filename().string();
                    fileLabel->style->text.color = rgba(232, 232, 232, 1);
                }
                // Auto-fill job name from filename stem
                if (jobNameInput) {
                    std::string stem = std::filesystem::path(path).stem().string();
                    std::replace(stem.begin(), stem.end(), ' ', '_');
                    jobNameInput->text->content = stem;
                }
                logQ.push("Loaded: " + std::filesystem::path(path).filename().string());
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
            std::string cmd = "python pc/slicer.py \"" + inFile + "\""
                " --expose-ms " + exposeMs +
                " --feed-rate " + feedRate +
                " --threshold " + threshold +
                " --proj-w "    + projW +
                " --proj-h "    + projH +
                " --overlap "   + overlap +
                " --output-dir \"" + outDir + "\"";
            if (!jobName.empty())   cmd += " --job-name " + jobName;
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

            PROCESS_INFORMATION pi{};
            BOOL ok = CreateProcessA(nullptr, (LPSTR)cmd.c_str(),
                                     nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            CloseHandle(hW);

            if (!ok) {
                CloseHandle(hR);
                logQ.push("[ERR] Failed to start slicer — is Python in PATH?");
                if (sliceBtnTxt) sliceBtnTxt->content = "SLICE";
                return;
            }

            char buf[256];
            DWORD rd;
            while (ReadFile(hR, buf, sizeof(buf) - 1, &rd, nullptr) && rd > 0) {
                buf[rd] = '\0';
                // Strip newlines and push each line
                std::string chunk(buf, rd);
                std::istringstream ss(chunk);
                std::string line;
                while (std::getline(ss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) logQ.push(line);
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
            ws("port",         portInput  ? portInput->text->strContent  : settings.port);
            ws("piHost",       hostInput  ? hostInput->text->strContent  : settings.piHost);
            ws("jobsDir",      jobsDirInput ? jobsDirInput->text->strContent : settings.jobsDir);
            ws("platform",     platform == Platform::Pi ? "pi" : "stm32");
        }

        // ─────────────────────────────────────────────────────────────────────
        // Helpers
        // ─────────────────────────────────────────────────────────────────────

        // Create a labelled TextInput
        TextInput* makeInput(Box* parent, const std::string& lbl, const std::string& val, size_t maxLen) {
            auto* inp = new TextInput(parent, { .label = lbl, .placeholder = val, .maxLength = maxLen });
            if (!val.empty()) inp->text->content = val;
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

        static std::string fmtFloat(float v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.4g", (double)v);
            return buf;
        }

        static float parseFloat(const std::string& s, float def) {
            try { return std::stof(s); } catch (...) { return def; }
        }
    };

} // namespace LithoControl
