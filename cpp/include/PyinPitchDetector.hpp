#pragma once

#include "IPitchDetector.hpp"
#include <vector>

// Probabilistic YIN (PYIN) — unlike plain YIN, it collects every CMND local minimum
// below threshold and picks the most probable one rather than the first.
// This eliminates the octave errors that occur when the first minimum corresponds
// to a harmonic period rather than the fundamental.
class PyinPitchDetector : public IPitchDetector {
public:
    PyinPitchDetector(float sampleRate, int frameSize);

    DetectorResult detect(const float* frame, int n, float sampleRate) override;

    void reset() override {}
    void setFrequencyRange(float minHz, float maxHz) override;
    void setThreshold(float threshold) override;

private:
    float sampleRate_;
    int   frameSize_;

    float minHz_     = 60.0f;
    float maxHz_     = 1200.0f;
    float threshold_ = 0.25f; // wider than YIN — captures all plausible candidates

    std::vector<float> diff_;
    std::vector<float> cmnd_;

    struct Candidate { int tau; float prob; };
    std::vector<Candidate> candidates_; // pre-allocated, reused each frame

    float parabolicInterpolation(int tau) const;
};
