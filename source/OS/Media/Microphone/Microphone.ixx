module;

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

export module Rev.Media.Microphone;

import Rev.Media.NativeMicrophone;

export namespace Rev::Media {

    struct Microphone {
        using Device = NativeMicrophone::Device;

        struct Block {
            // Processed mono analysis data. Native device-rate samples never
            // cross the microphone worker boundary.
            std::vector<float> samples;
            uint32_t sampleRate = 1000;
            uint16_t channels = 1;
            double capturedAtSeconds = 0.0;
            float rms = 0.0f;
            float peak = 0.0f;
            float fundamentalHz = 0.0f;
        };

        using Callback = std::function<void(const Block&)>;

        NativeMicrophone native;
        std::atomic<float> currentRms = 0.0f;
        std::atomic<float> currentPeak = 0.0f;
        std::atomic<float> currentMin = 0.0f;
        std::atomic<float> currentMax = 0.0f;
        std::atomic<float> currentFundamentalHz = 0.0f;
        mutable std::mutex callbackMutex;
        Callback callback;
        mutable std::mutex samplesMutex;
        std::vector<float> pendingSamples;
        std::atomic<uint64_t> sampleCounter = 0;

        Microphone();
        Microphone(const Microphone&) = delete;
        Microphone& operator=(const Microphone&) = delete;
        ~Microphone();

        static std::vector<Device> devices();
        bool open(size_t deviceIndex = 0);
        void close();
        bool isOpen() const;
        std::string error() const;
        std::string diagnostics() const;
        float rms() const;
        float peak() const;
        float processedMin() const;
        float processedMax() const;
        float fundamentalHz() const;
        std::vector<float> takeSamples();
        uint64_t processedSampleCount() const;
        void onAudio(Callback listener);
    };

    Microphone::Microphone() : native([this](
            std::vector<float>&& samples,
            uint32_t sampleRate,
            uint16_t channels,
            double capturedAtSeconds,
            float rms,
            float peak,
            float fundamentalHz
        ) {
            currentRms.store(rms, std::memory_order_relaxed);
            currentPeak.store(peak, std::memory_order_relaxed);
            currentFundamentalHz.store(fundamentalHz, std::memory_order_relaxed);
            if (!samples.empty()) {
                const auto range = std::minmax_element(samples.begin(), samples.end());
                currentMin.store(*range.first, std::memory_order_relaxed);
                currentMax.store(*range.second, std::memory_order_relaxed);
            }
            {
                std::scoped_lock lock(samplesMutex);
                pendingSamples.insert(
                    pendingSamples.end(), samples.begin(), samples.end()
                );
                // Bound unattended consumers to ten seconds of analysis data.
                if (pendingSamples.size() > 10'000) {
                    pendingSamples.erase(
                        pendingSamples.begin(),
                        pendingSamples.end() - 10'000
                    );
                }
            }
            sampleCounter.fetch_add(samples.size(), std::memory_order_release);
            Callback listener;
            {
                std::scoped_lock lock(callbackMutex);
                listener = callback;
            }
            if (listener) {
                listener(Block{
                    .samples = std::move(samples),
                    .sampleRate = sampleRate,
                    .channels = channels,
                    .capturedAtSeconds = capturedAtSeconds,
                    .rms = rms,
                    .peak = peak,
                    .fundamentalHz = fundamentalHz
                });
            }
        }) {}

    Microphone::~Microphone() { close(); }

    std::vector<Microphone::Device> Microphone::devices() {
        return NativeMicrophone::devices();
    }

    bool Microphone::open(size_t deviceIndex) {
        return native.open(deviceIndex);
    }

    void Microphone::close() {
        native.close();
        currentRms = 0.0f;
        currentPeak = 0.0f;
        currentMin = 0.0f;
        currentMax = 0.0f;
        currentFundamentalHz = 0.0f;
    }

    bool Microphone::isOpen() const { return native.isOpen(); }
    std::string Microphone::error() const { return native.error(); }
    std::string Microphone::diagnostics() const { return native.diagnostics(); }
    float Microphone::rms() const {
        return currentRms.load(std::memory_order_relaxed);
    }
    float Microphone::peak() const {
        return currentPeak.load(std::memory_order_relaxed);
    }
    float Microphone::processedMin() const {
        return currentMin.load(std::memory_order_relaxed);
    }
    float Microphone::processedMax() const {
        return currentMax.load(std::memory_order_relaxed);
    }
    float Microphone::fundamentalHz() const {
        return currentFundamentalHz.load(std::memory_order_relaxed);
    }

    std::vector<float> Microphone::takeSamples() {
        std::vector<float> result;
        std::scoped_lock lock(samplesMutex);
        result.swap(pendingSamples);
        return result;
    }

    uint64_t Microphone::processedSampleCount() const {
        return sampleCounter.load(std::memory_order_acquire);
    }

    void Microphone::onAudio(Callback listener) {
        std::scoped_lock lock(callbackMutex);
        callback = std::move(listener);
    }
}
