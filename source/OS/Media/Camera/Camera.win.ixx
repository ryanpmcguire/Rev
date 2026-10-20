module;

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
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

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

        explicit NativeCamera(std::function<CameraTypes::Frame*(CameraTypes::Frame*)> callback) {
            onFrame = callback;
            writeFrame = new CameraTypes::Frame;
        }
        NativeCamera(const NativeCamera&) = delete;
        NativeCamera& operator=(const NativeCamera&) = delete;
        ~NativeCamera() {
            close();
            delete writeFrame;
            writeFrame = nullptr;
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
            if (SUCCEEDED(result)) { result = MFCreateMediaType(&outputType); }
            if (SUCCEEDED(result)) { result = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); }
            if (SUCCEEDED(result)) { result = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32); }
            if (SUCCEEDED(result)) { result = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType); }

            IMFMediaType* actualType = nullptr;
            UINT32 frameWidth = 0;
            UINT32 frameHeight = 0;
            if (SUCCEEDED(result)) { result = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType); }
            if (SUCCEEDED(result)) { result = MFGetAttributeSize(actualType, MF_MT_FRAME_SIZE, &frameWidth, &frameHeight); }

            width = frameWidth;
            height = frameHeight;
            frameFormat = Core::PixelFormat::BGRX8;

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
            result = requestNextSample();
            if (FAILED(result)) {
                setError(hresultMessage("Could not begin reading camera frames", result));
                close();
                return false;
            }
            return true;
        }

        void close() {
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
            releaseRuntime();
        }

        bool isOpen() const { return running && reader; }

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
            if (!running || !reader) { return MF_E_SHUTDOWN; }
            return reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
        }

        void receive(HRESULT status, DWORD flags, LONGLONG timestamp, IMFSample* sample) {

            if (FAILED(status)) {
                setError(hresultMessage("Camera frame read failed", status));
                running = false;
                return;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                setError("Camera stream ended");
                running = false;
                return;
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
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) { return; }

            writeFrame->data.resize(width * height * 4);
            bool converted = false;
            IMF2DBuffer* buffer2d = nullptr;

            if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d)))) {
                BYTE* scanline = nullptr;
                LONG pitch = 0;
                if (SUCCEEDED(buffer2d->Lock2D(&scanline, &pitch))) {
                    converted = copyPixels(writeFrame->data, scanline, pitch);
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
                        converted = copyPixels(
                            writeFrame->data,
                            bytes,
                            static_cast<LONG>(width * 4)
                        );
                    }
                    buffer->Unlock();
                }
            }

            buffer->Release();
            if (!converted) { return; }

            writeFrame->size = { width, height, 4 };
            writeFrame->format = frameFormat;
            writeFrame->timestamp = {
                .capturedAtMs = capturedAtMs,
                .receivedAtMs = receivedAtMs,
                .timestamp100ns = static_cast<uint64_t>(timestamp)
            };
            if (onFrame) { writeFrame = onFrame(writeFrame); }
        }

        friend class CameraReaderCallback;

        IMFSourceReader* reader = nullptr;
        IMFMediaSource* mediaSource = nullptr;
        IKsControl* ksControl = nullptr;
        CameraReaderCallback* callback = nullptr;
        std::atomic<bool> running = false;
        bool ownsCom = false;
        bool ownsMediaFoundation = false;
        size_t width = 0;
        size_t height = 0;
        Core::PixelFormat frameFormat = Core::PixelFormat::Null;

        mutable std::mutex errorMutex;
        std::string errorText;
        CameraTypes::Frame* writeFrame = nullptr;
        std::function<CameraTypes::Frame*(CameraTypes::Frame*)> onFrame;

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
        owner->receive(status, flags, timestamp, sample);
        if (owner->isOpen()) { owner->requestNextSample(); }
        return S_OK;
    }

    void CameraReaderCallback::detach() {
        std::scoped_lock lock(ownerMutex);
        owner = nullptr;
    }
}
