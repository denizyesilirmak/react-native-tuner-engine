#pragma once

#include "NoteMapper.hpp"
#include "PitchResult.hpp"
#include "YinPitchDetector.hpp"

class TunerEngine {
public:
    TunerEngine(float sampleRate, int frameSize);

    PitchResult process(const float* input, int frameCount);

    void setA4(float a4);
    void setNoiseGateDb(float db);
    void setConfidenceThreshold(float value);
    void setFrequencyRange(float minFrequency, float maxFrequency);

private:
    float calculateRmsDb(const float* input, int frameCount) const;

    float sampleRate_;
    int frameSize_;

    float noiseGateDb_ = -55.0f;
    float confidenceThreshold_ = 0.75f;

    NoteMapper noteMapper_;
    YinPitchDetector yin_;
};