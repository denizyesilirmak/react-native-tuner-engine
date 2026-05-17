#pragma once

#include "IPitchDetector.hpp"
#include <vector>

struct YinResult {
    bool hasPitch = false;
    float frequency = 0.0f;
    float confidence = 0.0f;
};

class YinPitchDetector : public IPitchDetector {
public:
    YinPitchDetector(float sampleRate, int frameSize);

    // Legacy interface used by existing tests
    YinResult detect(const float* input, int frameCount);

    // IPitchDetector — delegates to the above
    DetectorResult detect(const float* frame, int n, float sampleRate) override;

    void setFrequencyRange(float minFrequency, float maxFrequency) override;
    void setThreshold(float threshold) override;

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
