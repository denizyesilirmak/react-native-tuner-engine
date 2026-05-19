#include "TunerEngine.hpp"
#include "YinPitchDetector.hpp"

#include <memory>

TunerEngine::TunerEngine(float sampleRate, int frameSize) {
    auto detector = std::make_unique<YinPitchDetector>(sampleRate, frameSize);
    pipeline_ = std::make_unique<Pipeline>(frameSize, sampleRate, std::move(detector));
}

PitchResult TunerEngine::process(const float* input, int frameCount) {
    return pipeline_->process(input, frameCount);
}

void TunerEngine::setA4(float a4) {
    pipeline_->setA4(a4);
}

void TunerEngine::setNoiseGateDb(float db) {
    pipeline_->setNoiseGateDb(db);
}

void TunerEngine::setConfidenceThreshold(float value) {
    pipeline_->setConfidenceThreshold(value);
}

void TunerEngine::setFrequencyRange(float minFrequency, float maxFrequency) {
    pipeline_->setFrequencyRange(minFrequency, maxFrequency);
}

void TunerEngine::setInstrument(const std::string& name) {
    pipeline_->setInstrument(name);
}
