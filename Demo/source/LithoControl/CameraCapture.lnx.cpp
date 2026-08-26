module;

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <utility>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <linux/videodev2.h>

module LithoControl.Interface;   // implementation unit -- no 'export'

namespace LithoControl {

    struct V4L2MappedBuffer { void* start; size_t length; };

    static inline uint8_t clampByte(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

    // Standard BT.601 YUYV (YUY2) -> RGBA. V4L2 has no built-in format-
    // conversion pipeline the way Media Foundation's video processor MFT
    // does, and YUYV is close to universally supported by UVC webcams where
    // RGB24 often isn't, so this is the fallback path most cameras take.
    static void yuyvToRgba(const uint8_t* src, int w, int h, std::vector<uint8_t>& out) {
        out.resize((size_t)w * h * 4);
        for (int y = 0; y < h; y++) {
            const uint8_t* row = src + (size_t)y * w * 2;
            uint8_t* orow = out.data() + (size_t)y * w * 4;
            for (int x = 0; x + 1 < w; x += 2) {
                int y0 = row[x*2+0], u = row[x*2+1], y1 = row[x*2+2], v = row[x*2+3];
                for (int i = 0; i < 2; i++) {
                    int yy = (i == 0) ? y0 : y1;
                    int c = yy - 16, d = u - 128, e = v - 128;
                    int r = (298*c + 409*e + 128) >> 8;
                    int g = (298*c - 100*d - 208*e + 128) >> 8;
                    int b = (298*c + 516*d + 128) >> 8;
                    uint8_t* p = orow + (x+i)*4;
                    p[0] = clampByte(r); p[1] = clampByte(g); p[2] = clampByte(b); p[3] = 255;
                }
            }
        }
    }

    void Interface::scanCameras() {
        cameraDevices.clear();
        std::vector<Dropdown::Option> opts;

        // /dev/video0..63: modern UVC devices often expose several nodes per
        // physical camera (capture, metadata, ...) -- only list the ones that
        // actually advertise video capture.
        for (int i = 0; i < 64; i++) {
            std::string path = "/dev/video" + std::to_string(i);
            int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
            if (fd < 0) continue;

            v4l2_capability cap{};
            // cap.capabilities reports the UNION of capabilities across every
            // sibling device node a driver exposes (capture, metadata, ...),
            // not just this one -- checking it directly makes a camera's
            // metadata-only node falsely claim VIDEO_CAPTURE too, listing
            // every physical camera twice. cap.device_caps holds THIS node's
            // actual capabilities; use it whenever the driver advertises it
            // (V4L2_CAP_DEVICE_CAPS), which is the case for any reasonably
            // modern V4L2 driver.
            if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
                uint32_t deviceCaps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                                     ? cap.device_caps : cap.capabilities;
                if (deviceCaps & V4L2_CAP_VIDEO_CAPTURE) {
                    std::string name(reinterpret_cast<const char*>(cap.card));
                    if (name.empty()) name = path;

                    // Some cameras legitimately expose MORE THAN ONE node
                    // that both correctly report VIDEO_CAPTURE in their own
                    // device_caps (not the union-capabilities false-positive
                    // the check above already handles) -- e.g. separate
                    // nodes for different format/resolution sets on the same
                    // physical sensor. Comparing full label+path (previous
                    // attempt) can never catch this: every /dev/videoN path
                    // is unique by construction, so that check only ever
                    // fired for a literal re-scan, never for a real second
                    // node of the same camera -- confirmed still showing a
                    // single USB camera twice on the target. Dedup by NAME
                    // alone instead: same cap.card string reported twice
                    // means the same physical camera, keep only the first
                    // (lowest-index) node -- it's the one that actually
                    // opens correctly for every camera tested so far.
                    bool alreadyListed = false;
                    for (auto& d : cameraDevices) if (d.name == name) { alreadyListed = true; break; }
                    if (!alreadyListed) {
                        std::string label = name + " (" + path + ")";
                        cameraDevices.push_back({ name });
                        opts.push_back({ label, std::to_string(i) });
                    }
                }
            }
            close(fd);
        }

        if (cameraDrop) cameraDrop->params.options = opts;
        logQ.push("[CAM] Found " + std::to_string(cameraDevices.size()) + " camera(s)");
    }

    void Interface::startCamera() {
        if (!cameraDrop || cameraDrop->params.value.empty()) {
            logQ.push("[CAM] Select a camera first"); return;
        }
        int idx = 0;
        try { idx = std::stoi(cameraDrop->params.value); }
        catch (...) { logQ.push("[CAM] Invalid device"); return; }

        if (cameraBtnTxt) cameraBtnTxt->content = "STOP";
        cameraRunning = true;
        if (cameraThread.joinable()) cameraThread.detach();
        cameraThread = std::thread([this, idx]() { runCameraCapture(idx); });
    }

    void Interface::stopCamera() {
        // No Flush()-equivalent to unblock a pending read the way Windows does
        // -- the capture loop below polls with a short timeout specifically so
        // it re-checks cameraRunning promptly instead of blocking indefinitely.
        cameraRunning = false;
        if (cameraThread.joinable()) cameraThread.join();
        if (cameraBtnTxt) cameraBtnTxt->content = "START";
    }

    // -- Extended UVC controls (exposure / gain-ISO) -------------------------
    // Standard V4L2 controls -- supported by any UVC-compliant camera (the
    // 8MP USB camera etc.); not called for the AmScope SDK backend, which
    // doesn't exist on Linux (see CameraBackend, Windows-only).
    static void queryCameraExtendedRange(Interface* self, int fd) {
        v4l2_queryctrl q{};

        q.id = V4L2_CID_EXPOSURE_ABSOLUTE;
        if (ioctl(fd, VIDIOC_QUERYCTRL, &q) == 0 && !(q.flags & V4L2_CTRL_FLAG_DISABLED)) {
            self->cameraExposureMin = q.minimum;
            self->cameraExposureMax = q.maximum;
            v4l2_control c{}; c.id = V4L2_CID_EXPOSURE_ABSOLUTE;
            self->cameraExposureVal = (ioctl(fd, VIDIOC_G_CTRL, &c) == 0) ? c.value : q.default_value;
        } else {
            self->cameraExposureMin = self->cameraExposureMax = 0;
        }

        v4l2_control autoExpo{};
        autoExpo.id = V4L2_CID_EXPOSURE_AUTO;
        autoExpo.value = self->cameraAutoExposure.load() ? V4L2_EXPOSURE_APERTURE_PRIORITY : V4L2_EXPOSURE_MANUAL;
        ioctl(fd, VIDIOC_S_CTRL, &autoExpo);   // best-effort; not every driver exposes this control

        q = {};
        q.id = V4L2_CID_GAIN;
        if (ioctl(fd, VIDIOC_QUERYCTRL, &q) == 0 && !(q.flags & V4L2_CTRL_FLAG_DISABLED)) {
            self->cameraGainMin = q.minimum;
            self->cameraGainMax = q.maximum;
            v4l2_control c{}; c.id = V4L2_CID_GAIN;
            self->cameraGainVal = (ioctl(fd, VIDIOC_G_CTRL, &c) == 0) ? c.value : q.default_value;
        } else {
            self->cameraGainMin = self->cameraGainMax = 0;
        }

        self->cameraCtrlFd.store(fd);
    }

    void Interface::setCameraExposure(int value) {
        int fd = cameraCtrlFd.load();
        if (fd < 0) return;
        v4l2_control c{}; c.id = V4L2_CID_EXPOSURE_ABSOLUTE; c.value = value;
        ioctl(fd, VIDIOC_S_CTRL, &c);
    }

    void Interface::setCameraGain(int value) {
        int fd = cameraCtrlFd.load();
        if (fd < 0) return;
        v4l2_control c{}; c.id = V4L2_CID_GAIN; c.value = value;
        ioctl(fd, VIDIOC_S_CTRL, &c);
    }

    void Interface::setCameraAutoExposure(bool enabled) {
        int fd = cameraCtrlFd.load();
        if (fd < 0) return;
        v4l2_control c{};
        c.id = V4L2_CID_EXPOSURE_AUTO;
        c.value = enabled ? V4L2_EXPOSURE_APERTURE_PRIORITY : V4L2_EXPOSURE_MANUAL;
        ioctl(fd, VIDIOC_S_CTRL, &c);
    }

    void Interface::runCameraCapture(int deviceIdx) {
        std::string path = "/dev/video" + std::to_string(deviceIdx);
        int fd = open(path.c_str(), O_RDWR);
        if (fd < 0) {
            logQ.push("[CAM] Failed to open " + path);
            cameraRunning = false;
            return;
        }

        // Read the driver's default format (keeps its default resolution,
        // mirroring how the Windows path only forces the pixel format and
        // leaves frame size alone), then try RGB24 first and fall back to
        // YUYV if the driver doesn't support RGB24 directly.
        v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
            logQ.push("[CAM] G_FMT failed");
            close(fd);
            cameraRunning = false;
            return;
        }

        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        bool isRgb24 = (ioctl(fd, VIDIOC_S_FMT, &fmt) == 0 &&
                        fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24);

        if (!isRgb24) {
            fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
            if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0 ||
                fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
                logQ.push("[CAM] Neither RGB24 nor YUYV supported by this camera");
                close(fd);
                cameraRunning = false;
                return;
            }
        }

        int w = (int)fmt.fmt.pix.width, h = (int)fmt.fmt.pix.height;

        v4l2_requestbuffers req{};
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
            logQ.push("[CAM] Buffer request failed");
            close(fd);
            cameraRunning = false;
            return;
        }

        std::vector<V4L2MappedBuffer> buffers(req.count);
        bool mmapFailed = false;
        for (unsigned i = 0; i < req.count; i++) {
            v4l2_buffer buf{};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) { mmapFailed = true; break; }

            buffers[i].length = buf.length;
            buffers[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, fd, buf.m.offset);
            if (buffers[i].start == MAP_FAILED) { mmapFailed = true; break; }

            ioctl(fd, VIDIOC_QBUF, &buf);
        }

        if (mmapFailed) {
            logQ.push("[CAM] Buffer mapping failed");
            for (auto& b : buffers) if (b.start && b.start != MAP_FAILED) munmap(b.start, b.length);
            close(fd);
            cameraRunning = false;
            return;
        }

        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            logQ.push("[CAM] STREAMON failed");
            for (auto& b : buffers) munmap(b.start, b.length);
            close(fd);
            cameraRunning = false;
            return;
        }

        // Extended exposure/gain controls -- best-effort, queried once
        // streaming so the device is fully configured; not every UVC driver
        // supports these (see queryCameraExtendedRange()).
        queryCameraExtendedRange(this, fd);

        logQ.push("[CAM] Live: " + std::to_string(w) + "x" + std::to_string(h) +
                   (isRgb24 ? " RGB24" : " YUYV"));

        std::vector<uint8_t> rgba;

        while (cameraRunning) {

            pollfd pfd{ fd, POLLIN, 0 };
            int pr = poll(&pfd, 1, 200);   // re-check cameraRunning promptly on stop
            if (pr <= 0) continue;

            v4l2_buffer buf{};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN) continue;
                break;
            }

            const uint8_t* src = static_cast<const uint8_t*>(buffers[buf.index].start);
            if (isRgb24) {
                rgba.resize((size_t)w * h * 4);
                for (int i = 0; i < w * h; i++) {
                    rgba[i*4+0] = src[i*3+0];
                    rgba[i*4+1] = src[i*3+1];
                    rgba[i*4+2] = src[i*3+2];
                    rgba[i*4+3] = 255;
                }
            } else {
                yuyvToRgba(src, w, h, rgba);
            }

            if (shouldEmitCameraFrame()) {
                applyCameraAdjustments(rgba, w, h);
                {
                    // swap, not copy -- leaves the old (already correctly-sized)
                    // buffer in `rgba` for the next iteration's resize()/fill
                    // instead of paying for a full-frame copy under the lock
                    std::lock_guard<std::mutex> lk(cameraFrameMtx);
                    std::swap(cameraFrameRGBA, rgba);
                    cameraFrameW = w; cameraFrameH = h;
                }
                cameraFrameReady = true;
                requestRepaint();
            }

            ioctl(fd, VIDIOC_QBUF, &buf);
        }

        ioctl(fd, VIDIOC_STREAMOFF, &type);
        for (auto& b : buffers) munmap(b.start, b.length);
        cameraCtrlFd.store(-1);
        cameraExposureMin = cameraExposureMax = 0;
        cameraGainMin     = cameraGainMax     = 0;
        close(fd);
        cameraRunning = false;
        logQ.push("[CAM] Stopped");
    }

} // namespace LithoControl
