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
        // Gated frames are the best view of the room's noise floor.
        snr_.update(rmsLinear, false);
        clearHold();
        PitchResult silent;
        silent.rmsDb = rmsDb;
        silent.stage = PitchStage::Gated;
        return silent;
    }

    // --- Onset detection (no-op when disabled — single branch) ---
    if (onsetDetector_.detect(rmsDb)) {
        postProcessor_.reset();
        clearHold();
    }

    // --- Working copy: HPF only.
    // Hann windowing is reserved for FFT-based detectors (M3 cepstrum/PYIN).
    // YIN works in the time domain — windowing distorts its difference function.
    std::copy(input, input + frameSize_, workBuffer_.begin());
    hpf_.process(workBuffer_.data(), frameSize_);

    // --- Pitch detection ---
    DetectorResult det = detector_->detect(workBuffer_.data(), frameSize_, sampleRate_);

    // --- SNR-weighted confidence ---
    const float snrDb     = snr_.update(rmsLinear, det.voiced);
    const float snrWeight = SnrEstimator::snrToWeight(snrDb);
    const float weightedConf = det.confidence * snrWeight;

    // A decaying string's confidence sinks below the threshold while the note
    // is still clearly there. A frame close to the note already shown may
    // continue it at a lower confidence; it can never start a note.
    bool held = false;
    if (noteHold_.enabled && heldFrequency_ > 0.0f && det.voiced
        && weightedConf < confidenceThreshold_ && weightedConf >= noteHold_.minConfidence
        && det.frequency > 0.0f) {
        const float cents = 1200.0f * std::log2(det.frequency / heldFrequency_);
        held = std::fabs(cents) <= noteHold_.maxCents;
    }

    if (!held && (!det.voiced || weightedConf < confidenceThreshold_)) {
        registerMiss();
        PitchResult nopit;
        nopit.rmsDb = rmsDb;
        nopit.stage = det.voiced ? PitchStage::LowConfidence : PitchStage::Unvoiced;
        nopit.detectorConfidence = det.confidence;
        nopit.snrDb = snrDb;
        return nopit;
    }

    // --- Post-process: median + EMA + hysteresis ---
    PostProcessor::Result pp = postProcessor_.process(det.frequency, weightedConf);

    if (!pp.isStable || pp.frequency <= 0.0f) {
        registerMiss();
        PitchResult nopit;
        nopit.rmsDb = rmsDb;
        nopit.stage = PitchStage::Settling;
        nopit.detectorConfidence = det.confidence;
        nopit.snrDb = snrDb;
        return nopit;
    }

    // --- Note mapping ---
    PitchResult result = noteMapper_.map(pp.frequency, weightedConf, rmsDb);
    // Override cents with the hysteresis-stabilised value from PostProcessor
    result.cents = pp.cents;
    heldFrequency_ = pp.frequency;
    missedFrames_  = 0;
    result.stage = PitchStage::Ok;
    result.detectorConfidence = det.confidence;
    result.snrDb = snrDb;

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
    clearHold();
}

void Pipeline::setInstrument(const std::string& name) {
    FrequencyRange r = instrumentPreset(name);
    detector_->setFrequencyRange(r.minHz, r.maxHz);
    clearHold();
}

void Pipeline::setTuning(const std::string& name) {
    stringMatcher_.setTuning(name.empty() ? nullptr : tuningPreset(name));
}

void Pipeline::setTemperament(const std::string& name) {
    noteMapper_.setTemperament(name);
}

void Pipeline::setPostProcessorConfig(PostProcessor::Config cfg) {
    postProcessor_.setConfig(cfg);
    clearHold();
}

void Pipeline::setNoteHold(NoteHold hold) {
    noteHold_ = hold;
    clearHold();
}

void Pipeline::clearHold() {
    heldFrequency_ = 0.0f;
    missedFrames_  = 0;
}

void Pipeline::registerMiss() {
    if (++missedFrames_ > noteHold_.maxMissedFrames) clearHold();
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
