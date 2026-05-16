#pragma once

#include <vector>

struct YinResult {
    bool hasPitch = false;
    float frequency = 0.0f;
    float confidence = 0.0f;
};

class YinPitchDetector {
public:
    YinPitchDetector(float sampleRate, int frameSize);

    YinResult detect(const float* input, int frameCount);

    void setFrequencyRange(float minFrequency, float maxFrequency);
    void setThreshold(float threshold);

private:
    float sampleRate_;
    int frameSize_;

    float minFrequency_ = 60.0f;
    float maxFrequency_ = 1200.0f;
    float threshold_ = 0.15f;

    std::vector<float> difference_;
    std::vector<float> cmnd_;

    float parabolicInterpolation(int tau) const;
};