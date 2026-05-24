#include "Pipeline.hpp"

#include <algorithm>
#include <cmath>

static constexpr float kMinLinear = 1e-7f;

Pipeline::Pipeline(int frameSize, float sampleRate, std::unique_ptr<IPitchDetector> detector)
    : frameSize_(frameSize)
    , sampleRate_(sampleRate)
    , hpf_(sampleRate, 70.0f)       // 70 Hz HPF — removes DC and sub-bass rumble
    , window_(frameSize)
    , detector_(std::move(detector))
    , workBuffer_(static_cast<size_t>(frameSize))
{}

PitchResult Pipeline::process(const float* input, int frameCount) {
    if (!input || frameCount < frameSize_) return PitchResult{};

    // --- RMS gate ---
    const float rmsLinear = calculateRmsLinear(input, frameSize_);
    const float rmsDb     = 20.0f * std::log10(std::max(rmsLinear, kMinLinear));

    if (rmsDb < noiseGateDb_) {
        PitchResult silent;
        silent.rmsDb = rmsDb;
        return silent;
    }

    // --- Onset detection (no-op when disabled — single branch) ---
    if (onsetDetector_.detect(rmsDb)) {
        postProcessor_.reset();
    }

    // --- Working copy: HPF only.
    // Hann windowing is reserved for FFT-based detectors (M3 cepstrum/PYIN).
    // YIN works in the time domain — windowing distorts its difference function.
    std::copy(input, input + frameSize_, workBuffer_.begin());
    hpf_.process(workBuffer_.data(), frameSize_);

    // --- Pitch detection ---
    DetectorResult det = detector_->detect(workBuffer_.data(), frameSize_, sampleRate_);

    // --- SNR-weighted confidence ---
    const float snrDb     = snr_.update(rmsLinear);
    const float snrWeight = SnrEstimator::snrToWeight(snrDb);
    const float weightedConf = det.confidence * snrWeight;

    if (!det.voiced || weightedConf < confidenceThreshold_) {
        PitchResult nopit;
        nopit.rmsDb = rmsDb;
        return nopit;
    }

    // --- Post-process: median + EMA + hysteresis ---
    PostProcessor::Result pp = postProcessor_.process(det.frequency, weightedConf);

    if (!pp.isStable || pp.frequency <= 0.0f) {
        PitchResult nopit;
        nopit.rmsDb = rmsDb;
        return nopit;
    }

    // --- Note mapping ---
    PitchResult result = noteMapper_.map(pp.frequency, weightedConf, rmsDb);
    // Override cents with the hysteresis-stabilised value from PostProcessor
    result.cents = pp.cents;

    // --- String matching (optional, only when a TuningProfile is active) ---
    if (stringMatcher_.hasTuning()) {
        auto m = stringMatcher_.match(pp.frequency);
        if (m) {
            result.nearestString    = m->name;
            result.stringDeviation  = m->deviationCents;
        }
    }

    return result;
}

void Pipeline::setA4(float hz) {
    noteMapper_.setA4(hz);
}

void Pipeline::setNoiseGateDb(float db) {
    noiseGateDb_ = db;
}

void Pipeline::setConfidenceThreshold(float threshold) {
    confidenceThreshold_ = threshold;
}

void Pipeline::setFrequencyRange(float minHz, float maxHz) {
    detector_->setFrequencyRange(minHz, maxHz);
}

void Pipeline::setInstrument(const std::string& name) {
    FrequencyRange r = instrumentPreset(name);
    detector_->setFrequencyRange(r.minHz, r.maxHz);
}

void Pipeline::setTuning(const std::string& name) {
    stringMatcher_.setTuning(name.empty() ? nullptr : tuningPreset(name));
}

void Pipeline::setPostProcessorConfig(PostProcessor::Config cfg) {
    postProcessor_.setConfig(cfg);
}

void Pipeline::setHpfCutoff(float hz) {
    hpf_ = BiquadHpf(sampleRate_, hz);
}

void Pipeline::setOnsetDetectionEnabled(bool enabled) {
    onsetDetector_.setEnabled(enabled);
    if (!enabled) onsetDetector_.reset();
}

void Pipeline::setOnsetConfig(OnsetDetector::Config cfg) {
    onsetDetector_.setConfig(cfg);
}

float Pipeline::calculateRmsDb(const float* input, int n) const {
    return 20.0f * std::log10(std::max(calculateRmsLinear(input, n), kMinLinear));
}

float Pipeline::calculateRmsLinear(const float* input, int n) const {
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        sum += input[i] * input[i];
    }
    return std::sqrt(sum / static_cast<float>(n));
}
