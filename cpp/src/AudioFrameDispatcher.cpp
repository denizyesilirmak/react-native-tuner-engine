#include "AudioFrameDispatcher.hpp"

#include <chrono>

AudioFrameDispatcher::AudioFrameDispatcher(
    int frameSize, float sampleRate, PitchCallback callback
)
    : frameSize_(frameSize)
    , sampleRate_(sampleRate)
    , callback_(std::move(callback))
    , frameBuffer_(static_cast<size_t>(frameSize), 0.0f)
    , engine_(std::make_unique<TunerEngine>(sampleRate, frameSize))
{}

AudioFrameDispatcher::~AudioFrameDispatcher() {
    stop();
}

void AudioFrameDispatcher::start() {
    if (running_.exchange(true)) return; // already running
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

void AudioFrameDispatcher::workerLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        if (ring_.available() >= frameSize_) {
            const int read = ring_.pop(frameBuffer_.data(), frameSize_);
            if (read == frameSize_) {
                PitchResult result;
                {
                    std::lock_guard<std::mutex> lock(engineMutex_);
                    result = engine_->process(frameBuffer_.data(), frameSize_);
                }
                if (callback_) {
                    callback_(result);
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
