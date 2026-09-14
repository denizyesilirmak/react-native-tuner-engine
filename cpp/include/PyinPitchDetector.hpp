#pragma once

#include "IPitchDetector.hpp"
#include <vector>

// Probabilistic YIN (pYIN, Mauch & Dixon 2014) — frame-wise stage.
//
// Plain YIN picks the first CMND minimum below one fixed threshold, so a single
// unlucky threshold choice produces octave errors. pYIN instead treats the
// threshold as a random variable with a Beta(2, 18) prior: every CMND local
// minimum accumulates the prior mass of all thresholds at which YIN would have
// picked it. The candidate with the most mass wins.
//
// The paper's HMM/Viterbi tracking stage is replaced by two lighter mechanisms
// suited to real-time tuning: a small selection bonus for candidates near the
// previous frame's pitch (here), and the pipeline's PostProcessor
// (median + EMA + hysteresis) downstream.
class PyinPitchDetector : public IPitchDetector {
public:
    PyinPitchDetector(float sampleRate, int frameSize);

    DetectorResult detect(const float* frame, int frameLength, float sampleRate) override;

    void reset() override;
    void setFrequencyRange(float minHz, float maxHz) override;

private:
    struct PitchCandidate {
        int   lag;         // integer sample lag of the CMND local minimum
        float cmndDepth;   // CMND value at the minimum (lower = more periodic)
        float probability; // pYIN mass accumulated across all thresholds
    };

    float sampleRate_;
    int   frameSize_;

    float minFrequencyHz_ = 60.0f;
    float maxFrequencyHz_ = 1200.0f;

    std::vector<float> squaredDifference_;    // YIN step 2, indexed by lag
    std::vector<float> normalizedDifference_; // CMND, YIN step 3, indexed by lag
    std::vector<PitchCandidate> candidates_;  // pre-allocated, reused each frame

    std::vector<float> thresholdLevels_; // equally spaced YIN thresholds in (0, 1]
    std::vector<float> thresholdPriors_; // Beta(2, 18) weight per level, sums to 1

    float previousPitchHz_ = 0.0f; // continuity-bonus target; 0 = no history

    float refineLagByParabola(int lag) const;
};
