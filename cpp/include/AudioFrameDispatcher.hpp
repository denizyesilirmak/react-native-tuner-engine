#pragma once

#include "PitchResult.hpp"
#include "PostProcessor.hpp"
#include "RingBuffer.hpp"
#include "TunerEngine.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Bridges the real-time audio thread and the pitch-detection worker thread.
//
// The audio thread calls push() with raw PCM chunks — no locks, no allocation.
// An internal worker thread drains the ring into fixed-size frames (with optional
// overlap via a sliding window), runs TunerEngine::process(), and delivers each
// PitchResult via the callback.
class AudioFrameDispatcher {
public:
    using PitchCallback = std::function<void(const PitchResult&)>;

    // frameSize: samples per processing frame (e.g. 2048)
    // sampleRate: initial sample rate — can change via setSampleRate()
    // callback: invoked from the worker thread; must be thread-safe w.r.t. the caller
    // overlapRatio: fraction of frame that overlaps with previous (0.0–0.75). Default: 0.0
    AudioFrameDispatcher(int frameSize, float sampleRate, PitchCallback callback,
                         float overlapRatio = 0.0f);
    ~AudioFrameDispatcher();

    AudioFrameDispatcher(const AudioFrameDispatcher&) = delete;
    AudioFrameDispatcher& operator=(const AudioFrameDispatcher&) = delete;

    void start();
    void stop();

    // Push PCM samples from the audio thread. No blocking, no allocation.
    void push(const float* samples, int count);

    void setSampleRate(float sampleRate);
    void setA4(float hz);
    void setNoiseGateDb(float db);
    void setConfidenceThreshold(float value);
    void setFrequencyRange(float minHz, float maxHz);
    void setInstrument(const std::string& name);
    void setTuning(const std::string& name);
    void setTemperament(const std::string& name);
    void setAdaptiveFrameSize(bool enabled);
    void setPostProcessorConfig(PostProcessor::Config cfg);
    void setHpfCutoff(float hz);
    void setOnsetDetectionEnabled(bool enabled);
    void setOnsetConfig(OnsetDetector::Config cfg);

    // Set overlap ratio (0.0–0.75). Recomputes hop size. Thread-safe.
    void setOverlapRatio(float ratio);

    // Reconfigure frame size and/or sample rate. Stops/restarts the worker thread,
    // reallocates internal buffers. Call from the JS/main thread only.
    void reconfigure(int newFrameSize, float sampleRate);

    int frameSize() const { return frameSize_; }
    int hopSize() const { return hopSize_; }

private:
    void workerLoop();
    void recomputeHopSize();
    void applyStoredSettings(); // re-apply cached settings after engine recreation

    static constexpr unsigned kRingCapacity = 32768u; // ~680ms at 48kHz — plenty of headroom

    int frameSize_;
    int hopSize_;
    float overlapRatio_;
    float sampleRate_;
    bool adaptiveFrameSize_{true}; // auto-resize frame on setInstrument
    PitchCallback callback_;

    FloatRingBuffer<kRingCapacity> ring_;
    std::vector<float> frameBuffer_;
    bool firstFrame_{true}; // cold-start: fill entire frame before first process

    mutable std::mutex engineMutex_; // protects engine_ access across threads
    std::unique_ptr<TunerEngine> engine_;

    // Cached settings — re-applied after engine recreation (setSampleRate / reconfigure)
    std::string currentInstrument_;
    std::string currentTuning_;
    std::string currentTemperament_;
    float currentA4_{440.0f};
    float currentNoiseGateDb_{-55.0f};
    float currentConfidenceThreshold_{0.75f};
    float currentMinHz_{60.0f};
    float currentMaxHz_{1200.0f};
    float currentHpfCutoff_{70.0f};
    bool currentOnsetEnabled_{false};
    PostProcessor::Config currentPostCfg_{};

    std::thread workerThread_;
    std::atomic<bool> running_{false};
};
