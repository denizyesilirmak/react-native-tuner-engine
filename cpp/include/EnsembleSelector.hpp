#pragma once

#include "IPitchDetector.hpp"
#include <memory>
#include <vector>

// Runs every sub-detector on the same frame and selects the best result.
// Detectors that agree within one semitone reinforce each other (confidence bonus).
// A lone detector with no agreement receives a confidence penalty.
class EnsembleSelector : public IPitchDetector {
public:
    explicit EnsembleSelector(std::vector<std::unique_ptr<IPitchDetector>> detectors);

    DetectorResult detect(const float* frame, int n, float sampleRate) override;

    void reset() override;
    void setFrequencyRange(float minHz, float maxHz) override;
    void setThreshold(float threshold) override;

private:
    std::vector<std::unique_ptr<IPitchDetector>> detectors_;
    std::vector<DetectorResult> resultsBuf_;

    static bool withinSemitones(float f1, float f2, float tolerance = 1.0f);
};
