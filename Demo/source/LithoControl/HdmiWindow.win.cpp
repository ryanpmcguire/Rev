module;

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <mutex>
#include <thread>

module LithoControl.Interface;   // implementation unit -- no 'export'

namespace LithoControl {

    static LRESULT CALLBACK HdmiWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        static const UINT WM_LITHO_FRAME = WM_USER + 1;
        if (msg == WM_CREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTA*>(lp);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return 0;
        }
        auto* self = reinterpret_cast<Interface*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        if (msg == WM_LITHO_FRAME || msg == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC hdc = (msg == WM_PAINT) ? BeginPaint(hwnd, &ps) : GetDC(hwnd);
            RECT rc; GetClientRect(hwnd, &rc);
            int ww = rc.right, wh = rc.bottom;
            // Build 32bpp pixel buffer from current 1bpp frame or solid color
            std::vector<uint32_t> px(640 * 360, 0xFF000000u);
            if (self) {
                uint32_t solid = self->hdmiSolidColor.load();
                if (solid) {
                    std::fill(px.begin(), px.end(), solid);
                } else if (self->hdmiTestActive.load()) {
                    std::lock_guard<std::mutex> lk(self->hdmiFrameMtx);
                    if (self->hdmiTestBGRA.size() == 640u * 360u * 4u) {
                        const auto* src = reinterpret_cast<const uint32_t*>(
                            self->hdmiTestBGRA.data());
                        std::copy(src, src + 640 * 360, px.begin());
                    }
                } else {
                    std::lock_guard<std::mutex> lk(self->hdmiFrameMtx);
                    if (!self->hdmiCurrentFrame.empty()) {
                        const auto& bmp = self->hdmiCurrentFrame;
                        uint32_t onColor = self->hdmiChannelMask.load();
                        for (int i = 0; i < 640 * 360; i++) {
                            uint8_t bit = (bmp[i >> 3] >> (7 - (i & 7))) & 1;
                            px[i] = bit ? onColor : 0xFF000000u;
                        }
                    }
                }
            }
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
            bmi.bmiHeader.biWidth       = 640;
            bmi.bmiHeader.biHeight      = -360;
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(hdc, 0, 0, ww, wh, 0, 0, 640, 360,
                          px.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
            if (msg == WM_PAINT) EndPaint(hwnd, &ps);
            else ReleaseDC(hwnd, hdc);
            return 0;
        }
        if (msg == WM_SETCURSOR) {
            SetCursor(NULL);
            return TRUE;
        }
        if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (msg == WM_DESTROY) {
            if (self) self->hdmiWinRunning = false;
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcA(hwnd, msg, wp, lp);
    }

    void Interface::openHdmiWindow() {
        if (hdmiHwnd) { logQ.push("[DISP] Window already open"); return; }
        if (!hdmiDisplayDrop || hdmiDisplayDrop->params.value.empty()) {
            logQ.push("[DISP] Select a display first"); return;
        }
        std::string dev = hdmiDisplayDrop->params.value;
        int mx = 0, my = 0, mw = 640, mh = 360;
        for (auto& d : hdmiDisplays)
            if (d.devName == dev) { mx = d.x; my = d.y; mw = d.w; mh = d.h; break; }

        hdmiWinRunning = true;
        hdmiWinThread = std::thread([this, mx, my, mw, mh]() {
            HINSTANCE hinst = GetModuleHandleA(nullptr);
            WNDCLASSA wc{};
            wc.lpfnWndProc   = HdmiWndProc;
            wc.hInstance     = hinst;
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            wc.lpszClassName = "LithoHdmi";
            RegisterClassA(&wc);

            HWND hwnd = CreateWindowExA(
                WS_EX_TOPMOST, "LithoHdmi", "LithoControl Projector",
                WS_POPUP | WS_VISIBLE,
                mx, my, mw, mh, nullptr, nullptr, hinst, this);
            hdmiHwnd = hwnd;

            if (!hwnd) { hdmiWinRunning = false; logQ.push("[DISP] Window create failed"); return; }
            logQ.push("[DISP] Projector window open (ESC to close)");

            MSG msg;
            while (GetMessageA(&msg, nullptr, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            hdmiHwnd = nullptr;
            UnregisterClassA("LithoHdmi", hinst);
            logQ.push("[DISP] Projector window closed");
        });
        hdmiWinThread.detach();
    }

    void Interface::closeHdmiWindow() {
        if (hdmiHwnd) PostMessageA((HWND)hdmiHwnd, WM_CLOSE, 0, 0);
    }

    void Interface::requestHdmiRepaint() {
        if (hdmiHwnd) PostMessageA((HWND)hdmiHwnd, WM_USER + 1, 0, 0);
    }

    // ---------------------------------------------------------------------
    // EDID apply / display driver restart (pc/apply_edid.bat, pc/restart_driver.bat)
    //
    // Lives here rather than its own EdidApply.win.cpp: a standalone 4th
    // implementation unit of LithoControl.Interface reproducibly hit an
    // MSVC C++20-modules internal compiler error (C1116, "importing module
    // Rev.SocketClient... Specialization of std::_Stop_callback_base::
    // _Do_attach") regardless of that file's actual content -- confirmed by
    // reducing it to a single-line stub and even renaming it, both still
    // failed identically. Folding it into an existing (3rd) implementation
    // unit sidesteps whatever internal limit that is.
    // ---------------------------------------------------------------------

    // Elevates a batch script via UAC and waits for it to exit. Returns the
    // process exit code, or (DWORD)-1 if it couldn't be launched (e.g. UAC
    // declined).
    static DWORD runElevatedBatch(Interface& self, const std::string& script, const std::string& cwd) {
        SHELLEXECUTEINFOA sei{};
        sei.cbSize      = sizeof(sei);
        sei.fMask       = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb      = "runas";
        sei.lpFile      = script.c_str();
        sei.lpDirectory = cwd.empty() ? nullptr : cwd.c_str();
        sei.nShow       = SW_SHOWNORMAL;

        if (!ShellExecuteExA(&sei) || !sei.hProcess) {
            self.logQ.push("[ERR] Could not launch " + script + " (UAC declined?)");
            return (DWORD)-1;
        }

        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return exitCode;
    }

    // Plain Win32 equivalent of std::filesystem::exists() -- avoids pulling in
    // <filesystem> here at all (see EdidApply history for why that matters).
    static bool fileExists(const std::string& path) {
        return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    // stdout can't be piped across the elevation boundary the way
    // runSlicerSubprocess() does, so the batch instead logs to
    // %TEMP%\litho_apply_edid.log and this tails that file once it exits.
    //
    // The driver restart is deliberately a *separate* elevated script
    // (pc/restart_driver.bat), only run after the user clicks OK on a prompt --
    // reloading the driver drops LithoRev's own live GPU context, so we warn
    // and let the user restart LithoRev afterward rather than doing it blind.
    void Interface::runEdidApplySubprocess(const std::string& repoRoot) {
        std::string installScript = repoRoot.empty()
            ? "pc/apply_edid.bat"
            : (repoRoot + "/pc/apply_edid.bat");

        if (!fileExists(installScript)) {
            logQ.push("[ERR] Not found: " + installScript);
            pendingEdidDone = true;
            return;
        }

        char tempDir[MAX_PATH];
        GetTempPathA(MAX_PATH, tempDir);
        std::string logPath = std::string(tempDir) + "litho_apply_edid.log";
        DeleteFileA(logPath.c_str());   // clear so we don't tail a stale run

        DWORD installExit = runElevatedBatch(*this, installScript, repoRoot);
        size_t linesSoFar = tailLogFile(logPath, 0);

        if (installExit != 0) {
            logQ.push("[ERR] apply_edid.bat exited with code " + std::to_string(installExit));
            pendingEdidDone = true;
            return;
        }

        logQ.push("[OK] EDID override installed.");

        auto choice = Rev::OS::Dialog::Confirm("Restart display driver?",
            "The projector EDID override was installed.\n\n"
            "Click OK to restart the display driver now and apply it.\n"
            "LithoRev's own display may drop when this happens -- if the "
            "window closes or goes blank, restart LithoRev afterward.");

        if (choice != Rev::OS::DialogResult::Yes) {
            logQ.push("Driver restart skipped -- reboot manually to apply the override.");
            pendingEdidDone = true;
            return;
        }

        std::string restartScript = repoRoot.empty()
            ? "pc/restart_driver.bat"
            : (repoRoot + "/pc/restart_driver.bat");

        if (!fileExists(restartScript)) {
            logQ.push("[ERR] Not found: " + restartScript);
            pendingEdidDone = true;
            return;
        }

        DWORD restartExit = runElevatedBatch(*this, restartScript, repoRoot);
        tailLogFile(logPath, linesSoFar);

        logQ.push(restartExit == 0
            ? "[OK] Driver restart finished -- reopen LithoRev if it closed"
            : "[ERR] restart_driver.bat exited with code " + std::to_string(restartExit));
        pendingEdidDone = true;
    }

} // namespace LithoControl
