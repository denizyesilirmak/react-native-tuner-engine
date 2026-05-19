#pragma once

struct DetectorResult {
    bool voiced = false;
    float frequency = 0.0f;
    float confidence = 0.0f;
};

class IPitchDetector {
public:
    virtual ~IPitchDetector() = default;

    // Detect pitch from a pre-filtered, windowed frame of length n at the given sample rate.
    virtual DetectorResult detect(const float* frame, int n, float sampleRate) = 0;

    // Reset any inter-frame state (Viterbi, smoothing, etc.).
    virtual void reset() {}

    virtual void setFrequencyRange(float minHz, float maxHz) = 0;
    virtual void setThreshold(float threshold) = 0;
};
