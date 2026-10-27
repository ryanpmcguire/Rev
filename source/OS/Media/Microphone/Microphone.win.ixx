module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

export module Rev.Media.NativeMicrophone;

import Rev.Core.Thread;

export namespace Rev::Media {

    struct NativeMicrophone {
        static constexpr uint32_t processedSampleRate = 1000;
        static constexpr size_t processedBlockSize = 20;
        static constexpr double levelLowPassHz = 12.0;
        static constexpr uint32_t pitchSampleRate = 8000;
        static constexpr size_t pitchWindowSize = 320;
        static constexpr size_t pitchHopSize = 160;

        struct Device {
            std::wstring id;
            std::wstring name;
            bool isDefault = false;
        };

        using Callback = std::function<void(
            std::vector<float>&&, uint32_t, uint16_t, double, float, float, float
        )>;

        explicit NativeMicrophone(Callback callback)
            : callback(std::move(callback)) {}
        ~NativeMicrophone() { close(); }
        NativeMicrophone(const NativeMicrophone&) = delete;
        NativeMicrophone& operator=(const NativeMicrophone&) = delete;

        static std::vector<Device> devices() {
            std::vector<Device> result;
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const bool ownsCom = SUCCEEDED(initialized);
            IMMDeviceEnumerator* enumerator = nullptr;
            IMMDeviceCollection* collection = nullptr;
            IMMDevice* defaultDevice = nullptr;
            LPWSTR defaultId = nullptr;
            UINT count = 0;
            if (FAILED(CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)
            ))) { goto cleanup; }
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(
                eCapture, eConsole, &defaultDevice
            ))) { defaultDevice->GetId(&defaultId); }
            if (FAILED(enumerator->EnumAudioEndpoints(
                eCapture, DEVICE_STATE_ACTIVE, &collection
            ))) { goto cleanup; }
            collection->GetCount(&count);
            for (UINT index = 0; index < count; ++index) {
                IMMDevice* device = nullptr;
                IPropertyStore* properties = nullptr;
                LPWSTR id = nullptr;
                PROPVARIANT name;
                PropVariantInit(&name);
                if (SUCCEEDED(collection->Item(index, &device))) {
                    device->GetId(&id);
                    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
                        properties->GetValue(PKEY_Device_FriendlyName, &name);
                    }
                    Device item;
                    if (id) { item.id = id; }
                    if (name.vt == VT_LPWSTR && name.pwszVal) {
                        item.name = name.pwszVal;
                    }
                    item.isDefault = defaultId && id && item.id == defaultId;
                    result.push_back(std::move(item));
                }
                PropVariantClear(&name);
                if (id) { CoTaskMemFree(id); }
                release(properties);
                release(device);
            }
        cleanup:
            if (defaultId) { CoTaskMemFree(defaultId); }
            release(defaultDevice);
            release(collection);
            release(enumerator);
            if (ownsCom) { CoUninitialize(); }
            return result;
        }

        bool open(size_t deviceIndex = 0) {
            close();
            {
                std::scoped_lock lock(errorMutex);
                errorText.clear();
                diagnosticText.clear();
            }
            packetCount = 0;
            silentPacketCount = 0;
            byteCount = 0;
            nonzeroByteCount = 0;
            deviceName = "Unknown input";
            endpointState = "endpoint state unavailable";
            running = true;
            worker.start([this, deviceIndex](std::stop_token stop) {
                capture(stop, deviceIndex);
            });
            bool ready = false;
            {
                std::unique_lock lock(stateMutex);
                ready = stateChanged.wait_for(
                    lock, std::chrono::milliseconds(1500),
                    [this]() { return opened.load() || !running.load(); }
                ) && opened.load();
            }
            if (ready) { return true; }
            if (running.load()) {
                fail("Timed out while opening the default microphone.");
            }
            close();
            return false;
        }

        void close() {
            running = false;
            worker.stop();
            opened = false;
        }

        bool isOpen() const { return opened.load(); }
        std::string error() const {
            std::scoped_lock lock(errorMutex);
            return errorText;
        }
        std::string diagnostics() const {
            std::scoped_lock lock(errorMutex);
            std::ostringstream text;
            text << deviceName << ", " << endpointState << ", "
                 << diagnosticText << ", packets "
                 << packetCount.load(std::memory_order_relaxed) << " total/"
                 << silentPacketCount.load(std::memory_order_relaxed) << " silent, raw "
                 << nonzeroByteCount.load(std::memory_order_relaxed) << "/"
                 << byteCount.load(std::memory_order_relaxed) << " nonzero bytes";
            return text.str();
        }

    private:
        template<typename T>
        static void release(T*& value) {
            if (value) { value->Release(); value = nullptr; }
        }

        void fail(std::string message) {
            std::scoped_lock lock(errorMutex);
            errorText = std::move(message);
        }

        void setDiagnostics(const WAVEFORMATEX& format, WORD tag, WORD validBits) {
            std::ostringstream text;
            text << format.nSamplesPerSec << " Hz, " << format.nChannels
                 << (format.nChannels == 1 ? " channel, " : " channels, ")
                 << (tag == WAVE_FORMAT_IEEE_FLOAT ? "float" : "PCM")
                 << ", " << format.wBitsPerSample << " container bits";
            if (validBits != format.wBitsPerSample) {
                text << ", " << validBits << " valid bits";
            }
            std::scoped_lock lock(errorMutex);
            diagnosticText = text.str();
        }

        static float sample(const BYTE* bytes, size_t index, WORD tag, WORD bits) {
            const BYTE* value = bytes + index * (bits / 8);
            if (tag == WAVE_FORMAT_IEEE_FLOAT && bits == 32) {
                float result = 0.0f;
                std::memcpy(&result, value, sizeof(result));
                return result;
            }
            if (tag == WAVE_FORMAT_IEEE_FLOAT && bits == 64) {
                double result = 0.0;
                std::memcpy(&result, value, sizeof(result));
                return static_cast<float>(result);
            }
            if (tag != WAVE_FORMAT_PCM) { return 0.0f; }
            if (bits == 8) { return (static_cast<float>(*value) - 128.0f) / 128.0f; }
            if (bits == 16) {
                int16_t result = 0;
                std::memcpy(&result, value, sizeof(result));
                return static_cast<float>(result) / 32768.0f;
            }
            if (bits == 24) {
                int32_t result = value[0] | (value[1] << 8) | (value[2] << 16);
                if (result & 0x800000) { result |= ~0xffffff; }
                return static_cast<float>(result) / 8388608.0f;
            }
            if (bits == 32) {
                int32_t result = 0;
                std::memcpy(&result, value, sizeof(result));
                return static_cast<float>(result) / 2147483648.0f;
            }
            return 0.0f;
        }

        static float estimateFundamental(const std::vector<float>& samples) {
            if (samples.size() < pitchWindowSize) { return 0.0f; }
            double mean = 0.0;
            for (float value : samples) { mean += value; }
            mean /= samples.size();
            double energy = 0.0;
            for (float value : samples) {
                const double centered = value - mean;
                energy += centered * centered;
            }
            if (energy / samples.size() < 1.0e-7) { return 0.0f; }

            constexpr size_t minimumLag = pitchSampleRate / 1000;
            constexpr size_t maximumLag = pitchSampleRate / 80;
            float bestCorrelation = 0.0f;
            size_t bestLag = 0;
            for (size_t lag = minimumLag; lag <= maximumLag; ++lag) {
                double correlation = 0.0;
                double leftEnergy = 0.0;
                double rightEnergy = 0.0;
                for (size_t index = lag; index < samples.size(); ++index) {
                    const double left = samples[index] - mean;
                    const double right = samples[index - lag] - mean;
                    correlation += left * right;
                    leftEnergy += left * left;
                    rightEnergy += right * right;
                }
                const double denominator = std::sqrt(leftEnergy * rightEnergy);
                const float normalized = denominator > 0.0
                    ? static_cast<float>(correlation / denominator) : 0.0f;
                if (normalized > bestCorrelation) {
                    bestCorrelation = normalized;
                    bestLag = lag;
                }
            }
            return bestCorrelation >= 0.65f && bestLag
                ? static_cast<float>(pitchSampleRate) / bestLag : 0.0f;
        }

        void capture(std::stop_token stop, size_t deviceIndex) {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const bool ownsCom = SUCCEEDED(initialized);
            IMMDeviceEnumerator* enumerator = nullptr;
            IMMDeviceCollection* collection = nullptr;
            IMMDevice* device = nullptr;
            IPropertyStore* deviceProperties = nullptr;
            IAudioClient* client = nullptr;
            IAudioCaptureClient* captureClient = nullptr;
            IAudioEndpointVolume* endpointVolume = nullptr;
            WAVEFORMATEX* format = nullptr;
            WORD sampleTag = 0;
            WORD validBits = 0;
            HANDLE event = nullptr;
            // These accumulators live only on the capture worker. Each output
            // sample is the RMS energy of approximately one millisecond of the
            // device-rate, interleaved input. No native-rate buffer leaves this
            // function.
            uint64_t resamplePhase = 0;
            uint64_t pitchPhase = 0;
            double binSquareSum = 0.0;
            size_t binValueCount = 0;
            float filteredLevel = 0.0f;
            bool filterInitialized = false;
            const float filterAmount = static_cast<float>(
                1.0 - std::exp(
                    -2.0 * 3.14159265358979323846 * levelLowPassHz /
                    static_cast<double>(processedSampleRate)
                )
            );
            std::vector<float> processed;
            processed.reserve(processedBlockSize * 2);
            std::vector<float> pitchWindow;
            pitchWindow.reserve(pitchWindowSize + pitchHopSize);
            float fundamentalHz = 0.0f;

            if (FAILED(CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)
            ))) { fail("Could not create the audio device enumerator."); goto cleanup; }
            if (deviceIndex == 0) {
                if (FAILED(enumerator->GetDefaultAudioEndpoint(
                    eCapture, eConsole, &device
                ))) { fail("No default microphone is available."); goto cleanup; }
            }
            else {
                if (FAILED(enumerator->EnumAudioEndpoints(
                    eCapture, DEVICE_STATE_ACTIVE, &collection
                ))) { fail("Could not enumerate microphones."); goto cleanup; }
                if (FAILED(collection->Item(static_cast<UINT>(deviceIndex), &device))) {
                    fail("The selected microphone is unavailable."); goto cleanup;
                }
            }
            {
                PROPVARIANT friendlyName;
                PropVariantInit(&friendlyName);
                if (SUCCEEDED(device->OpenPropertyStore(
                        STGM_READ, &deviceProperties
                    )) && SUCCEEDED(deviceProperties->GetValue(
                        PKEY_Device_FriendlyName, &friendlyName
                    )) && friendlyName.vt == VT_LPWSTR && friendlyName.pwszVal) {
                    const int length = WideCharToMultiByte(
                        CP_UTF8, 0, friendlyName.pwszVal, -1,
                        nullptr, 0, nullptr, nullptr
                    );
                    if (length > 1) {
                        std::string converted(static_cast<size_t>(length), '\0');
                        WideCharToMultiByte(
                            CP_UTF8, 0, friendlyName.pwszVal, -1,
                            converted.data(), length, nullptr, nullptr
                        );
                        converted.pop_back();
                        std::scoped_lock lock(errorMutex);
                        deviceName = std::move(converted);
                    }
                }
                PropVariantClear(&friendlyName);
            }
            if (SUCCEEDED(device->Activate(
                    __uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                    reinterpret_cast<void**>(&endpointVolume)
                ))) {
                BOOL muted = FALSE;
                float volume = 0.0f;
                endpointVolume->GetMute(&muted);
                endpointVolume->GetMasterVolumeLevelScalar(&volume);
                std::scoped_lock lock(errorMutex);
                endpointState = muted ? "muted" : "unmuted";
                endpointState += ", gain " +
                    std::to_string(static_cast<int>(std::round(volume * 100.0f))) +
                    "%";
            }
            if (FAILED(device->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(&client)
            ))) { fail("Could not open the microphone."); goto cleanup; }
            if (FAILED(client->GetMixFormat(&format))) {
                fail("Could not read the microphone format."); goto cleanup;
            }
            sampleTag = format->wFormatTag;
            validBits = format->wBitsPerSample;
            if (sampleTag == WAVE_FORMAT_EXTENSIBLE) {
                const auto& extended =
                    reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(*format);
                validBits = extended.Samples.wValidBitsPerSample;
                if (extended.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                    sampleTag = WAVE_FORMAT_IEEE_FLOAT;
                }
                else if (extended.SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
                    sampleTag = WAVE_FORMAT_PCM;
                }
                else {
                    fail("The microphone uses an unsupported sample subtype.");
                    goto cleanup;
                }
            }
            if (sampleTag != WAVE_FORMAT_PCM &&
                sampleTag != WAVE_FORMAT_IEEE_FLOAT) {
                fail("The microphone uses an unsupported sample format.");
                goto cleanup;
            }
            setDiagnostics(*format, sampleTag, validBits);
            event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!event) { fail("Could not create the microphone event."); goto cleanup; }
            if (FAILED(client->Initialize(
                AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                0, 0, format, nullptr
            )) || FAILED(client->SetEventHandle(event)) || FAILED(client->GetService(
                __uuidof(IAudioCaptureClient),
                reinterpret_cast<void**>(&captureClient)
            )) || FAILED(client->Start())) {
                fail("Could not start microphone capture."); goto cleanup;
            }
            opened = true;
            stateChanged.notify_all();
            while (running && !stop.stop_requested()) {
                if (WaitForSingleObject(event, 100) != WAIT_OBJECT_0) { continue; }
                UINT32 packetFrames = 0;
                HRESULT packetResult = captureClient->GetNextPacketSize(&packetFrames);
                if (FAILED(packetResult)) {
                    fail("Microphone capture failed while checking the next packet.");
                    running = false;
                    break;
                }
                while (packetFrames) {
                    BYTE* bytes = nullptr;
                    UINT32 frames = 0;
                    DWORD flags = 0;
                    if (FAILED(captureClient->GetBuffer(
                        &bytes, &frames, &flags, nullptr, nullptr
                    ))) {
                        fail("Microphone capture failed while reading a packet.");
                        running = false;
                        break;
                    }
                    packetCount.fetch_add(1, std::memory_order_relaxed);
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        silentPacketCount.fetch_add(1, std::memory_order_relaxed);
                    }
                    const size_t packetBytes = static_cast<size_t>(frames) *
                        format->nBlockAlign;
                    byteCount.fetch_add(packetBytes, std::memory_order_relaxed);
                    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && bytes) {
                        size_t nonzero = 0;
                        for (size_t index = 0; index < packetBytes; ++index) {
                            nonzero += bytes[index] != 0;
                        }
                        nonzeroByteCount.fetch_add(nonzero, std::memory_order_relaxed);
                    }
                    for (size_t frame = 0; frame < frames; ++frame) {
                        double frameSquareSum = 0.0;
                        double frameSum = 0.0;
                        for (size_t channel = 0;
                             channel < format->nChannels; ++channel) {
                            float value = 0.0f;
                            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                                value = std::clamp(
                                    sample(
                                        bytes,
                                        frame * format->nChannels + channel,
                                        sampleTag, format->wBitsPerSample
                                    ),
                                    -1.0f, 1.0f
                                );
                            }
                            frameSum += value;
                            frameSquareSum += static_cast<double>(value) * value;
                        }
                        pitchPhase += pitchSampleRate;
                        if (pitchPhase >= format->nSamplesPerSec) {
                            pitchPhase -= format->nSamplesPerSec;
                            pitchWindow.push_back(static_cast<float>(
                                frameSum / std::max<WORD>(format->nChannels, 1)
                            ));
                            if (pitchWindow.size() >= pitchWindowSize) {
                                fundamentalHz = estimateFundamental(pitchWindow);
                                pitchWindow.erase(
                                    pitchWindow.begin(),
                                    pitchWindow.begin() + pitchHopSize
                                );
                            }
                        }
                        binSquareSum += frameSquareSum /
                            std::max<WORD>(format->nChannels, 1);
                        ++binValueCount;
                        resamplePhase += processedSampleRate;
                        if (resamplePhase < format->nSamplesPerSec) { continue; }
                        resamplePhase -= format->nSamplesPerSec;
                        const float rawLevel = binValueCount
                            ? static_cast<float>(std::sqrt(
                                binSquareSum / binValueCount
                            ))
                            : 0.0f;
                        if (!filterInitialized) {
                            filteredLevel = rawLevel;
                            filterInitialized = true;
                        }
                        else {
                            filteredLevel += filterAmount *
                                (rawLevel - filteredLevel);
                        }
                        processed.push_back(filteredLevel);
                        binSquareSum = 0.0;
                        binValueCount = 0;
                    }
                    if (callback && processed.size() >= processedBlockSize) {
                        double processedSquareSum = 0.0;
                        float processedPeak = 0.0f;
                        for (float value : processed) {
                            processedSquareSum +=
                                static_cast<double>(value) * value;
                            processedPeak = std::max(processedPeak, value);
                        }
                        const float processedRms = static_cast<float>(std::sqrt(
                            processedSquareSum / processed.size()
                        ));
                        const double now = std::chrono::duration<double>(
                            std::chrono::steady_clock::now().time_since_epoch()
                        ).count();
                        callback(
                            std::move(processed), processedSampleRate, 1,
                            now, processedRms, processedPeak, fundamentalHz
                        );
                        processed.clear();
                        processed.reserve(processedBlockSize * 2);
                    }
                    captureClient->ReleaseBuffer(frames);
                    packetResult = captureClient->GetNextPacketSize(&packetFrames);
                    if (FAILED(packetResult)) {
                        fail("Microphone capture failed while checking the next packet.");
                        running = false;
                        break;
                    }
                }
            }
            client->Stop();
        cleanup:
            opened = false;
            running = false;
            stateChanged.notify_all();
            if (event) { CloseHandle(event); }
            if (format) { CoTaskMemFree(format); }
            release(captureClient);
            release(client);
            release(endpointVolume);
            release(deviceProperties);
            release(device);
            release(collection);
            release(enumerator);
            if (ownsCom) { CoUninitialize(); }
        }

        Callback callback;
        Core::Thread worker;
        std::atomic<bool> running = false;
        std::atomic<bool> opened = false;
        std::mutex stateMutex;
        std::condition_variable stateChanged;
        mutable std::mutex errorMutex;
        std::string errorText;
        std::string diagnosticText;
        std::string deviceName = "Unknown input";
        std::string endpointState = "endpoint state unavailable";
        std::atomic<uint64_t> packetCount = 0;
        std::atomic<uint64_t> silentPacketCount = 0;
        std::atomic<uint64_t> byteCount = 0;
        std::atomic<uint64_t> nonzeroByteCount = 0;
    };
}
