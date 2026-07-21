module;

#include <windows.h>

#include <string>
#include <vector>
#include <functional>

#include <dbg.hpp>

export module Rev.AmcamCamera;

// Thin wrapper around the AmScope/ToupTek "amcam" vendor SDK (amcam.dll),
// loaded dynamically via LoadLibrary/GetProcAddress -- mirrors the ctypes
// bindings in calib-dt's digital_twin/src/camera/amcam.py, which is the
// validated reference for the exact call sequence this camera needs
// (Amcam_EnumV2 -> Amcam_Open -> Amcam_get_Size -> best-effort AutoExpo ->
// Amcam_StartPullModeWithCallback -> Amcam_PullImageV2 per frame).
//
// No amcam.h/amcam.lib is vendored here (none shipped with the installed
// AmScope software) -- all entry points are resolved by name, same as the
// Python ctypes.WinDLL approach.

export namespace Rev {

    struct AmcamCamera {

        struct Device {
            std::string name;
            std::string id;
        };

        // Called on the SDK's internal delivery thread for each captured
        // frame; buffer is packed BGR24, width*height*3 bytes.
        using FrameCallback = std::function<void(const uint8_t* bgr, int width, int height)>;

        void*       cam    = nullptr; // HAmcam
        int         width  = 0;
        int         height = 0;
        FrameCallback callback;
        std::vector<uint8_t> buffer;

        static std::vector<Device> listDevices() {

            std::vector<Device> out;

            if (!loadDll()) return out;

            std::vector<DeviceV2> devices(MaxDevices);
            unsigned n = s_enumV2(devices.data());

            for (unsigned i = 0; i < n && i < MaxDevices; i++) {
                out.push_back({ wideToUtf8(devices[i].displayname), wideToUtf8(devices[i].id) });
            }

            return out;
        }

        // Opens the device identified by `id` (as returned by listDevices())
        // and starts streaming immediately; check connected() afterwards.
        AmcamCamera(const std::string& id, FrameCallback cb) : callback(std::move(cb)) {

            if (!loadDll()) {
                dbg("[AmcamCamera] amcam.dll not available");
                return;
            }

            std::wstring wid = utf8ToWide(id);
            cam = s_open(wid.c_str());

            if (!cam) {
                dbg("[AmcamCamera] Open failed for id=%s", id.c_str());
                return;
            }

            int w = 0, h = 0;
            if (FAILED(s_getSize(cam, &w, &h)) || w <= 0 || h <= 0) {
                dbg("[AmcamCamera] get_Size failed");
                s_close(cam);
                cam = nullptr;
                return;
            }

            width  = w;
            height = h;
            buffer.resize((size_t)width * height * 3);

            // Best-effort: some SDK/camera combos don't support these.
            s_putAutoExpoEnable(cam, 1);
            s_putAutoExpoTarget(cam, 160);

            HRESULT hr = s_startPullModeWithCallback(cam, &AmcamCamera::onEventTrampoline, this);

            if (FAILED(hr)) {
                dbg("[AmcamCamera] StartPullModeWithCallback failed hr=0x%08x", (unsigned)hr);
                s_close(cam);
                cam = nullptr;
                return;
            }

            dbg("[AmcamCamera] Streaming %dx%d", width, height);
        }

        ~AmcamCamera() {

            if (cam) {
                s_stop(cam);
                s_close(cam);
                cam = nullptr;
            }
        }

        bool connected() const { return cam != nullptr; }

    private:

        static constexpr unsigned MaxDevices        = 16;
        static constexpr unsigned AMCAM_EVENT_IMAGE  = 0x0004;

        struct DeviceV2 {
            wchar_t displayname[64];
            wchar_t id[64];
            void*   model;
        };

        struct FrameInfoV2 {
            unsigned           width;
            unsigned           height;
            unsigned           flag;
            unsigned           seq;
            unsigned long long timestamp;
        };

        using PEnumV2                     = unsigned (__stdcall*)(DeviceV2*);
        using POpen                       = void*    (__stdcall*)(const wchar_t*);
        using PClose                      = void     (__stdcall*)(void*);
        using PGetSize                    = HRESULT  (__stdcall*)(void*, int*, int*);
        using PPutAutoExpoEnable          = HRESULT  (__stdcall*)(void*, int);
        using PPutAutoExpoTarget          = HRESULT  (__stdcall*)(void*, unsigned short);
        using PEventCallback              = void     (__stdcall*)(unsigned, void*);
        using PStartPullModeWithCallback  = HRESULT  (__stdcall*)(void*, PEventCallback, void*);
        using PPullImageV2                = HRESULT  (__stdcall*)(void*, void*, int, FrameInfoV2*);
        using PStop                       = HRESULT  (__stdcall*)(void*);

        static inline HMODULE                     s_dll                        = nullptr;
        static inline PEnumV2                      s_enumV2                     = nullptr;
        static inline POpen                        s_open                       = nullptr;
        static inline PClose                       s_close                      = nullptr;
        static inline PGetSize                     s_getSize                    = nullptr;
        static inline PPutAutoExpoEnable           s_putAutoExpoEnable          = nullptr;
        static inline PPutAutoExpoTarget           s_putAutoExpoTarget          = nullptr;
        static inline PStartPullModeWithCallback   s_startPullModeWithCallback  = nullptr;
        static inline PPullImageV2                 s_pullImageV2                = nullptr;
        static inline PStop                        s_stop                       = nullptr;

        static bool loadDll() {

            if (s_dll) return true;

            s_dll = LoadLibraryA("C:\\Program Files\\AmScope\\AmScope\\x64\\amcam.dll");
            if (!s_dll) {
                dbg("[AmcamCamera] LoadLibrary(amcam.dll) failed");
                return false;
            }

            s_enumV2                    = (PEnumV2)GetProcAddress(s_dll, "Amcam_EnumV2");
            s_open                      = (POpen)GetProcAddress(s_dll, "Amcam_Open");
            s_close                     = (PClose)GetProcAddress(s_dll, "Amcam_Close");
            s_getSize                   = (PGetSize)GetProcAddress(s_dll, "Amcam_get_Size");
            s_putAutoExpoEnable         = (PPutAutoExpoEnable)GetProcAddress(s_dll, "Amcam_put_AutoExpoEnable");
            s_putAutoExpoTarget         = (PPutAutoExpoTarget)GetProcAddress(s_dll, "Amcam_put_AutoExpoTarget");
            s_startPullModeWithCallback = (PStartPullModeWithCallback)GetProcAddress(s_dll, "Amcam_StartPullModeWithCallback");
            s_pullImageV2               = (PPullImageV2)GetProcAddress(s_dll, "Amcam_PullImageV2");
            s_stop                      = (PStop)GetProcAddress(s_dll, "Amcam_Stop");

            if (!s_enumV2 || !s_open || !s_close || !s_getSize || !s_startPullModeWithCallback || !s_pullImageV2 || !s_stop) {
                dbg("[AmcamCamera] Missing expected amcam.dll export(s)");
                s_dll = nullptr;
                return false;
            }

            return true;
        }

        static std::string wideToUtf8(const wchar_t* w) {

            if (!w || !w[0]) return {};

            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n <= 0) return {};

            std::string s(n - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
            return s;
        }

        static std::wstring utf8ToWide(const std::string& s) {

            if (s.empty()) return {};

            int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            if (n <= 0) return {};

            std::wstring w(n - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
            return w;
        }

        // Invoked by the SDK on its own delivery thread; pulls the ready
        // frame and hands it to the user callback as packed BGR24.
        static void __stdcall onEventTrampoline(unsigned nEvent, void* ctx) {

            if (nEvent != AMCAM_EVENT_IMAGE) return;

            auto* self = reinterpret_cast<AmcamCamera*>(ctx);

            FrameInfoV2 info{};
            HRESULT hr = s_pullImageV2(self->cam, self->buffer.data(), 24, &info);

            if (FAILED(hr) || !info.width || !info.height) return;

            if (self->callback) {
                self->callback(self->buffer.data(), (int)info.width, (int)info.height);
            }
        }
    };
}
