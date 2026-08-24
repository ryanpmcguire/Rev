#include "ProjectorChild.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace LithoControl {

    // Fixed 640x360 BGRA source frames arrive as a raw byte stream on `fd`
    // (no framing needed -- every message is exactly this many bytes, see
    // Interface::composeHdmiFrame's fixed contract and the sender side in
    // HdmiWindow.lnx.cpp). Scaled up to the window's actual size with the
    // same nearest-neighbor approach the old in-process version used.
    int runProjectorChild(int argc, char** argv) {
        if (argc < 7) return 1;
        int fd = std::atoi(argv[2]);
        int x  = std::atoi(argv[3]);
        int y  = std::atoi(argv[4]);
        int w  = std::atoi(argv[5]);
        int h  = std::atoi(argv[6]);
        if (w <= 0) w = 640;
        if (h <= 0) h = 360;

        Display* display = XOpenDisplay(nullptr);
        if (!display) return 1;

        int screen = DefaultScreen(display);
        ::Window root = RootWindow(display, screen);

        XSetWindowAttributes attrs{};
        attrs.override_redirect = True;   // no WM decoration, bypasses WM entirely
        attrs.background_pixel  = BlackPixel(display, screen);
        attrs.event_mask        = ExposureMask | KeyPressMask | StructureNotifyMask;

        ::Window window = XCreateWindow(
            display, root, x, y, w, h, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);

        GC gc = XCreateGC(display, window, 0, nullptr);
        XMapRaised(display, window);
        XFlush(display);

        std::vector<uint32_t> px(640u * 360u, 0xFF000000u);
        std::vector<uint32_t> scaled((size_t)w * h);
        bool havePixels = false;

        auto repaint = [&]() {
            for (int yy = 0; yy < h; yy++) {
                int sy = yy * 360 / h;
                for (int xx = 0; xx < w; xx++) {
                    int sx = xx * 640 / w;
                    scaled[(size_t)yy * w + xx] = px[(size_t)sy * 640 + sx];
                }
            }
            XImage* img = XCreateImage(
                display, DefaultVisual(display, screen), 24, ZPixmap, 0,
                reinterpret_cast<char*>(scaled.data()), w, h, 32, 0);
            if (!img) return;
            XPutImage(display, window, gc, img, 0, 0, 0, 0, w, h);
            // `scaled` is vector-owned, not img->data -- detach before
            // destroying so it doesn't free() memory it doesn't own.
            img->data = nullptr;
            img->f.destroy_image(img);
            XFlush(display);
        };

        const size_t frameBytes = px.size() * sizeof(uint32_t);
        std::vector<uint8_t> frameBuf(frameBytes);
        size_t frameFill = 0;

        int xfd = ConnectionNumber(display);
        bool running = true;

        while (running) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            FD_SET(xfd, &rfds);
            int maxfd = (fd > xfd ? fd : xfd);

            timeval tv{ 0, 20000 };   // 20ms -- also the XPending() poll cadence below
            select(maxfd + 1, &rfds, nullptr, nullptr, &tv);

            if (FD_ISSET(fd, &rfds)) {
                ssize_t n = recv(fd, frameBuf.data() + frameFill, frameBytes - frameFill, 0);
                if (n <= 0) {
                    // EOF: the parent either closed us down deliberately
                    // (closeHdmiWindow()) or the whole process is gone --
                    // either way, this is the one signal that means "stop."
                    running = false;
                } else {
                    frameFill += (size_t)n;
                    if (frameFill == frameBytes) {
                        std::memcpy(px.data(), frameBuf.data(), frameBytes);
                        frameFill = 0;
                        havePixels = true;
                        repaint();
                    }
                }
            }

            while (XPending(display) > 0) {
                XEvent ev;
                XNextEvent(display, &ev);
                if (ev.type == Expose) {
                    if (havePixels) repaint();
                } else if (ev.type == KeyPress) {
                    KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    if (ks == XK_Escape) running = false;
                }
            }
        }

        XDestroyWindow(display, window);
        XFreeGC(display, gc);
        XCloseDisplay(display);
        close(fd);
        return 0;
    }
}
