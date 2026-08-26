module;
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <functional>
#include <memory>
#include <filesystem>
#include <cmath>
#include <cstdint>

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
        // Dropdown popups (camera/HDMI display/etc) are absolutely-positioned
        // children that render BELOW their own box, but they're still clipped
        // by this container's overflow:hidden scroll boundary (see sb's
        // Overflow::Hide, needed for the custom scrollbar) regardless of
        // their own z-index -- there's no "escape the clip" mechanism here.
        // A dropdown sitting near the bottom of a page's content therefore
        // has its option list cut off with nothing below it to scroll into.
        // Generous bottom padding on the whole scrollable column gives every
        // dropdown, wherever it ends up, room below it to fully open into.
        sidebarContent->style->padding.bottom    = 260_px;

        // Title
        Text* title = new Text(sidebarContent, "LITHOREV");
        title->style->size.width     = 100_pct;
        title->style->padding        = { 10_px, 8_px, 10_px, 10_px };
        title->style->text.color     = rgba(232, 232, 232, 1);
        title->style->text.size      = 12_px;
        title->style->border.bottom.color = rgba(42, 42, 42, 1);
        title->style->border.bottom.width = 1_px;

        // Page tabs -- switches which section groups are attached to
        // sidebarContent (see updatePageVisibility()) instead of showing
        // every section stacked at once.
        Box* pageTabRow = new Box(sidebarContent, { &Theme::RowH });
        pageTabRow->style->layout        = { Axis::Horizontal, Align::Start, Align::Center };
        pageTabRow->style->padding       = { 6_px, 6_px, 8_px, 8_px };
        pageTabRow->style->margin.bottom = 0_px;
        pageTabRow->style->border.bottom = { rgba(42, 42, 42, 1), 1_px };

        auto makePageTab = [this](Box* parent, const std::string& label, Page page) {
            Box* b = new Box(parent, { &Theme::Btn, &Theme::BtnHover });
            b->style->size   = { Grow(), 24_px };
            b->style->margin = { 2_px, 2_px, 0_px, 0_px };
            Text* t = new Text(b, label);
            t->style->text.size  = 9_px;
            t->style->text.color = rgba(232, 232, 232, 1);
            b->onMouseDown([this, page](Rev::Element::Event&) {
                currentPage = page;
                updatePageVisibility();
            });
            return b;
        };
        pageTabConnectionsBtn = makePageTab(pageTabRow, "CONNECTIONS", Page::Connections);
        pageTabCalibrationBtn = makePageTab(pageTabRow, "CALIBRATION", Page::Calibration);
        pageTabExecutionBtn   = makePageTab(pageTabRow, "EXECUTION", Page::Execution);

        Box* connBody = nullptr;
        Box* connSection = makeSection(sidebarContent, "CONNECTION", connBody);
        buildConnectionPanel(connBody);

        Box* dispBody = nullptr;
        Box* dispSection = makeSection(sidebarContent, "DISPLAY OUTPUT", dispBody);
        buildDisplayPanel(dispBody);

        Box* slicerBody = nullptr;
        Box* slicerSection = makeSection(sidebarContent, "SLICER", slicerBody);
        buildSlicerPanel(slicerBody);

        Box* jobBody = nullptr;
        Box* jobSection = makeSection(sidebarContent, "JOB QUEUE", jobBody);
        buildJobPanel(jobBody);

        Box* jogBody = nullptr;
        Box* jogSection = makeSection(sidebarContent, "JOG", jogBody);
        buildJogPanel(jogBody);

        Box* cameraDeviceBody = nullptr;
        Box* cameraDeviceSection = makeSection(sidebarContent, "CAMERA", cameraDeviceBody);
        buildCameraDevicePanel(cameraDeviceBody);

        Box* cameraSettingsBody = nullptr;
        Box* cameraSettingsSection = makeSection(sidebarContent, "CAMERA SETTINGS", cameraSettingsBody);
        buildCameraSettingsPanel(cameraSettingsBody);

        Box* calibBody = nullptr;
        Box* calibSection = makeSection(sidebarContent, "CALIBRATION ACTIONS", calibBody);
        buildCalibrationPanel(calibBody);

        // CONNECTIONS: link setup + projector output + camera device select.
        // CALIBRATION: camera preview/settings + calib-dt subprocess actions.
        // EXECUTION: artwork/job management, gantry jog + (in the right
        // panel) the runner/G-code logs.
        pageSections = {
            { Page::Connections, connSection },
            { Page::Connections, dispSection },
            { Page::Connections, cameraDeviceSection },
            { Page::Execution,   slicerSection },
            { Page::Execution,   jobSection },
            { Page::Calibration, cameraSettingsSection },
            { Page::Calibration, calibSection },
            { Page::Execution,   jogSection },
        };
        updatePageVisibility();

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

    // Collapsible section header + body, both wrapped in a single container
    // Box that's returned so callers (buildSidebar()) can group whole
    // sections into pages -- attaching/detaching the wrapper as a unit via
    // addChild/removeChild (see updatePageVisibility()) rather than having
    // to track each section's header and body separately.
    Box* Interface::makeSection(Box* parent, const std::string& title, Box*& body) {

        Box* wrapper = new Box(parent);
        wrapper->style->layout     = { Axis::Vertical, Align::Start, Align::Start, Wrap::False };
        wrapper->style->size.width = 100_pct;

        Box* hdr = new Box(wrapper, { &Theme::SectionHdr, &Theme::SectionHdrHover });
        hdr->style->layout = { Axis::Horizontal, Align::Start, Align::Center };

        Text* arrow = new Text(hdr, "v");
        arrow->style->text.color = rgba(232, 232, 232, 0.4f);
        arrow->style->text.size  = 10_px;
        arrow->style->margin.right = 6_px;

        Text* lbl = new Text(hdr, title);
        lbl->style->text.color = rgba(232, 232, 232, 0.4f);
        lbl->style->text.size  = 10_px;

        body = new Box(wrapper, { &Theme::SectionBody });

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

        return wrapper;
    }

    // Attaches/detaches whole sections (see makeSection()) to sidebarContent
    // based on currentPage, and shows the runner/G-code status panel only on
    // the Execution page. Called once at startup (after buildRightPanel(), so
    // rightPanelBox/statusPanelBox already exist) and again on every tab click.
    void Interface::updatePageVisibility() {
        for (auto& [page, sec] : pageSections) {
            if (!sec || !sidebarContent) continue;
            bool shouldShow = (page == currentPage);
            auto& kids = sidebarContent->children;
            bool isChild = std::find(kids.begin(), kids.end(), (Element*)sec) != kids.end();
            if (shouldShow && !isChild) sidebarContent->addChild(sec);
            else if (!shouldShow && isChild) sidebarContent->removeChild(sec);
        }

        // Switching pages changes the content height under the (unchanged)
        // scroll offset, so reset to the top rather than leaving the view
        // scrolled into empty space.
        sidebarScrollY = 0.0f;
        if (sidebarContent) sidebarContent->style->position.top = Px(0);

        auto setActive = [](Box* btn, bool active) {
            if (!btn) return;
            btn->style->background.color = active ? rgba(0, 87, 255, 1) : rgba(30, 30, 30, 1);
            btn->style->border.color     = active ? rgba(0, 87, 255, 1) : rgba(42, 42, 42, 1);
        };
        setActive(pageTabConnectionsBtn, currentPage == Page::Connections);
        setActive(pageTabCalibrationBtn, currentPage == Page::Calibration);
        setActive(pageTabExecutionBtn,   currentPage == Page::Execution);

        // The status panel is shown on Execution (full: progress + runner
        // log + G-code stream) and reused on Calibration as a plain terminal
        // for calib-dt's command output (which already flows into the same
        // runner log via runCalibDtSubprocess() -- see logQ/logLines), with
        // the job-progress row and G-code column hidden since neither
        // applies there. Hidden entirely on Connections.
        bool execPage  = (currentPage == Page::Execution);
        bool calibPage = (currentPage == Page::Calibration);
        bool showStatus = execPage || calibPage;

        if (rightPanelBox && statusDragHandle && statusPanelBox) {
            auto& kids = rightPanelBox->children;
            bool dragIn = std::find(kids.begin(), kids.end(), (Element*)statusDragHandle) != kids.end();
            bool boxIn  = std::find(kids.begin(), kids.end(), (Element*)statusPanelBox)  != kids.end();
            if (showStatus && !dragIn) rightPanelBox->addChild(statusDragHandle);
            if (showStatus && !boxIn)  rightPanelBox->addChild(statusPanelBox);
            if (!showStatus && dragIn) rightPanelBox->removeChild(statusDragHandle);
            if (!showStatus && boxIn)  rightPanelBox->removeChild(statusPanelBox);
        }

        if (statusProgRow && statusProgRow->parent) {
            Element* parent = statusProgRow->parent;
            auto& kids = parent->children;
            bool isIn = std::find(kids.begin(), kids.end(), (Element*)statusProgRow) != kids.end();
            if (execPage && !isIn) parent->addChild(statusProgRow);
            if (!execPage && isIn) parent->removeChild(statusProgRow);
        }
        if (gcodeColBox && gcodeColBox->parent) {
            Element* parent = gcodeColBox->parent;
            auto& kids = parent->children;
            bool isIn = std::find(kids.begin(), kids.end(), (Element*)gcodeColBox) != kids.end();
            if (execPage && !isIn) parent->addChild(gcodeColBox);
            if (!execPage && isIn) parent->removeChild(gcodeColBox);
        }
        if (runnerLogLbl) runnerLogLbl->content = execPage ? "RUNNER LOG" : "TERMINAL";
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
            .options = { { "STM32 / UV Tester (Serial)", "stm32" }, { "Pi Zero 2W (TCP)", "pi" } },
            .placeholder = "STM32 / UV Tester (Serial)",
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

            hdmiRedChk = new Checkbox(chanRow, { .label = "RED", .def = true });
            hdmiRedChk->label->style->text.color = rgba(255, 80, 80, 1);
            hdmiRedChk->label->style->text.size  = 10_px;
            hdmiRedChk->style->margin.right      = 8_px;

            hdmiGreenChk = new Checkbox(chanRow, { .label = "GREEN", .def = true });
            hdmiGreenChk->label->style->text.color = rgba(80, 220, 80, 1);
            hdmiGreenChk->label->style->text.size  = 10_px;
            hdmiGreenChk->style->margin.right      = 8_px;

            hdmiBlueChk = new Checkbox(chanRow, { .label = "BLUE", .def = true });
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

        // Flip correction: compensates for a mirrored/rotated optical path or
        // mount (e.g. a relay mirror, rear projection, ceiling mount).
        // Applied to everything sent to the projector window in
        // composeHdmiFrame() -- solid-color test, RGB test animation, and
        // actual job frames alike.
        {
            Text* flipHdr = new Text(body, "FLIP");
            flipHdr->style->text.color    = rgba(232, 232, 232, 0.4f);
            flipHdr->style->text.size     = 9_px;
            flipHdr->style->margin.top    = 8_px;
            flipHdr->style->margin.bottom = 4_px;

            Box* flipRow = new Box(body, { &Theme::RowH });
            flipRow->style->layout = { Axis::Horizontal, Align::Start, Align::Center };

            hdmiFlipHChk = new Checkbox(flipRow, { .label = "Flip H", .def = false });
            hdmiFlipHChk->label->style->text.color = rgba(232, 232, 232, 0.7f);
            hdmiFlipHChk->label->style->text.size  = 10_px;
            hdmiFlipHChk->style->margin.right      = 12_px;

            hdmiFlipVChk = new Checkbox(flipRow, { .label = "Flip V", .def = false });
            hdmiFlipVChk->label->style->text.color = rgba(232, 232, 232, 0.7f);
            hdmiFlipVChk->label->style->text.size  = 10_px;
        }

        // Projector EDID override -- runs pc/apply_edid.bat, which installs the
        // checked-in custom EDID (via a CRU-exported installer) and resets the
        // display driver so a new/other PC picks up the same mode as the
        // reference system. Always visible (not gated on Passthrough) since this
        // is a one-time per-machine fix, independent of streaming mode.
        {
            Box* edidRow = new Box(body, { &Theme::RowH });
            edidRow->style->margin.top = 8_px;
            edidBtn = makeBtn(edidRow, "APPLY EDID", [this]() { applyEdid(); });
            edidBtnTxt = (Text*)edidBtn->children[0];
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
        hdmiCustomImageBtn = makeBtn(testRow, "CUSTOM IMAGE", [this]() { loadCustomImage(); });

        // UV laser toggle
        Box* uvRow = new Box(body, { &Theme::RowH });
        uvRow->style->margin.top = 8_px;
        uvBtn = makeBtn(uvRow, "UV OFF", [this]() {
            if (!connFlag) { logQ.push("Not connected"); return; }
            if (platform == Platform::STM32 && stmSerial) {
                uvOn = !uvOn;
                std::lock_guard<std::mutex> lk(serialMtx);
                stmSerial->sendText(uvOn ? "UV_ON\n" : "UV_OFF\n");
                logQ.push(uvOn ? "-> UV_ON" : "-> UV_OFF");
                if (uvBtnTxt) uvBtnTxt->content = uvOn ? "UV ON" : "UV OFF";
                if (uvBtn) {
                    uvBtn->styles.remove(&Theme::Btn);
                    uvBtn->styles.remove(&Theme::BtnHover);
                    uvBtn->styles.remove(&Theme::BtnWarning);
                    if (uvOn) {
                        uvBtn->styles.add(&Theme::BtnWarning);
                    } else {
                        uvBtn->styles.add(&Theme::Btn);
                        uvBtn->styles.add(&Theme::BtnHover);
                    }
                }
            }
        });
        uvBtnTxt = (Text*)uvBtn->children[0];
    }

    // -- Camera panel ------------------------------------------------------

    // Device selection only (scan/select/start-stop) -- lives on the
    // Connections page, alongside the serial/socket link and projector
    // output, since it's the same kind of "what am I talking to" setup.
    // The actual preview/adjustment controls are buildCameraSettingsPanel(),
    // on the Calibration page.
    void Interface::buildCameraDevicePanel(Box* body) {
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

    // Live-preview adjustments + Save Frame -- lives on the Calibration page.
    void Interface::buildCameraSettingsPanel(Box* body) {
        makeBtn(body, "SAVE FRAME", [this]() { saveCameraFrame(); });
        // Point the known-diameter calibration circle target at the camera
        // and save one before RUN CALIBRATION -- calib-dt's --scale-reference
        // is a required argument (measures um-per-pixel), and RUN CALIBRATION
        // fails outright without at least one frame saved here.
        makeBtn(body, "SAVE SCALE REF", [this]() { saveScaleReferenceFrame(); })->style->margin.top = 4_px;

        // -- Adjustments (flip/rotate/brightness/contrast/FPS/resolution) --
        // Applied to every captured frame in software, identically across
        // backends -- see Interface::applyCameraAdjustments().

        auto sectionLbl = [](Box* parent, const std::string& text) {
            Text* t = new Text(parent, text);
            t->style->text.color    = rgba(232, 232, 232, 0.4f);
            t->style->text.size     = 9_px;
            t->style->margin.top    = 8_px;
            t->style->margin.bottom = 4_px;
            return t;
        };

        // Flip checkboxes -- Theme::RowH centers+spaces its children, which
        // reads as a huge gap for two small checkboxes; override to a
        // left-packed row (same fix already applied to the HDMI channel
        // checkbox row above) and give each an explicit light-colored label
        // (Checkbox's default label style is dark, meant for a light bg).
        Box* flipRow = new Box(body, { &Theme::RowH });
        flipRow->style->layout    = { Axis::Horizontal, Align::Start, Align::Center };
        flipRow->style->margin.top = 4_px;

        cameraFlipHChk = new Checkbox(flipRow, { .label = "Flip H", .def = false });
        cameraFlipHChk->label->style->text.color = rgba(232, 232, 232, 0.7f);
        cameraFlipHChk->label->style->text.size  = 10_px;
        cameraFlipHChk->style->margin.right      = 12_px;

        cameraFlipVChk = new Checkbox(flipRow, { .label = "Flip V", .def = false });
        cameraFlipVChk->label->style->text.color = rgba(232, 232, 232, 0.7f);
        cameraFlipVChk->label->style->text.size  = 10_px;

        // Rotation / resolution / target-FPS: compact toggle-button groups
        // instead of Dropdowns. A Dropdown's option list is a
        // position:absolute overlay that gets clipped by the sidebar's
        // Overflow::Hide (Theme::SidebarRoot) when it opens near the bottom
        // of the scrolled viewport, making the lower options unreachable --
        // button groups render in-flow so they can't be clipped that way.
        // A toggle group can span multiple rows (Box* parent varies per
        // call) while still highlighting exclusively across the whole set --
        // the button/value pairs accumulate in the shared `group` vector
        // across calls, so selecting one clears the highlight on every
        // other button that shares the same group, not just its own row.
        using ToggleGroup = std::vector<std::pair<Box*, std::string>>;
        auto addToggleRow = [this](Box* parent, std::shared_ptr<ToggleGroup> group, float btnWidth,
                                    std::vector<std::pair<std::string, std::string>> opts,
                                    std::function<void(const std::string&)> onSelect) {
            auto highlight = [group](const std::string& sel) {
                for (auto& [btn, v] : *group) {
                    bool on = (v == sel);
                    btn->style->background.color = on ? rgba(0, 87, 255, 1)  : rgba(30, 30, 30, 1);
                    btn->style->border.color     = on ? rgba(0, 87, 255, 1)  : rgba(42, 42, 42, 1);
                }
            };
            for (auto& [label, value] : opts) {
                Box* b = new Box(parent, { &Theme::Btn, &Theme::BtnHover });
                b->style->size      = { Px(btnWidth), 22_px };
                b->style->size.max  = { Px(btnWidth), 22_px };
                b->style->margin    = { 2_px, 2_px, 0_px, 0_px };
                Text* t = new Text(b, label);
                t->style->text.size  = 10_px;
                t->style->text.color = rgba(232, 232, 232, 1);
                group->push_back({ b, value });
                b->onMouseDown([value, onSelect, highlight](Rev::Element::Event&) {
                    onSelect(value);
                    highlight(value);
                });
            }
        };

        sectionLbl(body, "ROTATION");
        Box* rotRow = new Box(body, { &Theme::RowH });
        rotRow->style->layout = { Axis::Horizontal, Align::Start, Align::Center };
        auto rotGroup = std::make_shared<ToggleGroup>();
        addToggleRow(rotRow, rotGroup, 46.0f,
            { {"0", "0"}, {"90", "90"}, {"180", "180"}, {"270", "270"} },
            [this](const std::string& v) { cameraRotationDeg = std::stoi(v); });
        for (auto& [btn, v] : *rotGroup)
            if (v == "0") { btn->style->background.color = rgba(0, 87, 255, 1); btn->style->border.color = rgba(0, 87, 255, 1); }

        sectionLbl(body, "RESOLUTION");
        Box* resRow1 = new Box(body, { &Theme::RowH });
        resRow1->style->layout = { Axis::Horizontal, Align::Start, Align::Center };
        Box* resRow2 = new Box(body, { &Theme::RowH });
        resRow2->style->layout = { Axis::Horizontal, Align::Start, Align::Center };
        auto selectRes = [this](const std::string& v) {
            int rw = 0, rh = 0;
            std::sscanf(v.c_str(), "%dx%d", &rw, &rh);
            cameraResW = rw; cameraResH = rh; // 0x0 == native, no rescale
        };
        auto resGroup = std::make_shared<ToggleGroup>();
        addToggleRow(resRow1, resGroup, 76.0f,
            { {"Native", "0x0"}, {"1280x1024", "1280x1024"}, {"1024x768", "1024x768"} }, selectRes);
        addToggleRow(resRow2, resGroup, 76.0f,
            { {"800x600", "800x600"}, {"640x480", "640x480"}, {"320x240", "320x240"} }, selectRes);
        for (auto& [btn, v] : *resGroup)
            if (v == "0x0") { btn->style->background.color = rgba(0, 87, 255, 1); btn->style->border.color = rgba(0, 87, 255, 1); }

        sectionLbl(body, "TARGET FPS");
        Box* fpsRow1 = new Box(body, { &Theme::RowH });
        fpsRow1->style->layout = { Axis::Horizontal, Align::Start, Align::Center };
        Box* fpsRow2 = new Box(body, { &Theme::RowH });
        fpsRow2->style->layout = { Axis::Horizontal, Align::Start, Align::Center };
        auto selectFps = [this](const std::string& v) { cameraTargetFps = std::stoi(v); };
        auto fpsGroup = std::make_shared<ToggleGroup>();
        addToggleRow(fpsRow1, fpsGroup, 60.0f,
            { {"Unlimited", "0"}, {"5", "5"}, {"10", "10"} }, selectFps);
        addToggleRow(fpsRow2, fpsGroup, 60.0f,
            { {"15", "15"}, {"20", "20"}, {"30", "30"} }, selectFps);
        for (auto& [btn, v] : *fpsGroup)
            if (v == "0") { btn->style->background.color = rgba(0, 87, 255, 1); btn->style->border.color = rgba(0, 87, 255, 1); }

        // Section label with a small RESET button flush to the right --
        // used for brightness/contrast so each can be snapped back to its
        // default without dragging the slider by eye.
        auto sectionLblWithReset = [](Box* parent, const std::string& text, std::function<void()> onReset) {
            Box* row = new Box(parent, { &Theme::RowH });
            row->style->layout      = { Axis::Horizontal, Align::Start, Align::Center };
            row->style->margin.top    = 8_px;
            row->style->margin.bottom = 4_px;

            Text* t = new Text(row, text);
            t->style->text.color = rgba(232, 232, 232, 0.4f);
            t->style->text.size  = 9_px;
            t->style->size.width = Grow();

            Box* resetBtn = new Box(row, { &Theme::Btn, &Theme::BtnHover });
            resetBtn->style->size     = { 40_px, 16_px };
            resetBtn->style->size.max = { 40_px, 16_px };
            Text* resetTxt = new Text(resetBtn, "RESET");
            resetTxt->style->text.size  = 8_px;
            resetTxt->style->text.color = rgba(232, 232, 232, 0.8f);
            resetBtn->onMouseDown([onReset](Rev::Element::Event&) { onReset(); });
        };

        // Brightness / contrast: same custom track+fill drag-slider pattern
        // as the zoom/overlay sliders in buildPreviewBar() -- the framework
        // Slider element's default label/value text is dark-on-light and
        // came out unreadable on this dark sidebar, plus its label+value
        // Text children sit flush against each other with no separator.
        sectionLblWithReset(body, "BRIGHTNESS", [this]() { cameraBrightness = 0; });
        cameraBrightTrack = new Box(body);
        cameraBrightTrack->style->size             = { Grow(), 10_px };
        cameraBrightTrack->style->background.color = rgba(28, 28, 28, 1);
        cameraBrightTrack->style->border.color     = rgba(60, 60, 60, 1);
        cameraBrightTrack->style->border.radius    = 5_px;
        cameraBrightTrack->style->border.width     = 1_px;
        cameraBrightTrack->style->overflow         = Overflow::Hide;
        cameraBrightTrack->style->cursor           = Cursor::ArrowsHorizontal;

        cameraBrightFill = new Box(cameraBrightTrack);
        cameraBrightFill->style->layout.position  = Position::Absolute;
        cameraBrightFill->style->position.left    = Px(0);
        cameraBrightFill->style->position.top     = Px(0);
        cameraBrightFill->style->size.height      = 100_pct;
        cameraBrightFill->style->size.width       = Pct(50.0f);
        cameraBrightFill->style->background.color = rgba(0, 87, 255, 1);
        cameraBrightFill->style->border.radius    = 5_px;

        cameraBrightTrack->onMouseDown([this](Rev::Element::Event& e) {
            cameraBrightDragging = true;
            if (cameraBrightTrack->rect.w > 0.0f) {
                float t = std::clamp((e.mouse.pos.x - cameraBrightTrack->rect.x)
                                     / cameraBrightTrack->rect.w, 0.0f, 1.0f);
                cameraBrightness = (int)std::lround(-100.0f + t * 200.0f);
            }
            e.propagate = false;
        });

        cameraBrightLabel = new Text(body, "+0");
        cameraBrightLabel->style->text.color   = rgba(232, 232, 232, 0.6f);
        cameraBrightLabel->style->text.size    = 9_px;
        cameraBrightLabel->style->margin.top   = 3_px;

        sectionLblWithReset(body, "CONTRAST", [this]() { cameraContrastPct = 100; });
        cameraContrastTrack = new Box(body);
        cameraContrastTrack->style->size             = { Grow(), 10_px };
        cameraContrastTrack->style->background.color = rgba(28, 28, 28, 1);
        cameraContrastTrack->style->border.color     = rgba(60, 60, 60, 1);
        cameraContrastTrack->style->border.radius    = 5_px;
        cameraContrastTrack->style->border.width     = 1_px;
        cameraContrastTrack->style->overflow         = Overflow::Hide;
        cameraContrastTrack->style->cursor           = Cursor::ArrowsHorizontal;

        cameraContrastFill = new Box(cameraContrastTrack);
        cameraContrastFill->style->layout.position  = Position::Absolute;
        cameraContrastFill->style->position.left    = Px(0);
        cameraContrastFill->style->position.top     = Px(0);
        cameraContrastFill->style->size.height      = 100_pct;
        cameraContrastFill->style->size.width       = Pct(100.0f / 3.0f);
        cameraContrastFill->style->background.color = rgba(0, 160, 120, 1);
        cameraContrastFill->style->border.radius    = 5_px;

        cameraContrastTrack->onMouseDown([this](Rev::Element::Event& e) {
            cameraContrastDragging = true;
            if (cameraContrastTrack->rect.w > 0.0f) {
                float t = std::clamp((e.mouse.pos.x - cameraContrastTrack->rect.x)
                                     / cameraContrastTrack->rect.w, 0.0f, 1.0f);
                cameraContrastPct = (int)std::lround(t * 300.0f);
            }
            e.propagate = false;
        });

        cameraContrastLabel = new Text(body, "100%");
        cameraContrastLabel->style->text.color  = rgba(232, 232, 232, 0.6f);
        cameraContrastLabel->style->text.size   = 9_px;
        cameraContrastLabel->style->margin.top  = 3_px;

        // -- Extended UVC settings (exposure / gain-ISO) --------------------
        // Manual hardware controls for a generic UVC camera (e.g. an 8MP USB
        // camera) -- built here but kept detached from the tree by default
        // and only re-attached (see the sync block in computeStyle()) while
        // such a camera is running. The AmScope SDK path (MU130 etc.) drives
        // its own auto-exposure internally and has no equivalent control
        // surface, so this section never shows for it.
        cameraExtendedSlot = new Box(body);
        cameraExtendedSlot->style->layout     = { Axis::Vertical, Align::Start, Align::Start };
        cameraExtendedSlot->style->size.width = Grow();

        cameraExtendedBox = new Box(cameraExtendedSlot);
        cameraExtendedBox->style->layout     = { Axis::Vertical, Align::Start, Align::Start };
        cameraExtendedBox->style->size.width = Grow();

        sectionLbl(cameraExtendedBox, "EXTENDED SETTINGS (UVC)");

        Box* autoExpoRow = new Box(cameraExtendedBox, { &Theme::RowH });
        autoExpoRow->style->layout     = { Axis::Horizontal, Align::Start, Align::Center };
        autoExpoRow->style->margin.top = 4_px;
        cameraAutoExpoChk = new Checkbox(autoExpoRow, { .label = "Auto Exposure", .def = true });
        cameraAutoExpoChk->label->style->text.color = rgba(232, 232, 232, 0.7f);
        cameraAutoExpoChk->label->style->text.size  = 10_px;

        sectionLblWithReset(cameraExtendedBox, "EXPOSURE", [this]() {
            int mid = (cameraExposureMin.load() + cameraExposureMax.load()) / 2;
            cameraExposureVal = mid;
            setCameraExposure(mid);
        });
        cameraExposureTrack = new Box(cameraExtendedBox);
        cameraExposureTrack->style->size             = { Grow(), 10_px };
        cameraExposureTrack->style->background.color = rgba(28, 28, 28, 1);
        cameraExposureTrack->style->border.color     = rgba(60, 60, 60, 1);
        cameraExposureTrack->style->border.radius    = 5_px;
        cameraExposureTrack->style->border.width     = 1_px;
        cameraExposureTrack->style->overflow         = Overflow::Hide;
        cameraExposureTrack->style->cursor           = Cursor::ArrowsHorizontal;

        cameraExposureFill = new Box(cameraExposureTrack);
        cameraExposureFill->style->layout.position  = Position::Absolute;
        cameraExposureFill->style->position.left    = Px(0);
        cameraExposureFill->style->position.top     = Px(0);
        cameraExposureFill->style->size.height      = 100_pct;
        cameraExposureFill->style->size.width       = Pct(0.0f);
        cameraExposureFill->style->background.color = rgba(255, 149, 0, 1);
        cameraExposureFill->style->border.radius    = 5_px;

        cameraExposureTrack->onMouseDown([this](Rev::Element::Event& e) {
            cameraExposureDragging = true;
            int lo = cameraExposureMin.load(), hi = cameraExposureMax.load();
            if (hi > lo && cameraExposureTrack->rect.w > 0.0f) {
                float t = std::clamp((e.mouse.pos.x - cameraExposureTrack->rect.x)
                                     / cameraExposureTrack->rect.w, 0.0f, 1.0f);
                int v = lo + (int)std::lround(t * (hi - lo));
                cameraExposureVal = v;
                setCameraExposure(v);
            }
            e.propagate = false;
        });

        cameraExposureLabel = new Text(cameraExtendedBox, "-");
        cameraExposureLabel->style->text.color  = rgba(232, 232, 232, 0.6f);
        cameraExposureLabel->style->text.size   = 9_px;
        cameraExposureLabel->style->margin.top  = 3_px;

        sectionLblWithReset(cameraExtendedBox, "GAIN (ISO)", [this]() {
            int lo = cameraGainMin.load();
            cameraGainVal = lo;
            setCameraGain(lo);
        });
        cameraGainTrack = new Box(cameraExtendedBox);
        cameraGainTrack->style->size             = { Grow(), 10_px };
        cameraGainTrack->style->background.color = rgba(28, 28, 28, 1);
        cameraGainTrack->style->border.color     = rgba(60, 60, 60, 1);
        cameraGainTrack->style->border.radius    = 5_px;
        cameraGainTrack->style->border.width     = 1_px;
        cameraGainTrack->style->overflow         = Overflow::Hide;
        cameraGainTrack->style->cursor           = Cursor::ArrowsHorizontal;

        cameraGainFill = new Box(cameraGainTrack);
        cameraGainFill->style->layout.position  = Position::Absolute;
        cameraGainFill->style->position.left    = Px(0);
        cameraGainFill->style->position.top     = Px(0);
        cameraGainFill->style->size.height      = 100_pct;
        cameraGainFill->style->size.width       = Pct(0.0f);
        cameraGainFill->style->background.color = rgba(191, 90, 242, 1);
        cameraGainFill->style->border.radius    = 5_px;

        cameraGainTrack->onMouseDown([this](Rev::Element::Event& e) {
            cameraGainDragging = true;
            int lo = cameraGainMin.load(), hi = cameraGainMax.load();
            if (hi > lo && cameraGainTrack->rect.w > 0.0f) {
                float t = std::clamp((e.mouse.pos.x - cameraGainTrack->rect.x)
                                     / cameraGainTrack->rect.w, 0.0f, 1.0f);
                int v = lo + (int)std::lround(t * (hi - lo));
                cameraGainVal = v;
                setCameraGain(v);
            }
            e.propagate = false;
        });

        cameraGainLabel = new Text(cameraExtendedBox, "-");
        cameraGainLabel->style->text.color  = rgba(232, 232, 232, 0.6f);
        cameraGainLabel->style->text.size   = 9_px;
        cameraGainLabel->style->margin.top  = 3_px;

        // Hidden until a generic UVC camera (not AmScope) is running -- see
        // the attach/detach sync in computeStyle().
        cameraExtendedSlot->removeChild(cameraExtendedBox);
    }

    // -- Calibration Actions panel -------------------------------------------
    // Front-end for calib-dt's CLI scripts (see ../calib-dt), invoked the same
    // way the Slicer panel shells out to pc/slicer.py -- via runCapturedProcess
    // (see runCalibDtSubprocess() in Interface.ixx). calib-dt is a separate,
    // actively-changing scipy/opencv codebase; this panel deliberately does
    // not reimplement any of its feature-extraction/optimization math in C++.

    void Interface::buildCalibrationPanel(Box* body) {
        Text* pathLbl = new Text(body, "CALIB-DT FOLDER");
        pathLbl->style->text.color    = rgba(232, 232, 232, 0.4f);
        pathLbl->style->text.size     = 9_px;
        pathLbl->style->margin.bottom = 4_px;

        Box* pathRow = new Box(body, { &Theme::RowH });
        pathRow->style->layout = { Axis::Horizontal, Align::Start, Align::Center };

        calibDtPathLabel = new Text(pathRow, calibDtRoot.empty() ? "(not set)" : calibDtRoot);
        calibDtPathLabel->style->text.color  = rgba(232, 232, 232, 0.6f);
        calibDtPathLabel->style->text.size   = 10_px;
        calibDtPathLabel->style->text.wrap   = Wrap::BreakWord;
        calibDtPathLabel->style->size.width  = Grow();

        Box* browseBtn = makeBtn(pathRow, "...", [this]() {
            std::string path;
            if (Rev::OS::Dialog::PickFolder(path, "Select calib-dt folder")) {
                calibDtRoot = path;
                settings.calibDtRoot = path;
                saveSettings();
                if (calibDtPathLabel) calibDtPathLabel->content = path;
            }
        });
        browseBtn->style->size     = { 40_px, Grow() };
        browseBtn->style->size.max.width = 40_px;

        // Test-circle diameter -- the calibration slide has two known-size
        // circles (0.15mm and 0.07mm); calib-dt measures um-per-pixel off
        // whichever one SAVE SCALE REF captured, so this has to match
        // whatever's actually under the camera. Two presets fill the box;
        // it stays a free-text field for any other size too (e.g. a
        // different slide). Value is in micrometers, matching calib-dt's
        // own --circle-diameter-um CLI argument directly -- no unit
        // conversion between here and the command built in
        // runCalibrationWithGrid().
        Text* circleLbl = new Text(body, "TEST CIRCLE DIAMETER (um)");
        circleLbl->style->text.color    = rgba(232, 232, 232, 0.4f);
        circleLbl->style->text.size     = 9_px;
        circleLbl->style->margin.top    = 8_px;
        circleLbl->style->margin.bottom = 4_px;

        Box* circleRow = new Box(body, { &Theme::RowH });
        circleRow->style->layout = { Axis::Horizontal, Align::Start, Align::Center };

        circleDiameterInput = makeInput(circleRow, "", std::to_string(settings.circleDiameterUm), 8);
        circleDiameterInput->container->style->size.width = 90_px;
        circleDiameterInput->container->style->margin.right = 6_px;

        makeBtn(circleRow, "0.15mm", [this]() {
            settings.circleDiameterUm = 150;
            if (circleDiameterInput) circleDiameterInput->text->content = "150";
        })->style->size.width = 60_px;

        makeBtn(circleRow, "0.07mm", [this]() {
            settings.circleDiameterUm = 70;
            if (circleDiameterInput) circleDiameterInput->text->content = "70";
        })->style->margin.left = 4_px;

        // Status banner: the ONE thing that should be glanceable without
        // scrolling the runner log below -- idle/running/succeeded/failed,
        // plus a next-step hint, color-coded (see calibStatusLbl handling in
        // computeStyle()). Previously the only feedback was scrolling log
        // text, easy to miss and giving no clear pass/fail signal.
        calibStatusLbl = new Text(body, "Idle -- no calibration action run yet");
        calibStatusLbl->style->text.color    = rgba(232, 232, 232, 0.4f);
        calibStatusLbl->style->text.size     = 10_px;
        calibStatusLbl->style->text.wrap     = Wrap::BreakWord;
        calibStatusLbl->style->margin.top    = 8_px;
        calibStatusLbl->style->margin.bottom = 4_px;

        Text* actLbl = new Text(body, "ACTIONS");
        actLbl->style->text.color    = rgba(232, 232, 232, 0.4f);
        actLbl->style->text.size     = 9_px;
        actLbl->style->margin.top    = 8_px;
        actLbl->style->margin.bottom = 4_px;

        // Defaults mirror calib-dt's own README examples; the log lines each
        // script prints (progress, warnings, output paths) stream straight
        // into the runner log via startCalibDtAction()/runCalibDtSubprocess(),
        // and the pass/fail summary + next step goes to calibStatusLbl above.
        makeBtn(body, "CAPTURE FRAMES", [this]() {
            startCalibDtAction("capture-frames --frames 20 --out captures", "CAPTURE FRAMES",
                "Saved to captures/ -- ready for RUN CALIBRATION.", /*usesCamera=*/true);
        })->style->margin.bottom = 4_px;

        // PROJECT PATTERN and RUN CALIBRATION deliberately do NOT call
        // calib-dt's own project-pattern script or pass it --project-pattern/
        // --projector-monitor -- that opens calib-dt's own fullscreen OpenCV
        // window, a second window uncoordinated with (and fighting for the
        // same monitor as) LithoRev's HDMI passthrough window. Instead the
        // grid is generated and displayed by LithoRev itself (see
        // toggleCalibGrid()/runCalibrationWithGrid(), hdmiCalibGridActive),
        // so it shares the one HDMI window and picks up Flip H/V like every
        // other projector output mode.
        makeBtn(body, "PROJECT PATTERN", [this]() {
            toggleCalibGrid();
        })->style->margin.bottom = 4_px;

        makeBtn(body, "RUN CALIBRATION", [this]() {
            startCalibrationWithGrid();
        })->style->margin.bottom = 4_px;

        makeBtn(body, "ANALYZE SENSITIVITY", [this]() {
            startCalibDtAction("analyze-sensitivity --out sensitivity_output", "ANALYZE SENSITIVITY",
                "Wrote sensitivity_output/ -- review its diagnostic report.");
        });
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
        rightPanelBox = rp;

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
        statusDragHandle = new Box(rp);
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
        statusProgRow = progRow;

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
        runnerLogLbl = runnerLbl;
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
        gcodeColBox = gcodeCol;
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

    // See declaration in Interface.ixx -- shared by both platforms' HDMI
    // paint routines so the frame-composition logic (previously duplicated
    // verbatim in HdmiWindow.win.cpp and HdmiWindow.lnx.cpp) lives in one place.
    // Line-grid calibration target, generated directly at the DMD's native
    // 640x360 (mirrors calib-dt's own make_line_grid(), which draws the same
    // pattern but at the *monitor's* full resolution -- wrong for this rig,
    // since the DMD is a fixed 640x360 device and everything downstream of
    // composeHdmiFrame() assumes that logical resolution). Generating it here
    // instead of in calib-dt means the projected grid gets Flip H/V and goes
    // through the one HDMI window LithoRev already owns, instead of calib-dt
    // opening a second, uncoordinated fullscreen window on top of it.
    static void generateCalibGrid(uint32_t* outPx640x360) {
        constexpr int W = 640, H = 360, pitch = 40, lineWidth = 2;
        std::fill(outPx640x360, outPx640x360 + W * H, 0xFF000000u);

        int half = lineWidth / 2;
        for (int x = W / 2 % pitch; x < W; x += pitch) {
            for (int dx = -half; dx < lineWidth - half; dx++) {
                int xx = x + dx;
                if (xx < 0 || xx >= W) continue;
                for (int y = 0; y < H; y++) outPx640x360[y * W + xx] = 0xFFFFFFFFu;
            }
        }
        for (int y = H / 2 % pitch; y < H; y += pitch) {
            for (int dy = -half; dy < lineWidth - half; dy++) {
                int yy = y + dy;
                if (yy < 0 || yy >= H) continue;
                for (int x = 0; x < W; x++) outPx640x360[yy * W + x] = 0xFFFFFFFFu;
            }
        }
    }

    void Interface::composeHdmiFrame(uint32_t* outPx640x360) {
        std::fill(outPx640x360, outPx640x360 + 640 * 360, 0xFF000000u);

        uint32_t solid = hdmiSolidColor.load();
        if (solid) {
            std::fill(outPx640x360, outPx640x360 + 640 * 360, solid);
        } else if (hdmiCalibGridActive.load()) {
            generateCalibGrid(outPx640x360);
        } else if (hdmiCustomImageActive.load()) {
            std::lock_guard<std::mutex> lk(hdmiFrameMtx);
            if (hdmiCustomImageBGRA.size() == 640u * 360u * 4u) {
                const auto* src = reinterpret_cast<const uint32_t*>(hdmiCustomImageBGRA.data());
                std::copy(src, src + 640 * 360, outPx640x360);
            }
        } else if (hdmiTestActive.load()) {
            std::lock_guard<std::mutex> lk(hdmiFrameMtx);
            if (hdmiTestBGRA.size() == 640u * 360u * 4u) {
                // Mask by the same R/G/B "OUTPUT CHANNELS" checkboxes used for
                // artwork playback, so toggling a channel off blanks it in the
                // test animation too (mask already carries the EVM R<->B swap
                // and the alpha byte -- see the recompute in the update loop).
                const auto* src = reinterpret_cast<const uint32_t*>(hdmiTestBGRA.data());
                uint32_t chanMask = hdmiChannelMask.load();
                for (int i = 0; i < 640 * 360; i++)
                    outPx640x360[i] = src[i] & chanMask;
            }
        } else {
            std::lock_guard<std::mutex> lk(hdmiFrameMtx);
            if (!hdmiCurrentFrame.empty()) {
                const auto& bmp = hdmiCurrentFrame;
                uint32_t onColor = hdmiChannelMask.load();
                for (int i = 0; i < 640 * 360; i++) {
                    uint8_t bit = (bmp[i >> 3] >> (7 - (i & 7))) & 1;
                    outPx640x360[i] = bit ? onColor : 0xFF000000u;
                }
            }
        }

        // Physical flip to compensate for the projector's mount/optical path
        // -- applied last so it covers the solid-color test, RGB test
        // animation, and actual job frames alike (see hdmiFlipH/hdmiFlipV).
        bool flipH = hdmiFlipH.load();
        bool flipV = hdmiFlipV.load();
        if (flipH) {
            for (int y = 0; y < 360; y++) {
                uint32_t* row = outPx640x360 + y * 640;
                std::reverse(row, row + 640);
            }
        }
        if (flipV) {
            for (int y = 0; y < 180; y++) {
                std::swap_ranges(outPx640x360 + y * 640,
                                  outPx640x360 + y * 640 + 640,
                                  outPx640x360 + (359 - y) * 640);
            }
        }
    }

} // namespace LithoControl
