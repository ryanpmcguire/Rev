module;

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <thread>
#include <algorithm>

module LithoControl.Interface;   // implementation unit -- no 'export'

namespace LithoControl {

    // Opaque state behind Interface::hdmiHwnd (void*) on Linux. Deliberately a
    // dedicated XOpenDisplay() connection, independent of Rev's own NativeWindow
    // connection -- keeps this feature fully self-contained, same as the Windows
    // side never touches the main app window's HWND.
    struct HdmiX11State {
        Display* display = nullptr;
        ::Window window  = 0;
        GC       gc      = 0;
        Atom     repaintAtom = 0;
        int      w = 640, h = 360;
    };

    // uint32_t values throughout this feature are BGRA-in-memory (matches the
    // Win32 GDI convention the rest of Interface.ixx was written against),
    // which on a little-endian machine is exactly the 0x00RRGGBB pixel format
    // X11's default TrueColor visual expects at depth 24 -- no conversion needed.
    static void paintHdmiWindow(Interface* self, HdmiX11State* state) {

        std::vector<uint32_t> px(640 * 360, 0xFF000000u);
        uint32_t solid = self->hdmiSolidColor.load();
        if (solid) {
            std::fill(px.begin(), px.end(), solid);
        } else if (self->hdmiTestActive.load()) {
            std::lock_guard<std::mutex> lk(self->hdmiFrameMtx);
            if (self->hdmiTestBGRA.size() == 640u * 360u * 4u) {
                const auto* src = reinterpret_cast<const uint32_t*>(self->hdmiTestBGRA.data());
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

        // Nearest-neighbor scale to the window's actual size -- X11 has no
        // built-in stretch-blit like GDI's StretchDIBits.
        std::vector<uint32_t> scaled((size_t)state->w * state->h);
        for (int y = 0; y < state->h; y++) {
            int sy = y * 360 / state->h;
            for (int x = 0; x < state->w; x++) {
                int sx = x * 640 / state->w;
                scaled[(size_t)y * state->w + x] = px[(size_t)sy * 640 + sx];
            }
        }

        XImage* img = XCreateImage(
            state->display, DefaultVisual(state->display, DefaultScreen(state->display)),
            24, ZPixmap, 0, reinterpret_cast<char*>(scaled.data()),
            state->w, state->h, 32, 0);
        if (!img) return;

        XPutImage(state->display, state->window, state->gc, img,
                  0, 0, 0, 0, state->w, state->h);

        // `scaled` is stack/vector-owned, not img->data -- detach before
        // destroying the image so it doesn't free() memory it doesn't own.
        // Called through the image's own function table rather than the
        // XDestroyImage() macro, which some Xlib.h layouts don't expose here.
        img->data = nullptr;
        img->f.destroy_image(img);

        XFlush(state->display);
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

            auto* state = new HdmiX11State();
            state->w = mw; state->h = mh;

            // A separate XOpenDisplay() connection to the same X server;
            // safe to use from this thread concurrently with Rev's own main-
            // window connection because Rev already calls XInitThreads()
            // during its own NativeWindow startup (before the GUI can even
            // be interacted with, i.e. before openHdmiWindow() is reachable).
            state->display = XOpenDisplay(nullptr);
            if (!state->display) {
                hdmiWinRunning = false;
                logQ.push("[DISP] XOpenDisplay failed");
                delete state;
                return;
            }

            int screen = DefaultScreen(state->display);
            ::Window root = RootWindow(state->display, screen);

            XSetWindowAttributes attrs{};
            attrs.override_redirect = True;   // no WM decoration, bypasses WM entirely --
                                               // the X11 analog of WS_POPUP | WS_EX_TOPMOST
            attrs.background_pixel  = BlackPixel(state->display, screen);
            attrs.event_mask        = ExposureMask | KeyPressMask | StructureNotifyMask;

            state->window = XCreateWindow(
                state->display, root,
                mx, my, mw, mh, 0,
                CopyFromParent, InputOutput, CopyFromParent,
                CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);

            state->gc = XCreateGC(state->display, state->window, 0, nullptr);
            state->repaintAtom = XInternAtom(state->display, "LITHO_HDMI_REPAINT", False);

            XMapRaised(state->display, state->window);
            XFlush(state->display);

            hdmiHwnd = state;
            logQ.push("[DISP] Projector window open (ESC to close)");

            paintHdmiWindow(this, state);

            while (hdmiWinRunning.load()) {
                while (XPending(state->display) > 0) {
                    XEvent ev;
                    XNextEvent(state->display, &ev);

                    if (ev.type == Expose) {
                        paintHdmiWindow(this, state);
                    } else if (ev.type == KeyPress) {
                        KeySym ks = XLookupKeysym(&ev.xkey, 0);
                        if (ks == XK_Escape) hdmiWinRunning = false;
                    } else if (ev.type == ClientMessage &&
                               ev.xclient.message_type == state->repaintAtom) {
                        paintHdmiWindow(this, state);
                    }
                }
                if (!hdmiWinRunning.load()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }

            XDestroyWindow(state->display, state->window);
            XFreeGC(state->display, state->gc);
            XCloseDisplay(state->display);
            delete state;

            hdmiHwnd = nullptr;
            logQ.push("[DISP] Projector window closed");
        });
        hdmiWinThread.detach();
    }

    void Interface::closeHdmiWindow() {
        hdmiWinRunning = false;   // the event loop above notices and tears down
    }

    void Interface::requestHdmiRepaint() {
        if (!hdmiHwnd) return;
        auto* state = reinterpret_cast<HdmiX11State*>(hdmiHwnd);

        // ClientMessage with event_mask=0 in XSendEvent delivers directly to
        // this window's event queue regardless of its selected input masks --
        // the standard idiom for cross-thread/cross-client signaling in X11,
        // equivalent to Win32 PostMessage(hwnd, WM_USER+1, ...).
        XClientMessageEvent ev{};
        ev.type = ClientMessage;
        ev.window = state->window;
        ev.message_type = state->repaintAtom;
        ev.format = 32;
        XSendEvent(state->display, state->window, False, 0, (XEvent*)&ev);
        XFlush(state->display);
    }
}
