#include "TunerEngine.hpp"

#include <cmath>

TunerEngine::TunerEngine(float sampleRate, int frameSize)
    : sampleRate_(sampleRate),
      frameSize_(frameSize),
      noteMapper_(440.0f),
      yin_(sampleRate, frameSize) {}

void TunerEngine::setA4(float a4) {
    noteMapper_.setA4(a4);
}

void TunerEngine::setNoiseGateDb(float db) {
    noiseGateDb_ = db;
}

void TunerEngine::setConfidenceThreshold(float value) {
    confidenceThreshold_ = value;
}

void TunerEngine::setFrequencyRange(float minFrequency, float maxFrequency) {
    yin_.setFrequencyRange(minFrequency, maxFrequency);
}

PitchResult TunerEngine::process(const float* input, int frameCount) {
    PitchResult empty;

    if (input == nullptr || frameCount < frameSize_) {
        return empty;
    }

    const float rmsDb = calculateRmsDb(input, frameSize_);

    if (rmsDb < noiseGateDb_) {
        empty.rmsDb = rmsDb;
        return empty;
    }

    const auto yinResult = yin_.detect(input, frameSize_);

    if (!yinResult.hasPitch || yinResult.confidence < confidenceThreshold_) {
        empty.rmsDb = rmsDb;
        empty.confidence = yinResult.confidence;
        return empty;
    }

    return noteMapper_.map(yinResult.frequency, yinResult.confidence, rmsDb);
}

float TunerEngine::calculateRmsDb(const float* input, int frameCount) const {
    float sum = 0.0f;

    for (int i = 0; i < frameCount; ++i) {
        sum += input[i] * input[i];
    }

    const float rms = std::sqrt(sum / frameCount);

    if (rms <= 1e-9f) {
        return -120.0f;
    }

    return 20.0f * std::log10(rms);
}