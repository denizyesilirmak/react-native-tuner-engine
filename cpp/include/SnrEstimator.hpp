#pragma once

// Estimates signal-to-noise ratio by tracking a slow noise-floor EMA.
// The floor decays toward the current RMS level; it only rises when the
// signal drops below it (i.e. it tracks the quiet passages).
class SnrEstimator {
public:
    explicit SnrEstimator(float floorInitDb = -70.0f);

    // Update with current linear RMS and return SNR in dB.
    float update(float rmsLinear);

    // Map SNR to a confidence weight in [0, 1].
    static float snrToWeight(float snrDb);

    void reset();

private:
    float noiseFloorLinear_;
    static constexpr float kAttackAlpha  = 0.0100f; // how fast floor follows signal downward
};
