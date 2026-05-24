#include "AudioFrameDispatcher.hpp"
#include "InstrumentPresets.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

AudioFrameDispatcher::AudioFrameDispatcher(
    int frameSize, float sampleRate, PitchCallback callback, float overlapRatio
)
    : frameSize_(frameSize)
    , hopSize_(frameSize) // will be recomputed below
    , overlapRatio_(std::clamp(overlapRatio, 0.0f, 0.75f))
    , sampleRate_(sampleRate)
    , callback_(std::move(callback))
    , frameBuffer_(static_cast<size_t>(frameSize), 0.0f)
    , engine_(std::make_unique<TunerEngine>(sampleRate, frameSize))
{
    recomputeHopSize();
}

AudioFrameDispatcher::~AudioFrameDispatcher() {
    stop();
}

void AudioFrameDispatcher::recomputeHopSize() {
    hopSize_ = std::max(1, static_cast<int>(
        std::round(frameSize_ * (1.0f - overlapRatio_))
    ));
}

void AudioFrameDispatcher::start() {
    if (running_.exchange(true)) return; // already running
    firstFrame_ = true;
    workerThread_ = std::thread(&AudioFrameDispatcher::workerLoop, this);
}

void AudioFrameDispatcher::stop() {
    if (!running_.exchange(false)) return; // already stopped
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void AudioFrameDispatcher::push(const float* samples, int count) {
    ring_.push(samples, count);
}

void AudioFrameDispatcher::setSampleRate(float sampleRate) {
    if (sampleRate <= 0.0f) return;
    std::lock_guard<std::mutex> lock(engineMutex_);
    sampleRate_ = sampleRate;
    engine_ = std::make_unique<TunerEngine>(sampleRate, frameSize_);
}

void AudioFrameDispatcher::setA4(float hz) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    if (engine_) engine_->setA4(hz);
}

void AudioFrameDispatcher::setNoiseGateDb(float db) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    if (engine_) engine_->setNoiseGateDb(db);
}

void AudioFrameDispatcher::setConfidenceThreshold(float value) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    if (engine_) engine_->setConfidenceThreshold(value);
}

void AudioFrameDispatcher::setFrequencyRange(float minHz, float maxHz) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    if (engine_) engine_->setFrequencyRange(minHz, maxHz);
}

void AudioFrameDispatcher::setInstrument(const std::string& name) {
    // Check if the instrument's recommended frame size differs
    const int recommended = instrumentRecommendedFrameSize(name);
    if (recommended != frameSize_) {
        reconfigure(recommended, sampleRate_);
    }

    std::lock_guard<std::mutex> lock(engineMutex_);
    if (engine_) engine_->setInstrument(name);
}

void AudioFrameDispatcher::setTuning(const std::string& name) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    if (engine_) engine_->setTuning(name);
}

void AudioFrameDispatcher::setPostProcessorConfig(PostProcessor::Config cfg) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    if (engine_) engine_->setPostProcessorConfig(cfg);
}

void AudioFrameDispatcher::setHpfCutoff(float hz) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    if (engine_) engine_->setHpfCutoff(hz);
}

void AudioFrameDispatcher::setOnsetDetectionEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    if (engine_) engine_->setOnsetDetectionEnabled(enabled);
}

void AudioFrameDispatcher::setOnsetConfig(OnsetDetector::Config cfg) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    if (engine_) engine_->setOnsetConfig(cfg);
}

void AudioFrameDispatcher::setOverlapRatio(float ratio) {
    std::lock_guard<std::mutex> lock(engineMutex_);
    overlapRatio_ = std::clamp(ratio, 0.0f, 0.75f);
    recomputeHopSize();
    firstFrame_ = true; // reset sliding window state
}

void AudioFrameDispatcher::reconfigure(int newFrameSize, float sampleRate) {
    const bool wasRunning = running_.load();
    if (wasRunning) stop();

    {
        std::lock_guard<std::mutex> lock(engineMutex_);
        frameSize_ = newFrameSize;
        sampleRate_ = sampleRate;
        recomputeHopSize();
        frameBuffer_.assign(static_cast<size_t>(frameSize_), 0.0f);
        firstFrame_ = true;
        engine_ = std::make_unique<TunerEngine>(sampleRate_, frameSize_);
    }

    if (wasRunning) start();
}

void AudioFrameDispatcher::workerLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        if (firstFrame_) {
            // Cold start: wait for a full frame before first processing
            if (ring_.available() >= frameSize_) {
                ring_.pop(frameBuffer_.data(), frameSize_);
                firstFrame_ = false;

                PitchResult result;
                {
                    std::lock_guard<std::mutex> lock(engineMutex_);
                    result = engine_->process(frameBuffer_.data(), frameSize_);
                }
                if (callback_) {
                    callback_(result);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else {
            // Sliding window: shift old data left, pop hopSize_ new samples at the end
            if (ring_.available() >= hopSize_) {
                // Shift existing samples left by hopSize_
                const int retain = frameSize_ - hopSize_;
                std::memmove(frameBuffer_.data(),
                             frameBuffer_.data() + hopSize_,
                             static_cast<size_t>(retain) * sizeof(float));

                // Pop new samples into the tail
                ring_.pop(frameBuffer_.data() + retain, hopSize_);

                PitchResult result;
                {
                    std::lock_guard<std::mutex> lock(engineMutex_);
                    result = engine_->process(frameBuffer_.data(), frameSize_);
                }
                if (callback_) {
                    callback_(result);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
}
