module;

#include <dbg.hpp>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cfgmgr32.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "Cfgmgr32.lib")

export module Rev.Media.NativeCamera;

import Rev.Media.Camera.Types;
import Rev.Core.PixelFormat;

export namespace Rev::Media {

    struct NativeCamera;

    struct CameraReaderCallback final : public IMFSourceReaderCallback {
        explicit CameraReaderCallback(NativeCamera* owner) : owner(owner) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT status, DWORD streamIndex, DWORD flags, LONGLONG timestamp, IMFSample* sample) override;
        HRESULT STDMETHODCALLTYPE OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE OnFlush(DWORD) override { return S_OK; }

        void detach();

        std::atomic<ULONG> references = 1;
        std::mutex ownerMutex;
        NativeCamera* owner = nullptr;
    };

    struct NativeCamera {

        NativeCamera(
            std::function<CameraTypes::Frame*(CameraTypes::Frame*)> callback,
            std::function<void(std::string)> offline,
            std::function<void()> devicesChanged
        ) {
            onFrame = callback;
            onOffline = offline;
            onDevicesChanged = devicesChanged;
            writeFrame = new CameraTypes::Frame;

            if (onDevicesChanged) {
                listenForDeviceChanges();
            }
        }
        NativeCamera(const NativeCamera&) = delete;
        NativeCamera& operator=(const NativeCamera&) = delete;
        ~NativeCamera() {
            stopListeningForDeviceChanges();
            close();
            delete writeFrame;
            writeFrame = nullptr;
        }

        static DWORD CALLBACK deviceNotification(
            HCMNOTIFICATION,
            PVOID context,
            CM_NOTIFY_ACTION action,
            PCM_NOTIFY_EVENT_DATA,
            DWORD
        ) {
            if (
                action != CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL
                && action != CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL
            ) {
                return ERROR_SUCCESS;
            }

            NativeCamera* camera = static_cast<NativeCamera*>(context);

            if (camera != nullptr && camera->onDevicesChanged) {
                camera->onDevicesChanged();
            }

            return ERROR_SUCCESS;
        }

        void listenForDeviceChanges() {
            registerDeviceNotification(KSCATEGORY_VIDEO_CAMERA, videoCameraNotification);
            registerDeviceNotification(KSCATEGORY_CAPTURE, captureNotification);
        }

        void registerDeviceNotification(const GUID& category, HCMNOTIFICATION& notification) {
            CM_NOTIFY_FILTER filter{};
            filter.cbSize = sizeof(filter);
            filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
            filter.u.DeviceInterface.ClassGuid = category;

            if (
                CM_Register_Notification(
                    &filter,
                    this,
                    &NativeCamera::deviceNotification,
                    &notification
                ) != CR_SUCCESS
            ) {
                notification = nullptr;
            }
        }

        void stopListeningForDeviceChanges() {
            if (videoCameraNotification != nullptr) {
                CM_Unregister_Notification(videoCameraNotification);
                videoCameraNotification = nullptr;
            }

            if (captureNotification != nullptr) {
                CM_Unregister_Notification(captureNotification);
                captureNotification = nullptr;
            }
        }

        static std::string narrow(const wchar_t* value) {
            if (!value) { return {}; }
            int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
            if (length <= 1) { return {}; }
            std::string result(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length, nullptr, nullptr);
            result.resize(static_cast<size_t>(length - 1));
            return result;
        }

        static std::string hresultMessage(const char* action, HRESULT result) {
            std::ostringstream message;
            message << action << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ")";
            return message.str();
        }

        static std::vector<CameraTypes::DeviceInfo> devices() {

            std::vector<CameraTypes::DeviceInfo> result;
            HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            bool uninitializeCom = SUCCEEDED(com);
            if (FAILED(com) && com != RPC_E_CHANGED_MODE) { return result; }
            if (FAILED(MFStartup(MF_VERSION))) {
                if (uninitializeCom) { CoUninitialize(); }
                return result;
            }

            IMFAttributes* attributes = nullptr;
            IMFActivate** activations = nullptr;
            UINT32 count = 0;

            if (SUCCEEDED(MFCreateAttributes(&attributes, 1)) &&
                SUCCEEDED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)) &&
                SUCCEEDED(MFEnumDeviceSources(attributes, &activations, &count))) {

                for (UINT32 i = 0; i < count; i++) {
                    wchar_t* name = nullptr;
                    wchar_t* id = nullptr;
                    UINT32 nameLength = 0;
                    UINT32 idLength = 0;
                    activations[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLength);
                    activations[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &id, &idLength);
                    result.push_back({ i, narrow(name), narrow(id) });
                    CoTaskMemFree(name);
                    CoTaskMemFree(id);
                }
            }

            for (UINT32 i = 0; i < count; i++) { if (activations[i]) { activations[i]->Release(); } }
            CoTaskMemFree(activations);
            if (attributes) { attributes->Release(); }
            MFShutdown();
            if (uninitializeCom) { CoUninitialize(); }
            return result;
        }

        bool open(size_t deviceIndex = 0) {

            close();
            clearError();

            HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ownsCom = SUCCEEDED(result);
            if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
                setError(hresultMessage("Could not initialize COM for camera capture", result));
                return false;
            }

            result = MFStartup(MF_VERSION);
            if (FAILED(result)) {
                setError(hresultMessage("Could not start Media Foundation", result));
                releaseRuntime();
                return false;
            }
            ownsMediaFoundation = true;

            IMFAttributes* deviceAttributes = nullptr;
            IMFActivate** activations = nullptr;
            UINT32 count = 0;

            result = MFCreateAttributes(&deviceAttributes, 1);
            if (SUCCEEDED(result)) {
                result = deviceAttributes->SetGUID(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
                );
            }
            if (SUCCEEDED(result)) { result = MFEnumDeviceSources(deviceAttributes, &activations, &count); }

            if (FAILED(result) || deviceIndex >= count) {
                setError(count == 0 ? "No USB/video camera was found" : "Camera device index is out of range");
                if (deviceAttributes) { deviceAttributes->Release(); }
                for (UINT32 i = 0; i < count; i++) { if (activations[i]) { activations[i]->Release(); } }
                CoTaskMemFree(activations);
                releaseRuntime();
                return false;
            }

            IMFAttributes* readerAttributes = nullptr;
            IMFMediaType* outputType = nullptr;

            result = activations[deviceIndex]->ActivateObject(IID_PPV_ARGS(&mediaSource));
            if (SUCCEEDED(result)) {
                mediaSource->QueryInterface(__uuidof(IKsControl), reinterpret_cast<void**>(&ksControl));
            }
            if (SUCCEEDED(result)) { result = MFCreateAttributes(&readerAttributes, 3); }

            callback = new CameraReaderCallback(this);
            if (SUCCEEDED(result)) { result = readerAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback); }
            if (SUCCEEDED(result)) { result = readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE); }
            if (SUCCEEDED(result)) { result = MFCreateSourceReaderFromMediaSource(mediaSource, readerAttributes, &reader); }
            reselected = false;
            bool full = SUCCEEDED(result) && selectFullHd();
            if (SUCCEEDED(result)) { result = MFCreateMediaType(&outputType); }
            if (SUCCEEDED(result)) { result = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); }
            if (SUCCEEDED(result)) { result = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32); }
            if (SUCCEEDED(result) && full) { result = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, 1920, 1080); }
            if (SUCCEEDED(result)) { result = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType); }

            IMFMediaType* actualType = nullptr;
            UINT32 frameWidth = 0;
            UINT32 frameHeight = 0;
            if (SUCCEEDED(result)) { result = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType); }
            if (SUCCEEDED(result)) { result = MFGetAttributeSize(actualType, MF_MT_FRAME_SIZE, &frameWidth, &frameHeight); }

            width = frameWidth;
            height = frameHeight;
            frameFormat = Core::PixelFormat::BGRX8;
            setMediaFormat(describeNativeType());

            if (actualType) { actualType->Release(); }
            if (outputType) { outputType->Release(); }
            if (readerAttributes) { readerAttributes->Release(); }
            if (deviceAttributes) { deviceAttributes->Release(); }
            for (UINT32 i = 0; i < count; i++) { if (activations[i]) { activations[i]->Release(); } }
            CoTaskMemFree(activations);

            if (FAILED(result)) {
                setError(hresultMessage("Could not open the selected camera", result));
                close();
                return false;
            }

            running = true;
            streaming = false;
            samplePending = false;
            return true;
        }

        bool start() {
            if (!isOpen()) { return false; }
            if (streaming.exchange(true)) { return true; }

            HRESULT result = requestNextSample();

            if (FAILED(result)) {
                streaming = false;
                setError(hresultMessage("Could not begin reading camera frames", result));
                return false;
            }

            return true;
        }

        void stop() {
            streaming = false;
        }

        void close() {
            streaming = false;
            running = false;

            if (callback) { callback->detach(); }
            if (reader) {
                reader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
                reader->Release();
                reader = nullptr;
            }
            if (mediaSource) {
                mediaSource->Shutdown();
                mediaSource->Release();
                mediaSource = nullptr;
            }
            if (ksControl) {
                ksControl->Release();
                ksControl = nullptr;
            }
            if (callback) {
                callback->Release();
                callback = nullptr;
            }

            width = 0;
            height = 0;
            {
                std::scoped_lock lock(errorMutex);
                mediaFormatText = "unavailable (not open)";
            }
            samplePending = false;
            releaseRuntime();
        }

        bool isOpen() const { return running && reader; }
        bool isStreaming() const { return streaming; }

        // Diagnostics: the source's negotiated native media type (before the
        // reader converts to RGB32): subtype, size and frame rate.
        std::string mediaFormat() const {
            std::scoped_lock lock(errorMutex);
            return mediaFormatText;
        }

        // Logs only on change (open, or a mid-stream media type change).
        void setMediaFormat(std::string text) {
            text += "; delivered " + std::to_string(width) + "x" + std::to_string(height);
            {
                std::scoped_lock lock(errorMutex);
                if (text == mediaFormatText) { return; }
                mediaFormatText = text;
            }
            dbg("[Camera] media format: %s", text.c_str());
        }

        // The source may renegotiate mid-stream; re-read the output size so
        // width/height never go stale, and record the new native type.
        // Picks a 1920x1080 native type: MJPG, then YUY2, then NV12; highest fps <= 30.
        // Returns false (after a loud log listing the types) when none exists.
        bool selectFullHd() {
            int bestRank = -1;
            double bestFps = 0;
            DWORD bestIndex = 0;
            std::string available;
            for (DWORD i = 0;; ++i) {
                IMFMediaType* type = nullptr;
                if (FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type))) { break; }
                GUID subtype{};
                UINT32 w = 0, h = 0, num = 0, den = 0;
                type->GetGUID(MF_MT_SUBTYPE, &subtype);
                MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
                MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den);
                type->Release();
                double fps = den ? double(num) / den : 0;
                int rank = subtype == MFVideoFormat_MJPG ? 3 : subtype == MFVideoFormat_YUY2 ? 2
                    : subtype == MFVideoFormat_NV12 ? 1 : 0;
                char fourcc[5] = {char(subtype.Data1 & 0xFF), char((subtype.Data1 >> 8) & 0xFF),
                    char((subtype.Data1 >> 16) & 0xFF), char((subtype.Data1 >> 24) & 0xFF), 0};
                available += std::string(" ") + fourcc + " " + std::to_string(w) + "x" + std::to_string(h)
                    + "@" + std::to_string(int(fps + 0.5)) + ";";
                if (w != 1920 || h != 1080 || rank == 0 || fps > 30.5) { continue; }
                if (rank > bestRank || (rank == bestRank && fps > bestFps)) {
                    bestRank = rank; bestFps = fps; bestIndex = i;
                }
            }
            IMFMediaType* chosen = nullptr;
            if (bestRank > 0 && SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, bestIndex, &chosen))) {
                HRESULT set = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, chosen);
                chosen->Release();
                if (SUCCEEDED(set)) { return true; }
            }
            dbg("[Camera] WARNING: no usable 1920x1080 native type; keeping the device default. Available:%s",
                available.c_str());
            return false;
        }

        void refreshMediaType() {
            if (!reselected && width && height) {
                IMFMediaType* native = nullptr;
                UINT32 nw = 0, nh = 0;
                if (SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                        MF_SOURCE_READER_CURRENT_TYPE_INDEX, &native))) {
                    MFGetAttributeSize(native, MF_MT_FRAME_SIZE, &nw, &nh);
                    native->Release();
                }
                if (nw != 1920 || nh != 1080) {
                    reselected = true;  // once per open: never loop against the device
                    IMFMediaType* output = nullptr;
                    if (selectFullHd() && SUCCEEDED(MFCreateMediaType(&output))) {
                        output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                        output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
                        MFSetAttributeSize(output, MF_MT_FRAME_SIZE, 1920, 1080);
                        HRESULT set = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output);
                        dbg("[Camera] mid-stream reselect to 1920x1080: SetCurrentMediaType hr=0x%08lX%s",
                            static_cast<unsigned long>(set), SUCCEEDED(set) ? "" : " (FAILED; close and reopen to recover)");
                    }
                    if (output) { output->Release(); }
                }
            }
            IMFMediaType* current = nullptr;
            UINT32 w = 0, h = 0;
            if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current))
                && SUCCEEDED(MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &w, &h))) {
                width = w;
                height = h;
            }
            if (current) { current->Release(); }
            setMediaFormat(describeNativeType());
        }

        std::string describeNativeType() const {
            if (!reader) return "unavailable (no reader)";
            IMFMediaType* native = nullptr;
            HRESULT result = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                MF_SOURCE_READER_CURRENT_TYPE_INDEX, &native);
            if (FAILED(result) || !native)
                return "unavailable (GetNativeMediaType failed)";
            GUID subtype{};
            std::string name = "unknown";
            if (SUCCEEDED(native->GetGUID(MF_MT_SUBTYPE, &subtype))) {
                if (subtype == MFVideoFormat_MJPG) name = "MJPG";
                else if (subtype == MFVideoFormat_YUY2) name = "YUY2";
                else if (subtype == MFVideoFormat_NV12) name = "NV12";
                else if (subtype == MFVideoFormat_RGB32) name = "RGB32";
                else if (subtype == MFVideoFormat_RGB24) name = "RGB24";
                else {
                    char fourcc[5] = {
                        char(subtype.Data1 & 0xFF), char((subtype.Data1 >> 8) & 0xFF),
                        char((subtype.Data1 >> 16) & 0xFF), char((subtype.Data1 >> 24) & 0xFF), 0};
                    name = std::string("fourcc ") + fourcc;
                }
            }
            UINT32 w = 0, h = 0, numerator = 0, denominator = 0;
            MFGetAttributeSize(native, MF_MT_FRAME_SIZE, &w, &h);
            MFGetAttributeRatio(native, MF_MT_FRAME_RATE, &numerator, &denominator);
            native->Release();
            return name + " " + std::to_string(w) + "x" + std::to_string(h) + " @ "
                + (denominator ? std::to_string(double(numerator) / denominator) : std::string("?"))
                + " fps (" + std::to_string(numerator) + "/" + std::to_string(denominator)
                + "); reader output RGB32";
        }

        std::string error() const {
            std::scoped_lock lock(errorMutex);
            return errorText;
        }

        std::vector<CameraTypes::Setting> readControls() {
            std::vector<CameraTypes::Setting> result;
            if (!ksControl) { return result; }
            for (const auto& descriptor : controlDescriptors()) {
                CameraTypes::Setting info;
                if (queryControl(descriptor, info)) { result.push_back(std::move(info)); }
            }
            return result;
        }

        bool writeControl(CameraTypes::Setting::Type control, long value, CameraTypes::Setting::Mode mode,
                          CameraTypes::Setting& actual) {
            const auto* descriptor = descriptorFor(control);
            return ksControl && descriptor && writeControl(*descriptor, value, mode)
                && queryControl(*descriptor, actual);
        }

        HRESULT requestNextSample() {
            if (!running || !streaming || !reader) { return S_FALSE; }
            if (samplePending.exchange(true)) { return S_OK; }

            HRESULT result = reader->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                0,
                nullptr,
                nullptr,
                nullptr,
                nullptr
            );

            if (FAILED(result)) {
                samplePending = false;
            }

            return result;
        }

        void receive(HRESULT status, DWORD flags, LONGLONG timestamp, IMFSample* sample) {

            if (FAILED(status)) {
                lost(hresultMessage("Camera frame read failed", status));
                return;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                lost("Camera stream ended");
                return;
            }
            if (reader && (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED
                | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED))) {
                dbg("[Camera] media type changed mid-stream (flags 0x%lx)", static_cast<unsigned long>(flags));
                refreshMediaType();
            }
            if (!sample || width == 0 || height == 0) { return; }

            const double receivedAtMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const LONGLONG clock100ns = MFGetSystemTime();
            UINT64 device100ns = 0;
            double capturedAtMs = 0;
            // DeviceTimestamp shares MFGetSystemTime's QPC epoch, unlike sample time.
            // Missing, future or implausibly old driver timestamps remain zero.
            if (SUCCEEDED(sample->GetUINT64(MFSampleExtension_DeviceTimestamp, &device100ns)) &&
                device100ns > 0 && clock100ns > 0 && device100ns <= UINT64(clock100ns) &&
                UINT64(clock100ns) - device100ns <= 50000000ULL) {
                capturedAtMs = receivedAtMs - double(UINT64(clock100ns) - device100ns) / 10000.0;
            }

            IMFMediaBuffer* buffer = nullptr;
            DWORD bufferCount = 0;
            sample->GetBufferCount(&bufferCount);
            HRESULT bufferResult = bufferCount == 1
                ? sample->GetBufferByIndex(0, &buffer)
                : sample->ConvertToContiguousBuffer(&buffer);
            if (FAILED(bufferResult) || !buffer) { return; }

            auto publish = [this, receivedAtMs, capturedAtMs, timestamp]() {
                CameraTypes::Frame* submitted = writeFrame;
                submitted->size = { width, height, 4 };
                submitted->format = frameFormat;
                submitted->timestamp = {
                    .capturedAtMs = capturedAtMs,
                    .receivedAtMs = receivedAtMs,
                    .timestamp100ns = static_cast<uint64_t>(timestamp)
                };
                if (onFrame) { writeFrame = onFrame(submitted); }
                submitted->pixels = nullptr;
                submitted->pitch = 0;
            };

            IMF2DBuffer* buffer2d = nullptr;

            if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d)))) {
                BYTE* scanline = nullptr;
                LONG pitch = 0;
                if (SUCCEEDED(buffer2d->Lock2D(&scanline, &pitch))) {
                    if (pitch > 0) {
                        writeFrame->pixels = scanline;
                        writeFrame->pitch = static_cast<size_t>(pitch);
                        publish();
                    }
                    buffer2d->Unlock2D();
                }
                buffer2d->Release();
            }
            else {
                BYTE* bytes = nullptr;
                DWORD maxLength = 0;
                DWORD currentLength = 0;
                if (SUCCEEDED(buffer->Lock(&bytes, &maxLength, &currentLength))) {
                    if (currentLength >= width * height * 4) {
                        writeFrame->pixels = bytes;
                        writeFrame->pitch = width * 4;
                        publish();
                    }
                    buffer->Unlock();
                }
            }

            buffer->Release();
        }

        friend class CameraReaderCallback;

        IMFSourceReader* reader = nullptr;
        IMFMediaSource* mediaSource = nullptr;
        IKsControl* ksControl = nullptr;
        CameraReaderCallback* callback = nullptr;
        std::atomic<bool> running = false;
        std::atomic<bool> streaming = false;
        std::atomic<bool> samplePending = false;
        // Written on the MF callback thread (refreshMediaType) and read or reset
        // on the main thread (open, close, getters): atomic (application-owner
        // sign-off, 2026-11-04).
        std::atomic<bool> reselected = false;
        bool ownsCom = false;
        bool ownsMediaFoundation = false;
        std::atomic<size_t> width = 0;
        std::atomic<size_t> height = 0;
        Core::PixelFormat frameFormat = Core::PixelFormat::Null;
        std::string mediaFormatText = "unavailable (not open)";

        mutable std::mutex errorMutex;
        std::string errorText;
        CameraTypes::Frame* writeFrame = nullptr;
        std::function<CameraTypes::Frame*(CameraTypes::Frame*)> onFrame;
        std::function<void(std::string)> onOffline;
        std::function<void()> onDevicesChanged;
        HCMNOTIFICATION videoCameraNotification = nullptr;
        HCMNOTIFICATION captureNotification = nullptr;

        struct ControlDescriptor {
            CameraTypes::Setting::Type control;
            const char* name;
            const GUID* propertySet;
            ULONG propertyId;
            bool cameraControl;
        };

        static const std::vector<ControlDescriptor>& controlDescriptors() {
            static const std::vector<ControlDescriptor> descriptors = {
                { CameraTypes::Setting::Type::Exposure, "Exposure", &PROPSETID_VIDCAP_CAMERACONTROL, KSPROPERTY_CAMERACONTROL_EXPOSURE, true },
                { CameraTypes::Setting::Type::Focus, "Focus", &PROPSETID_VIDCAP_CAMERACONTROL, KSPROPERTY_CAMERACONTROL_FOCUS, true },
                { CameraTypes::Setting::Type::Iris, "Iris", &PROPSETID_VIDCAP_CAMERACONTROL, KSPROPERTY_CAMERACONTROL_IRIS, true },
                { CameraTypes::Setting::Type::Zoom, "Zoom", &PROPSETID_VIDCAP_CAMERACONTROL, KSPROPERTY_CAMERACONTROL_ZOOM, true },
                { CameraTypes::Setting::Type::Gain, "Gain", &PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_GAIN, false },
                { CameraTypes::Setting::Type::Gamma, "Gamma", &PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_GAMMA, false },
                { CameraTypes::Setting::Type::Brightness, "Brightness", &PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_BRIGHTNESS, false },
                { CameraTypes::Setting::Type::Contrast, "Contrast", &PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_CONTRAST, false },
                { CameraTypes::Setting::Type::Hue, "Hue", &PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_HUE, false },
                { CameraTypes::Setting::Type::Saturation, "Saturation", &PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_SATURATION, false },
                { CameraTypes::Setting::Type::Sharpness, "Sharpness", &PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_SHARPNESS, false },
                { CameraTypes::Setting::Type::WhiteBalance, "White balance", &PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_WHITEBALANCE, false },
                { CameraTypes::Setting::Type::BacklightCompensation, "Backlight compensation", &PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_BACKLIGHT_COMPENSATION, false }
            };
            return descriptors;
        }

        static const ControlDescriptor* descriptorFor(CameraTypes::Setting::Type control) {
            const auto& descriptors = controlDescriptors();
            auto found = std::find_if(
                descriptors.begin(), descriptors.end(),
                [control](const ControlDescriptor& descriptor) { return descriptor.control == control; }
            );
            return found == descriptors.end() ? nullptr : &*found;
        }

        bool queryRange(const ControlDescriptor& descriptor, CameraTypes::Setting& info) {
            KSPROPERTY property = { *descriptor.propertySet, descriptor.propertyId, KSPROPERTY_TYPE_BASICSUPPORT };
            std::vector<unsigned char> data(512);
            ULONG returned = 0;
            NTSTATUS status = ksControl->KsProperty(
                &property, sizeof(property), data.data(), static_cast<ULONG>(data.size()), &returned
            );
            if (status < 0 && returned > data.size()) {
                data.resize(returned);
                status = ksControl->KsProperty(
                    &property, sizeof(property), data.data(), static_cast<ULONG>(data.size()), &returned
                );
            }
            if (status < 0 || returned < sizeof(KSPROPERTY_DESCRIPTION)) { return false; }

            auto* description = reinterpret_cast<const KSPROPERTY_DESCRIPTION*>(data.data());
            size_t offset = sizeof(KSPROPERTY_DESCRIPTION);
            long defaultValue = info.val;
            bool foundRange = false;

            for (ULONG i = 0; i < description->MembersListCount; i++) {
                if (offset + sizeof(KSPROPERTY_MEMBERSHEADER) > returned) { break; }
                auto* header = reinterpret_cast<const KSPROPERTY_MEMBERSHEADER*>(data.data() + offset);
                offset += sizeof(KSPROPERTY_MEMBERSHEADER);
                size_t memberBytes = static_cast<size_t>(header->MembersSize) * header->MembersCount;
                if (offset + memberBytes > returned) { break; }

                if ((header->MembersFlags == KSPROPERTY_MEMBER_STEPPEDRANGES ||
                     header->MembersFlags == KSPROPERTY_MEMBER_RANGES) &&
                    header->MembersSize >= sizeof(KSPROPERTY_STEPPING_LONG) &&
                    header->MembersCount > 0) {
                    auto* range = reinterpret_cast<const KSPROPERTY_STEPPING_LONG*>(data.data() + offset);
                    info.min = range->Bounds.SignedMinimum;
                    info.max = range->Bounds.SignedMaximum;
                    info.step = static_cast<long>(range->SteppingDelta);
                    foundRange = true;
                }
                if ((header->Flags & KSPROPERTY_MEMBER_FLAG_DEFAULT) &&
                    header->MembersSize >= sizeof(LONG) && header->MembersCount > 0) {
                    defaultValue = *reinterpret_cast<const LONG*>(data.data() + offset);
                }
                offset += memberBytes;
            }

            info.def = defaultValue;
            return foundRange;
        }

        bool queryControl(const ControlDescriptor& descriptor, CameraTypes::Setting& info) {
            if (!ksControl) { return false; }
            info.control = descriptor.control;
            info.name = descriptor.name;
            ULONG returned = 0;

            if (descriptor.cameraControl) {
                KSPROPERTY_CAMERACONTROL_S request = {};
                request.Property = { *descriptor.propertySet, descriptor.propertyId, KSPROPERTY_TYPE_GET };
                NTSTATUS status = ksControl->KsProperty(
                    &request.Property, sizeof(request), &request, sizeof(request), &returned
                );
                if (status < 0) { return false; }
                info.val = request.Value;
                info.mode = (request.Flags & KSPROPERTY_CAMERACONTROL_FLAGS_AUTO)
                    ? CameraTypes::Setting::Mode::Automatic
                    : (request.Flags & KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL)
                        ? CameraTypes::Setting::Mode::Manual
                        : CameraTypes::Setting::Mode::Disabled;
                info.supportsManual = (request.Capabilities & KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL) != 0;
                info.supportsAutomatic = (request.Capabilities & KSPROPERTY_CAMERACONTROL_FLAGS_AUTO) != 0;
            }
            else {
                KSPROPERTY_VIDEOPROCAMP_S request = {};
                request.Property = { *descriptor.propertySet, descriptor.propertyId, KSPROPERTY_TYPE_GET };
                NTSTATUS status = ksControl->KsProperty(
                    &request.Property, sizeof(request), &request, sizeof(request), &returned
                );
                if (status < 0) { return false; }
                info.val = request.Value;
                info.mode = (request.Flags & KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO)
                    ? CameraTypes::Setting::Mode::Automatic
                    : (request.Flags & KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL)
                        ? CameraTypes::Setting::Mode::Manual
                        : CameraTypes::Setting::Mode::Disabled;
                info.supportsManual = (request.Capabilities & KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL) != 0;
                info.supportsAutomatic = (request.Capabilities & KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO) != 0;
            }

            return queryRange(descriptor, info);
        }

        bool writeControl(
            const ControlDescriptor& descriptor,
            long value,
            CameraTypes::Setting::Mode mode
        ) {
            ULONG returned = 0;
            if (descriptor.cameraControl) {
                KSPROPERTY_CAMERACONTROL_S request = {};
                request.Property = { *descriptor.propertySet, descriptor.propertyId, KSPROPERTY_TYPE_SET };
                request.Value = value;
                request.Flags = mode == CameraTypes::Setting::Mode::Automatic
                    ? KSPROPERTY_CAMERACONTROL_FLAGS_AUTO
                    : KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;
                return ksControl->KsProperty(
                    &request.Property, sizeof(request), &request, sizeof(request), &returned
                ) >= 0;
            }

            KSPROPERTY_VIDEOPROCAMP_S request = {};
            request.Property = { *descriptor.propertySet, descriptor.propertyId, KSPROPERTY_TYPE_SET };
            request.Value = value;
            request.Flags = mode == CameraTypes::Setting::Mode::Automatic
                ? KSPROPERTY_VIDEOPROCAMP_FLAGS_AUTO
                : KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL;
            return ksControl->KsProperty(
                &request.Property, sizeof(request), &request, sizeof(request), &returned
            ) >= 0;
        }

        bool copyPixels(std::vector<unsigned char>& target, const BYTE* firstRow, LONG pitch) {
            if (!firstRow || pitch == 0) { return false; }
            for (size_t y = 0; y < height; y++) {
                const BYTE* source = firstRow + static_cast<ptrdiff_t>(y) * pitch;
                unsigned char* row = target.data() + y * width * 4;
                std::memcpy(row, source, width * 4);
            }
            return true;
        }

        void setError(std::string value) {
            std::scoped_lock lock(errorMutex);
            errorText = std::move(value);
        }

        void clearError() {
            std::scoped_lock lock(errorMutex);
            errorText.clear();
        }

        void lost(std::string error) {
            setError(error);

            if (!running.exchange(false)) {
                return;
            }

            if (onOffline) {
                onOffline(std::move(error));
            }
        }

        void releaseRuntime() {
            if (ownsMediaFoundation) { MFShutdown(); ownsMediaFoundation = false; }
            if (ownsCom) { CoUninitialize(); ownsCom = false; }
        }
    };

    HRESULT CameraReaderCallback::QueryInterface(REFIID iid, void** object) {
        if (!object) { return E_POINTER; }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFSourceReaderCallback)) {
            *object = static_cast<IMFSourceReaderCallback*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG CameraReaderCallback::Release() {
        ULONG remaining = --references;
        if (remaining == 0) { delete this; }
        return remaining;
    }

    HRESULT CameraReaderCallback::OnReadSample(
        HRESULT status, DWORD, DWORD flags, LONGLONG timestamp, IMFSample* sample) {

        std::scoped_lock lock(ownerMutex);
        if (!owner) { return S_OK; }
        owner->samplePending = false;
        // receive() only copies into a snapshot; a failure costs one frame,
        // never the read loop, so the next ReadSample is always issued.
        try { owner->receive(status, flags, timestamp, sample); }
        catch (const std::exception& error) { dbg("[Camera] sample dropped: %s", error.what()); }
        catch (...) { dbg("[Camera] sample dropped: unknown error"); }
        if (owner->isOpen() && owner->isStreaming()) {
            HRESULT next = owner->requestNextSample();

            if (FAILED(next)) {
                owner->lost(NativeCamera::hresultMessage(
                    "Could not continue reading camera frames",
                    next
                ));
            }
        }
        return S_OK;
    }

    void CameraReaderCallback::detach() {
        std::scoped_lock lock(ownerMutex);
        owner = nullptr;
    }
}
