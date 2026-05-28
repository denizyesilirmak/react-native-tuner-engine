#pragma once

#include <oboe/Oboe.h>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>

// Oboe-based microphone capture.
// The onSamples callback is invoked on the Oboe real-time audio thread —
// must not block, lock, or allocate.
class OboeAudioSource : public oboe::AudioStreamCallback,
                        public std::enable_shared_from_this<OboeAudioSource> {
public:
    using SamplesCallback = std::function<void(const float* samples, int count, float sampleRate)>;

    explicit OboeAudioSource(SamplesCallback callback);
    ~OboeAudioSource();

    bool start();
    void stop();
    float sampleRate() const;

    // oboe::AudioStreamCallback
    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream* stream,
        void* audioData,
        int32_t numFrames
    ) override;

    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    bool openStream();

    SamplesCallback callback_;
    std::shared_ptr<oboe::AudioStream> stream_;
    float sampleRate_{48000.0f};
    std::atomic<bool> stopped_{false};
    std::mutex restartMutex_;
    std::thread restartThread_;
};
