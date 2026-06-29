module;
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <functional>
#include <memory>

module LithoControl.Interface;   // implementation unit — no 'export'

namespace LithoControl {

    void Interface::buildSidebar() {

        sidebarBox = new Box(this, { &Theme::SidebarRoot });
        Box* sb = sidebarBox;

        // Absolutely-positioned content column: full width, grows to fit all sections.
        // Shifted up by sidebarScrollY each frame.
        sidebarContent = new Box(sb);
        sidebarContent->style->layout.direction  = Axis::Vertical;
        sidebarContent->style->layout.horizontal = Align::Start;
        sidebarContent->style->layout.vertical   = Align::Start;
        sidebarContent->style->layout.position   = Position::Absolute;
        sidebarContent->style->layout.wrap       = Wrap::False;
        sidebarContent->style->size.width        = 100_pct;

        // Title
        Text* title = new Text(sidebarContent, "LITHOREV");
        title->style->size.width     = 100_pct;
        title->style->padding        = { 10_px, 8_px, 10_px, 10_px };
        title->style->text.color     = rgba(232, 232, 232, 1);
        title->style->text.size      = 12_px;
        title->style->border.bottom.color = rgba(42, 42, 42, 1);
        title->style->border.bottom.width = 1_px;

        Box* connBody = nullptr;
        makeSection(sidebarContent, "CONNECTION", connBody);
        buildConnectionPanel(connBody);

        Box* dispBody = nullptr;
        makeSection(sidebarContent, "DISPLAY OUTPUT", dispBody);
        buildDisplayPanel(dispBody);

        Box* slicerBody = nullptr;
        makeSection(sidebarContent, "SLICER", slicerBody);
        buildSlicerPanel(slicerBody);

        Box* jobBody = nullptr;
        makeSection(sidebarContent, "JOB QUEUE", jobBody);
        buildJobPanel(jobBody);

        Box* jogBody = nullptr;
        makeSection(sidebarContent, "JOG", jogBody);
        buildJogPanel(jogBody);

        Box* cameraBody = nullptr;
        makeSection(sidebarContent, "CAMERA", cameraBody);
        buildCameraPanel(cameraBody);

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

        // Wheel handler: route to job list when cursor is over it, otherwise scroll sidebar.
        sb->onMouseWheel([this](Rev::Element::Event& e) {
            if (jobListBox && jobListInner && jobListBox->rect.contains(e.mouse.pos)) {
                jobListScrollY -= (e.mouse.wheel.y / 120.0f) * 30.0f;
                if (jobListScrollY < 0.0f) jobListScrollY = 0.0f;
                float maxScroll = measureSpread(jobListInner) - jobListBox->rect.h;
                if (maxScroll < 0.0f) maxScroll = 0.0f;
                if (jobListScrollY > maxScroll) jobListScrollY = maxScroll;
                jobListInner->style->position.top = Px(-jobListScrollY);
                e.propagate = false;
                return;
            }
            sidebarScrollY -= (e.mouse.wheel.y / 120.0f) * 40.0f;
            if (sidebarScrollY < 0.0f) sidebarScrollY = 0.0f;
            sidebarContent->style->position.top = Px(-sidebarScrollY);
            e.propagate = false;
        });
    }

    // Collapsible section header + body.
    // Returns a toggle() callable — call it after populating the body to start
    // the section collapsed; the header click calls the same function to expand.
    std::function<void()> Interface::makeSection(Box* parent, const std::string& title, Box*& body) {

        Box* hdr = new Box(parent, { &Theme::SectionHdr, &Theme::SectionHdrHover });
        hdr->style->layout = { Axis::Horizontal, Align::Start, Align::Center };

        Text* arrow = new Text(hdr, "v");
        arrow->style->text.color = rgba(232, 232, 232, 0.4f);
        arrow->style->text.size  = 10_px;
        arrow->style->margin.right = 6_px;

        Text* lbl = new Text(hdr, title);
        lbl->style->text.color = rgba(232, 232, 232, 0.4f);
        lbl->style->text.size  = 10_px;

        body = new Box(parent, { &Theme::SectionBody });

        // Saved children used as collapse-state indicator (empty = expanded)
        auto saved = std::make_shared<std::vector<Element*>>();

        auto toggle = [arrow, body, saved]() {
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
        };

        hdr->onMouseDown([toggle](Rev::Element::Event&) { toggle(); });

        return toggle;
    }

    // -- Sidebar collapse handle -------------------------------------------
    // A 16px-wide strip between sidebar and right panel -- always visible,
    // lets the user hide/show the sidebar without losing access to the toggle.

    void Interface::buildCollapseHandle() {
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

    // -- Connection panel -------------------------------------------------

    void Interface::buildConnectionPanel(Box* body) {

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

        // STM32 port row -- vertical so dropdown + scan button stack cleanly
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

        // Connect button -- no fixed px width, flex with the row
        Box* connBtn = makeBtn(connRow, "CONNECT", nullptr, true);
        connectBtnTxt = (Text*)connBtn->children[0];
        connBtn->onMouseDown([this](Rev::Element::Event& e) { toggleConnect(); });

        // Activate the correct row for the initial platform
        updatePlatformRows();
    }

    // -- Display output panel ----------------------------------------------

    void Interface::buildDisplayPanel(Box* body) {

        hdmiPassthroughChk = new Checkbox(body, { .label = "HDMI Passthrough", .def = false });
        hdmiPassthroughChk->label->style->text.color = rgba(232, 232, 232, 0.7f);
        hdmiPassthroughChk->label->style->text.size  = 11_px;

        // Channel selector: choose which colour channels reach the projector.
        // BGRA byte order: R=0x000000FF, G=0x0000FF00, B=0x00FF0000.
        {
            Text* chanHdr = new Text(body, "OUTPUT CHANNELS");
            chanHdr->style->text.color    = rgba(232, 232, 232, 0.4f);
            chanHdr->style->text.size     = 9_px;
            chanHdr->style->margin.top    = 8_px;
            chanHdr->style->margin.bottom = 4_px;

            Box* chanRow = new Box(body, { &Theme::RowH });
            chanRow->style->layout = { Axis::Horizontal, Align::Start, Align::Center };

            hdmiRedChk = new Checkbox(chanRow, { .label = "RED", .def = false });
            hdmiRedChk->label->style->text.color = rgba(255, 80, 80, 1);
            hdmiRedChk->label->style->text.size  = 10_px;
            hdmiRedChk->style->margin.right      = 8_px;

            hdmiGreenChk = new Checkbox(chanRow, { .label = "GREEN", .def = false });
            hdmiGreenChk->label->style->text.color = rgba(80, 220, 80, 1);
            hdmiGreenChk->label->style->text.size  = 10_px;
            hdmiGreenChk->style->margin.right      = 8_px;

            hdmiBlueChk = new Checkbox(chanRow, { .label = "BLUE", .def = false });
            hdmiBlueChk->label->style->text.color = rgba(80, 140, 255, 1);
            hdmiBlueChk->label->style->text.size  = 10_px;
            hdmiBlueChk->style->margin.right      = 8_px;

            // EVM CORRECT: swaps R↔B in the output pixel so the channel labels
            // match what the EVM physically outputs (its R and B data lines are
            // wired in reverse — empirically verified via color-cycle test).
            hdmiEvmCorrectChk = new Checkbox(chanRow, { .label = "EVM", .def = false });
            hdmiEvmCorrectChk->label->style->text.color = rgba(232, 232, 232, 0.6f);
            hdmiEvmCorrectChk->label->style->text.size  = 10_px;
        }

        // Slot: display selector row is added/removed here so it takes no layout space
        // when hidden. Using addChild/removeChild (not Visibility::Hidden) avoids the
        // hit-area offset that occurs when a zero-height hidden element stays in the
        // layout row.members list.
        hdmiDisplaySlot = new Box(body);
        hdmiDisplaySlot->style->layout.direction  = Axis::Vertical;
        hdmiDisplaySlot->style->layout.horizontal = Align::Start;
        hdmiDisplaySlot->style->size.width        = 100_pct;

        // Display selector row -- built as child of slot so shared/canvas is valid,
        // then immediately removed (checkbox starts unchecked).
        hdmiDisplayRow = new Box(hdmiDisplaySlot);
        hdmiDisplayRow->style->layout    = { Axis::Vertical, Align::Start, Align::Start };
        hdmiDisplayRow->style->size.width = 100_pct;
        hdmiDisplayRow->style->margin.top = 6_px;
        hdmiDisplaySlot->removeChild(hdmiDisplayRow);

        Text* dispLbl = new Text(hdmiDisplayRow, "DISPLAY");
        dispLbl->style->text.color    = rgba(232, 232, 232, 0.4f);
        dispLbl->style->text.size     = 9_px;
        dispLbl->style->margin.bottom = 4_px;

        hdmiDisplayDrop = new Dropdown(hdmiDisplayRow, {
            .options = {}, .placeholder = "Scan for displays...", .value = ""
        });
        hdmiDisplayDrop->label->style->visibility = Visibility::Hidden;

        Box* btnRow = new Box(hdmiDisplayRow, { &Theme::RowH });
        btnRow->style->margin.top = 4_px;
        makeBtn(btnRow, "SCAN",     [this]() { scanDisplays(); });
        makeBtn(btnRow, "640x360",  [this]() { setDisplayResolution(); });
        makeBtn(btnRow, "OPEN",     [this]() { openHdmiWindow(); });
        makeBtn(btnRow, "CLOSE",    [this]() { closeHdmiWindow(); });

        Box* testRow = new Box(hdmiDisplayRow, { &Theme::RowH });
        testRow->style->margin.top = 4_px;
        makeBtn(testRow, "COLOR TEST", [this]() { colorTest(); });
        hdmiTestBtn = makeBtn(testRow, "RGB TEST", [this]() { toggleTestImage(); });
    }

    // -- Camera panel ------------------------------------------------------

    void Interface::buildCameraPanel(Box* body) {
        Text* devLbl = new Text(body, "DEVICE");
        devLbl->style->text.color    = rgba(232, 232, 232, 0.4f);
        devLbl->style->text.size     = 9_px;
        devLbl->style->margin.bottom = 4_px;

        cameraDrop = new Dropdown(body, {
            .options = {}, .placeholder = "Scan for cameras...", .value = ""
        });
        cameraDrop->label->style->visibility = Visibility::Hidden;

        Box* btnRow = new Box(body, { &Theme::RowH });
        btnRow->style->margin.top = 4_px;
        makeBtn(btnRow, "SCAN", [this]() { scanCameras(); });

        Box* startBtn = makeBtn(btnRow, "START", nullptr, true);
        cameraBtnTxt = (Text*)startBtn->children[0];
        startBtn->onMouseDown([this](Rev::Element::Event&) { toggleCamera(); });
    }

    // -- Slicer panel ------------------------------------------------------

    void Interface::buildSlicerPanel(Box* body) {

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

        // Off by default -- slicing never touches the Pi unless this is ticked.
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
    void Interface::updateSendChkVisibility() {
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

    // -- Job panel ---------------------------------------------------------

    void Interface::buildJobPanel(Box* body) {

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

        // Search / filter input above the job list
        jobSearchInput = new TextInput(body, {
            .label = "SEARCH",
            .placeholder = "filter...",
            .maxLength = 64
        });
        jobSearchInput->label->style->text.color = rgba(232, 232, 232, 0.6f);
        jobSearchInput->label->style->text.size  = 9_px;
        tightenInput(jobSearchInput);

        // Job list container: fixed height + clip, with an inner
        // absolute-positioned content box that shifts for scroll.
        jobListBox = new Box(body);
        jobListBox->style->layout         = { Axis::Vertical, Align::Start, Align::Start };
        jobListBox->style->size.width      = 100_pct;
        jobListBox->style->size.height     = Px(160);
        jobListBox->style->size.min.height = Px(160);
        jobListBox->style->overflow        = Overflow::Hide;
        jobListBox->style->background.color = rgba(28, 28, 28, 1);
        jobListBox->style->border.color     = rgba(42, 42, 42, 1);
        jobListBox->style->border.width     = 1_px;
        jobListBox->style->border.radius    = 3_px;
        jobListBox->style->margin.bottom    = 6_px;
        jobListBox->style->padding          = { 2_px, 2_px, 2_px, 2_px };

        // Absolute-positioned inner column that scrolls vertically.
        jobListInner = new Box(jobListBox);
        jobListInner->style->layout          = { Axis::Vertical, Align::Start, Align::Start };
        jobListInner->style->layout.position = Position::Absolute;
        jobListInner->style->layout.wrap     = Wrap::False;
        jobListInner->style->size.width      = 100_pct;

        jobListContent = jobListInner;

        // Scrollbar track pinned to the right edge of the job list box.
        // Position is updated each frame in computeStyle as the box width may change.
        jobListScrollTrack = new Box(jobListBox);
        jobListScrollTrack->style->layout.position = Position::Absolute;
        jobListScrollTrack->style->position.top    = Px(2);
        jobListScrollTrack->style->position.left   = Px(0); // updated in computeStyle
        jobListScrollTrack->style->size.width      = 4_px;
        jobListScrollTrack->style->size.height     = Px(156);
        jobListScrollTrack->style->background.color = rgba(40, 40, 40, 1);
        jobListScrollTrack->style->border.radius   = 2_px;

        jobListScrollThumb = new Box(jobListScrollTrack);
        jobListScrollThumb->style->layout.position  = Position::Absolute;
        jobListScrollThumb->style->position.left    = Px(0);
        jobListScrollThumb->style->position.top     = Px(0);
        jobListScrollThumb->style->size.width       = 4_px;
        jobListScrollThumb->style->size.height      = Px(20);
        jobListScrollThumb->style->background.color = rgba(90, 90, 90, 1);
        jobListScrollThumb->style->border.radius    = 2_px;
        jobListScrollThumb->style->cursor           = Cursor::Hand;

        jobListScrollThumb->onMouseDown([this](Rev::Element::Event& e) {
            jobListThumbDragging       = true;
            jobListThumbDragStartY     = e.mouse.pos.y;
            jobListThumbDragStartScroll = jobListScrollY;
            e.propagate = false;
        });

        Box* btnRow = new Box(body, { &Theme::RowH });

        makeBtn(btnRow, "REFRESH", [this]() { refreshJobList(); }, false);
        makeBtn(btnRow, "PREVIEW", [this]() { previewJob(); }, false);
        makeBtn(btnRow, "START",   [this]() { startJob(); }, false, false, true);
        makeBtn(btnRow, "PAUSE",   [this]() { pauseJob(); }, false);
        makeBtn(btnRow, "ABORT",   [this]() { abortJob(); }, false, true);
    }

    // -- Jog panel ---------------------------------------------------------

    void Interface::buildJogPanel(Box* body) {

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

        // Position readout + query button
        Box* posRow = new Box(body, { &Theme::RowH });
        posRow->style->margin.top = 4_px;

        auto makePosLbl = [this](Box* parent, const std::string& axis) -> Text* {
            Text* axisLbl = new Text(parent, axis + ":");
            axisLbl->style->text.color = rgba(140, 140, 140, 1);
            axisLbl->style->text.size  = 10_px;
            axisLbl->style->margin     = { 0_px, 4_px, 0_px, 6_px };
            Text* val = new Text(parent, "---");
            val->style->text.color = rgba(200, 200, 200, 1);
            val->style->text.size  = 10_px;
            val->style->size       = { 50_px };
            val->style->margin.right = 4_px;
            val->selectable = true;
            return val;
        };
        posXLabel = makePosLbl(posRow, "X");
        posYLabel = makePosLbl(posRow, "Y");
        posZLabel = makePosLbl(posRow, "Z");

        Box* qBtn = makeBtn(posRow, "?", [this]() { queryPosition(); }, false);
        qBtn->style->size.width     = 24_px;
        qBtn->style->size.max.width = 24_px;
        qBtn->style->margin         = { 2_px, 2_px, 0_px, 0_px };
    }

    // -- Right panel -------------------------------------------------------

    void Interface::buildRightPanel() {

        Box* rp = new Box(this, { &Theme::RightPanel });

        // Preview control bar -- frame cycling + merge for sliced jobs
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

        // Viewport controls
        makeSmallBtn(prevBar, "FIT", [this]() {
            if (!previewImg) return;
            previewImg->zoom = 0.0f;
            previewImg->offsetX = previewImg->offsetY = 0.0f;
        });
        makeSmallBtn(prevBar, "1:1", [this]() {
            if (!previewImg) return;
            previewImg->zoom = 1.0f;
            previewImg->offsetX = previewImg->offsetY = 0.0f;
        });

        // Zoom drag slider
        Text* zoomLbl = new Text(prevBar, "ZOOM");
        zoomLbl->style->text.color   = rgba(232, 232, 232, 0.4f);
        zoomLbl->style->text.size    = 9_px;
        zoomLbl->style->margin.left  = 6_px;
        zoomLbl->style->margin.right = 4_px;

        zoomSliderTrack = new Box(prevBar);
        zoomSliderTrack->style->size             = { 90_px, 10_px };
        zoomSliderTrack->style->size.max.width   = 90_px;
        zoomSliderTrack->style->background.color = rgba(28, 28, 28, 1);
        zoomSliderTrack->style->border.color     = rgba(60, 60, 60, 1);
        zoomSliderTrack->style->border.radius    = 5_px;
        zoomSliderTrack->style->border.width     = 1_px;
        zoomSliderTrack->style->overflow         = Overflow::Hide;
        zoomSliderTrack->style->cursor           = Cursor::ArrowsHorizontal;

        zoomSliderFill = new Box(zoomSliderTrack);
        zoomSliderFill->style->layout.position  = Position::Absolute;
        zoomSliderFill->style->position.left    = Px(0);
        zoomSliderFill->style->position.top     = Px(0);
        zoomSliderFill->style->size.height      = 100_pct;
        zoomSliderFill->style->size.width       = Pct(0);
        zoomSliderFill->style->background.color = rgba(0, 160, 120, 1);
        zoomSliderFill->style->border.radius    = 5_px;

        zoomSliderTrack->onMouseDown([this](Rev::Element::Event& e) {
            zoomSliderDragging = true;
            if (previewImg && zoomSliderTrack->rect.w > 0.0f) {
                float t = std::clamp((e.mouse.pos.x - zoomSliderTrack->rect.x)
                                     / zoomSliderTrack->rect.w, 0.0f, 1.0f);
                previewImg->zoom = 0.1f * std::pow(200.0f, t);
            }
            e.propagate = false;
        });

        zoomLabel = new Text(prevBar, "1.0x");
        zoomLabel->style->text.color    = rgba(232, 232, 232, 0.6f);
        zoomLabel->style->text.size     = 9_px;
        zoomLabel->style->margin.left   = 5_px;
        zoomLabel->style->size.min.width = 34_px;

        // Spacer between viewport controls and frame label
        Box* prevBarSpacer = new Box(prevBar);
        prevBarSpacer->style->size.width = Grow();

        previewLabel = new Text(prevBar, "");
        previewLabel->style->text.color  = rgba(232, 232, 232, 0.6f);
        previewLabel->style->text.size   = 10_px;
        previewLabel->style->margin.right = 10_px;

        // Overlay opacity slider (right side of bar)
        Text* ovlLbl = new Text(prevBar, "OVL");
        ovlLbl->style->text.color    = rgba(232, 232, 232, 0.4f);
        ovlLbl->style->text.size     = 9_px;
        ovlLbl->style->margin.left   = 8_px;
        ovlLbl->style->margin.right  = 4_px;

        overlaySliderTrack = new Box(prevBar);
        overlaySliderTrack->style->size             = { 100_px, 10_px };
        overlaySliderTrack->style->size.max.width   = 100_px;
        overlaySliderTrack->style->background.color = rgba(28, 28, 28, 1);
        overlaySliderTrack->style->border.color     = rgba(60, 60, 60, 1);
        overlaySliderTrack->style->border.radius    = 5_px;
        overlaySliderTrack->style->border.width     = 1_px;
        overlaySliderTrack->style->overflow         = Overflow::Hide;
        overlaySliderTrack->style->cursor           = Cursor::ArrowsHorizontal;

        overlaySliderFill = new Box(overlaySliderTrack);
        overlaySliderFill->style->layout.position   = Position::Absolute;
        overlaySliderFill->style->position.left     = Px(0);
        overlaySliderFill->style->position.top      = Px(0);
        overlaySliderFill->style->size.height       = 100_pct;
        overlaySliderFill->style->size.width        = Pct(0);
        overlaySliderFill->style->background.color  = rgba(0, 87, 255, 1);
        overlaySliderFill->style->border.radius     = 5_px;

        overlaySliderTrack->onMouseDown([this](Rev::Element::Event& e) {
            overlaySliderDragging = true;
            float t = overlaySliderTrack->rect.x;
            float w = overlaySliderTrack->rect.w;
            if (w > 0.0f && previewImg)
                previewImg->overlayOpacity = std::clamp((e.mouse.pos.x - t) / w, 0.0f, 1.0f);
            e.propagate = false;
        });

        overlayPctLabel = new Text(prevBar, "0%");
        overlayPctLabel->style->text.color   = rgba(232, 232, 232, 0.6f);
        overlayPctLabel->style->text.size    = 9_px;
        overlayPctLabel->style->margin.left  = 5_px;
        overlayPctLabel->style->margin.right = 4_px;
        overlayPctLabel->style->size.min.width = 28_px;

        // Preview area -- ImagePreview fills remaining height above the status panel
        previewImg = new ImagePreview(rp, { &Theme::PreviewArea });

        // Mirror artwork to the HDMI projector whenever a new frame is staged.
        // Camera frames go through stagePixelsLive() and do NOT trigger this.
        previewImg->onArtworkBaked = [this](const std::vector<uint8_t>& px, int w, int h) {
            hdmiArtworkRGBA = px;
            hdmiArtworkW    = w;
            hdmiArtworkH    = h;
            pushArtworkToHdmi();
        };

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

        // Status panel -- explicit height so it can be resized by drag
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

        // Log row (runner + gcode side by side) -- grows to fill status panel
        Box* logRow = new Box(sp);
        logRow->style->layout        = { Axis::Horizontal, Align::Start, Align::Start };
        logRow->style->size.width    = 100_pct;
        logRow->style->size.height   = Grow();

        // -- Runner log ----------------------------------------------------

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
        runnerLogTxt->style->position.left   = 6_px;
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

        // -- G-code log ----------------------------------------------------

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
        gcodeLogTxt->style->position.left   = 6_px;
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

    // Create a labelled TextInput with dark-theme label colour
    TextInput* Interface::makeInput(Box* parent, const std::string& lbl, const std::string& val, size_t maxLen) {
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
    void Interface::tightenInput(TextInput* inp) {
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
            inp->text->style->text.wrap = Wrap::False;
            // Grow() for width so the grow phase allocates container width.
            // min.width explicitly zeroed so the 100_pct min from TextInput::Styles::Text
            // does not survive the cascade and force the element wider than the container.
            inp->text->style->size = { Grow() };
            inp->text->style->size.min.width = 0_px;
        }
        if (inp->placeholder) {
            inp->placeholder->style->text.wrap = Wrap::False;
        }
        inp->style->margin = { 3_px, 3_px, 2_px, 2_px };
    }

    // Create a button Box
    Box* Interface::makeBtn(Box* parent, const std::string& label,
                 std::function<void()> cb,
                 bool accent, bool danger,
                 bool success, bool warning)
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
    Box* Interface::makeJogBtn(Box* parent, const std::string& label, std::function<void()> cb) {
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

} // namespace LithoControl
