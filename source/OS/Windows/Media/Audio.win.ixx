module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

export module Rev.OS.Media.Audio;

export namespace Rev::OS::Media::Audio {

    enum class SampleType {
        Unknown,
        Unsigned8,
        Signed16,
        Signed24,
        Signed32,
        Float32,
        Float64
    };

    struct Format {
        uint32_t sampleRate = 0;
        uint16_t channels = 0;
        uint16_t bitsPerSample = 0;
        uint16_t blockAlign = 0;
        SampleType sampleType = SampleType::Unknown;
    };

    struct Device {
        std::wstring id;
        std::wstring name;
        bool isDefault = false;
    };

    struct ComScope {
        HRESULT result = E_FAIL;
        bool ownsInitialization = false;

        ComScope() {
            result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ownsInitialization = SUCCEEDED(result);
            if (result == RPC_E_CHANGED_MODE) { result = S_OK; }
        }

        ~ComScope() {
            if (ownsInitialization) { CoUninitialize(); }
        }

        explicit operator bool() const { return SUCCEEDED(result); }
    };

    template<typename T>
    void release(T*& value) {
        if (value) {
            value->Release();
            value = nullptr;
        }
    }

    struct Devices {
        static std::vector<Device> outputs() {
            std::vector<Device> result;
            ComScope com;
            if (!com) { return result; }

            IMMDeviceEnumerator* enumerator = nullptr;
            IMMDeviceCollection* collection = nullptr;
            IMMDevice* defaultDevice = nullptr;
            LPWSTR defaultId = nullptr;

            if (FAILED(CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                __uuidof(IMMDeviceEnumerator),
                reinterpret_cast<void**>(&enumerator)
            ))) {
                return result;
            }

            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(
                eRender, eConsole, &defaultDevice
            ))) {
                defaultDevice->GetId(&defaultId);
            }

            if (SUCCEEDED(enumerator->EnumAudioEndpoints(
                eRender, DEVICE_STATE_ACTIVE, &collection
            ))) {
                UINT count = 0;
                collection->GetCount(&count);
                result.reserve(count);

                for (UINT index = 0; index < count; ++index) {
                    IMMDevice* device = nullptr;
                    IPropertyStore* properties = nullptr;
                    LPWSTR id = nullptr;
                    PROPVARIANT name;
                    PropVariantInit(&name);

                    if (SUCCEEDED(collection->Item(index, &device))) {
                        device->GetId(&id);
                        if (SUCCEEDED(device->OpenPropertyStore(
                            STGM_READ, &properties
                        ))) {
                            properties->GetValue(PKEY_Device_FriendlyName, &name);
                        }

                        Device description;
                        if (id) { description.id = id; }
                        if (name.vt == VT_LPWSTR && name.pwszVal) {
                            description.name = name.pwszVal;
                        }
                        description.isDefault = defaultId && id &&
                            description.id == defaultId;
                        result.push_back(std::move(description));
                    }

                    PropVariantClear(&name);
                    if (id) { CoTaskMemFree(id); }
                    release(properties);
                    release(device);
                }
            }

            if (defaultId) { CoTaskMemFree(defaultId); }
            release(defaultDevice);
            release(collection);
            release(enumerator);
            return result;
        }
    };

    struct OutputStream {
        using Callback = std::function<void(
            std::byte* buffer,
            uint32_t frames,
            const Format& format
        )>;

        std::jthread worker;
        std::atomic<bool> running = false;
        std::atomic<bool> opened = false;
        Format format;

        ~OutputStream() { stop(); }

        bool start(Callback callback = {}) {
            if (running.exchange(true)) { return false; }
            worker = std::jthread(
                [this, callback = std::move(callback)](std::stop_token token) {
                    run(token, callback);
                }
            );
            return true;
        }

        void stop() {
            running = false;
            if (worker.joinable()) {
                worker.request_stop();
                worker.join();
            }
            opened = false;
        }

        static SampleType sampleType(const WAVEFORMATEX& wave) {
            WORD tag = wave.wFormatTag;
            GUID subtype{};
            if (tag == WAVE_FORMAT_EXTENSIBLE &&
                wave.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
            ) {
                const auto& extended =
                    reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(wave);
                subtype = extended.SubFormat;
                if (subtype == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                    tag = WAVE_FORMAT_IEEE_FLOAT;
                }
                else if (subtype == KSDATAFORMAT_SUBTYPE_PCM) {
                    tag = WAVE_FORMAT_PCM;
                }
            }
            if (tag == WAVE_FORMAT_IEEE_FLOAT) {
                return wave.wBitsPerSample == 64
                    ? SampleType::Float64
                    : SampleType::Float32;
            }
            if (tag != WAVE_FORMAT_PCM) { return SampleType::Unknown; }
            switch (wave.wBitsPerSample) {
                case 8: return SampleType::Unsigned8;
                case 16: return SampleType::Signed16;
                case 24: return SampleType::Signed24;
                case 32: return SampleType::Signed32;
                default: return SampleType::Unknown;
            }
        }

        void run(std::stop_token token, const Callback& callback) {
            ComScope com;
            if (!com) { running = false; return; }

            IMMDeviceEnumerator* enumerator = nullptr;
            IMMDevice* device = nullptr;
            IAudioClient* client = nullptr;
            IAudioRenderClient* renderer = nullptr;
            WAVEFORMATEX* wave = nullptr;
            HANDLE event = nullptr;

            if (FAILED(CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                __uuidof(IMMDeviceEnumerator),
                reinterpret_cast<void**>(&enumerator)
            ))) { goto cleanup; }
            if (FAILED(enumerator->GetDefaultAudioEndpoint(
                eRender, eConsole, &device
            ))) { goto cleanup; }
            if (FAILED(device->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(&client)
            ))) { goto cleanup; }
            if (FAILED(client->GetMixFormat(&wave))) { goto cleanup; }

            format = {
                .sampleRate = wave->nSamplesPerSec,
                .channels = wave->nChannels,
                .bitsPerSample = wave->wBitsPerSample,
                .blockAlign = wave->nBlockAlign,
                .sampleType = sampleType(*wave)
            };

            event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!event) { goto cleanup; }
            if (FAILED(client->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                0, 0, wave, nullptr
            ))) { goto cleanup; }
            if (FAILED(client->SetEventHandle(event))) { goto cleanup; }
            if (FAILED(client->GetService(
                __uuidof(IAudioRenderClient),
                reinterpret_cast<void**>(&renderer)
            ))) { goto cleanup; }
            if (FAILED(client->Start())) { goto cleanup; }

            opened = true;
            while (running && !token.stop_requested()) {
                if (WaitForSingleObject(event, 100) != WAIT_OBJECT_0) { continue; }

                UINT32 capacity = 0;
                UINT32 padding = 0;
                if (FAILED(client->GetBufferSize(&capacity)) ||
                    FAILED(client->GetCurrentPadding(&padding))
                ) { break; }
                const UINT32 available = capacity - padding;
                if (!available) { continue; }

                BYTE* bytes = nullptr;
                if (FAILED(renderer->GetBuffer(available, &bytes))) { break; }
                std::memset(
                    bytes,
                    format.sampleType == SampleType::Unsigned8 ? 128 : 0,
                    available * format.blockAlign
                );
                if (callback) {
                    callback(
                        reinterpret_cast<std::byte*>(bytes),
                        available,
                        format
                    );
                }
                if (FAILED(renderer->ReleaseBuffer(available, 0))) { break; }
            }

            client->Stop();

        cleanup:
            opened = false;
            running = false;
            if (event) { CloseHandle(event); }
            if (wave) { CoTaskMemFree(wave); }
            release(renderer);
            release(client);
            release(device);
            release(enumerator);
        }
    };
}
