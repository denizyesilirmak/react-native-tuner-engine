#pragma once

#include "PitchResult.hpp"
#include "PostProcessor.hpp"
#include "RingBuffer.hpp"
#include "TunerEngine.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Bridges the real-time audio thread and the pitch-detection worker thread.
//
// The audio thread calls push() with raw PCM chunks — no locks, no allocation.
// An internal worker thread drains the ring into fixed-size frames, runs
// TunerEngine::process(), and delivers each PitchResult via the callback.
class AudioFrameDispatcher {
public:
    using PitchCallback = std::function<void(const PitchResult&)>;

    // frameSize: samples per processing frame (e.g. 2048)
    // sampleRate: initial sample rate — can change via setSampleRate()
    // callback: invoked from the worker thread; must be thread-safe w.r.t. the caller
    AudioFrameDispatcher(int frameSize, float sampleRate, PitchCallback callback);
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
    void setPostProcessorConfig(PostProcessor::Config cfg);
    void setHpfCutoff(float hz);

private:
    void workerLoop();

    static constexpr unsigned kRingCapacity = 32768u; // ~680ms at 48kHz — plenty of headroom

    int frameSize_;
    float sampleRate_;
    PitchCallback callback_;

    FloatRingBuffer<kRingCapacity> ring_;
    std::vector<float> frameBuffer_;

    mutable std::mutex engineMutex_; // protects engine_ access across threads
    std::unique_ptr<TunerEngine> engine_;

    std::thread workerThread_;
    std::atomic<bool> running_{false};
};
