#include "TunerEngine.hpp"
#include "CepstrumPitchDetector.hpp"
#include "DetectorFusion.hpp"
#include "PyinPitchDetector.hpp"
#include "TuningPresets.hpp"

#include <memory>

TunerEngine::TunerEngine(float sampleRate, int frameSize) {
    auto fusion = std::make_unique<DetectorFusion>(
        std::make_unique<PyinPitchDetector>(sampleRate, frameSize),
        std::make_unique<CepstrumPitchDetector>(sampleRate, frameSize)
    );
    pipeline_ = std::make_unique<Pipeline>(frameSize, sampleRate, std::move(fusion));
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
    // Auto-apply the default tuning for this instrument (e.g. "guitar" → "guitar_standard").
    // Can be overridden afterwards with an explicit setTuning() call.
    const std::string defaultTuning = defaultTuningForInstrument(name);
    pipeline_->setTuning(defaultTuning);
}

void TunerEngine::setTuning(const std::string& name) {
    pipeline_->setTuning(name);
}

void TunerEngine::setTemperament(const std::string& name) {
    pipeline_->setTemperament(name);
}

void TunerEngine::setPostProcessorConfig(PostProcessor::Config cfg) {
    pipeline_->setPostProcessorConfig(cfg);
}

void TunerEngine::setHpfCutoff(float hz) {
    pipeline_->setHpfCutoff(hz);
}

void TunerEngine::setOnsetDetectionEnabled(bool enabled) {
    pipeline_->setOnsetDetectionEnabled(enabled);
}

void TunerEngine::setOnsetConfig(OnsetDetector::Config cfg) {
    pipeline_->setOnsetConfig(cfg);
}
